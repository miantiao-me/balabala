#!/usr/bin/env python3
"""用隔离归档/Git fixture 实跑补丁及构建入口，不修改真实 .tools。"""
import difflib
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(args, cwd, **kwargs):
    return subprocess.run(args, cwd=cwd, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, **kwargs).stdout


def git(repo, *args):
    return run(['git', '-c', 'protocol.file.allow=always', '-C', str(repo), *args], repo).decode().strip()


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def repository(path):
    path.mkdir(parents=True)
    git(path, 'init', '-q')
    git(path, 'config', 'user.name', 'Source fixture')
    git(path, 'config', 'user.email', 'fixture@example.invalid')
    write(path / 'source.c', 'original source\n')
    git(path, 'add', '.')
    git(path, 'commit', '-qm', 'fixture source')


def patch(root, name, changes):
    text = ''
    files = []
    for path, before, after in changes:
        text += ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                            fromfile='a/' + path, tofile='b/' + path))
        files.append({'path': path, 'before': digest(before.encode()), 'after': digest(after.encode())})
    write(root / 'patches' / name, text)
    return {'patch': name, 'sha256': digest(text.encode()), 'files': files}


def fixture(root, repositories):
    for name in ['apply-patches.py', 'apply-idf-patches.py', 'source_integrity.py',
                 'versions.sh', 'env.sh', 'build.sh', 'setup.sh']:
        target = root / 'tools' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'tools' / name, target)
    write(root / 'tools/fetch-components.py', '')
    write(root / 'tools/build-native.ts', 'resource sentinel must not execute\n')
    pocket_commit = 'a' * 40
    pocket = root / '.tools/pocketjs'
    write(pocket / 'engine/core.rs', 'core original\n')
    write(pocket / 'compiler/main.ts', 'compiler original\n')
    write(pocket / 'CLAUDE.md', 'archive identity\n')
    write(pocket / 'node_modules/archive-owned.js', 'archive-owned input\n')
    (pocket / 'AGENTS.md').symlink_to('CLAUDE.md')
    archive = root / '.tools/downloads/pocketjs.tar.gz'
    archive.parent.mkdir(parents=True)
    with tarfile.open(archive, 'w:gz') as bundle:
        bundle.add(pocket, arcname='pocketjs-' + pocket_commit)
    write(pocket / '.balabala-version', pocket_commit + '\n')
    write(pocket / 'node_modules/installed/index.js', 'derived dependency\n')
    specs = [patch(root, 'pocket-one.patch', [('engine/core.rs', 'core original\n', 'core one\n')]),
             patch(root, 'pocket-two.patch', [('engine/core.rs', 'core one\n', 'core final\n')])]
    write(root / 'patches/manifest.json', json.dumps({'commit': pocket_commit, 'patches': specs}))

    idf = root / '.tools/esp-idf'
    repository(idf)
    git(idf, 'submodule', 'add', '-q', str(repositories / 'child'), 'deps/child')
    # 嵌套子模块需要显式初始化；校验必须递归覆盖它。
    git(idf, 'submodule', 'update', '--init', '--recursive')
    for path in ['patched.c', 'instance.c', 'mempool.c']:
        write(idf / path, path + ' original\n')
    write(idf / '.gitignore', '__pycache__/\n')
    write(idf / 'tools/idf_tools.py', "import os\nfrom pathlib import Path\nPath(os.environ['IDF_SENTINEL']).write_text('executed')\n")
    git(idf, 'add', '.')
    git(idf, 'commit', '-qm', 'fixture IDF')
    idf_commit = git(idf, 'rev-parse', 'HEAD')
    specs = [patch(root, 'idf-one.patch', [('patched.c', 'patched.c original\n', 'patched.c one\n')]),
             patch(root, 'idf-instance.patch', [('instance.c', 'instance.c original\n', 'instance final\n')]),
             patch(root, 'idf-mempool.patch', [('mempool.c', 'mempool.c original\n', 'mempool final\n')]),
             patch(root, 'idf-observer.patch', [('patched.c', 'patched.c one\n', 'patched.c final\n')])]
    manifest = dict(specs[0], commit=idf_commit, instanceRemoval=specs[1],
                    mempoolReinit=specs[2], advertisingObserver=specs[3])
    write(root / 'patches/esp-idf-6.1.json', json.dumps(manifest))
    with (root / 'tools/versions.sh').open('a') as output:
        output.write(f'\nPOCKETJS_COMMIT={pocket_commit}\nPOCKETJS_SHA256={digest(archive.read_bytes())}\nIDF_COMMIT={idf_commit}\n')
    fakebin = root / '.build/bin'
    write(fakebin / 'rustup', '#!/bin/sh\nexit 0\n')
    write(fakebin / 'curl', '#!/bin/sh\nfor arg do dest=$arg; done\ncp "$FIXTURE_ARCHIVE" "$dest"\nprintf downloaded > "$DOWNLOAD_SENTINEL"\n')
    write(root / '.tools/bun/bun', '#!/bin/sh\n. "$(dirname "$0")/../../tools/versions.sh"\nif [ "$1" = --version ]; then echo "$BUN_VERSION"; else\n case "$*" in *build-native.ts*) printf executed > "$RESOURCE_SENTINEL";; esac\nfi\n')
    for binary in [fakebin / 'rustup', fakebin / 'curl', root / '.tools/bun/bun']:
        binary.chmod(0o755)
    shutil.copyfile(archive, root / '.build/pocketjs.tar.gz')
    write(root / '.build/generated/aot.json', 'frozen resource sentinel\n')


class SourceIntegrity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.space = tempfile.TemporaryDirectory(prefix='balabala-source-integrity-')
        cls.base = Path(cls.space.name)
        repos = cls.base / 'repositories'
        repository(repos / 'leaf')
        repository(repos / 'child')
        git(repos / 'child', 'submodule', 'add', '-q', str(repos / 'leaf'), 'nested/leaf')
        git(repos / 'child', 'commit', '-qam', 'nested module')
        cls.template = cls.base / 'template'
        fixture(cls.template, repos)

    @classmethod
    def tearDownClass(cls):
        cls.space.cleanup()

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(dir=self.base, prefix='case-')
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name) / 'project'
        shutil.copytree(self.template, self.root, symlinks=True)
        self.pocket = self.root / '.tools/pocketjs'
        self.idf = self.root / '.tools/esp-idf'
        self.env = dict(os.environ, PATH=str(self.root / '.build/bin') + os.pathsep + os.environ['PATH'],
                        IDF_SENTINEL=str(self.root / '.build/idf-executed'),
                        RESOURCE_SENTINEL=str(self.root / '.build/resource-executed'),
                        FIXTURE_ARCHIVE=str(self.root / '.build/pocketjs.tar.gz'),
                        DOWNLOAD_SENTINEL=str(self.root / '.build/downloaded'))
        self.generated = self.root / '.build/generated/aot.json'
        self.generated_before = (self.generated.stat().st_mtime_ns, self.generated.read_bytes())

    def invoke(self, script, success=True, shell=False):
        command = ['/bin/sh' if shell else 'python3', 'tools/' + script]
        if script == 'build.sh':
            command.append('test')
        result = subprocess.run(command, cwd=self.root, env=self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode == 0, success, result.stdout.decode())
        return result.stdout.decode()

    def rejected(self, script, expected, shell=False, idf_may_execute=False):
        text = self.invoke(script, success=False, shell=shell)
        self.assertIn(expected, text)
        if not idf_may_execute:
            self.assertFalse(Path(self.env['IDF_SENTINEL']).exists())
        self.assertFalse(Path(self.env['RESOURCE_SENTINEL']).exists())
        self.assertEqual((self.generated.stat().st_mtime_ns, self.generated.read_bytes()), self.generated_before)

    def applied(self):
        self.invoke('apply-patches.py')
        self.invoke('apply-idf-patches.py')

    def test_original_and_applied_sources_pass_idempotently(self):
        self.applied()
        self.assertEqual((self.pocket / 'engine/core.rs').read_text(), 'core final\n')
        self.assertEqual((self.idf / 'patched.c').read_text(), 'patched.c final\n')
        self.applied()

    def test_legal_patch_prefixes_pass(self):
        for script, manifest_name, source in [('apply-patches.py', 'manifest.json', self.pocket),
                                               ('apply-idf-patches.py', 'esp-idf-6.1.json', self.idf)]:
            manifest = json.loads((self.root / 'patches' / manifest_name).read_text())
            entry = manifest['patches'][0] if 'patches' in manifest else manifest
            run(['patch', '--batch', '-p1', '-i', str(self.root / 'patches' / entry['patch'])], source)
            self.invoke(script)

    def test_all_idf_patch_prefixes_pass_idempotently(self):
        manifest = json.loads((self.root / 'patches/esp-idf-6.1.json').read_text())
        specs = [manifest, manifest['instanceRemoval'], manifest['mempoolReinit'],
                 manifest['advertisingObserver']]
        for count in range(len(specs) + 1):
            with self.subTest(prefix=count):
                git(self.idf, 'checkout', '--', '.')
                for spec in specs[:count]:
                    run(['patch', '--batch', '-p1', '-i', str(self.root / 'patches' / spec['patch'])], self.idf)
                self.invoke('apply-idf-patches.py')
                self.invoke('apply-idf-patches.py')
                self.assertEqual((self.idf / 'patched.c').read_text(), 'patched.c final\n')
                self.assertEqual((self.idf / 'instance.c').read_text(), 'instance final\n')
                self.assertEqual((self.idf / 'mempool.c').read_text(), 'mempool final\n')

    def test_unpatched_compiler_drift_fails_before_patching_or_resources(self):
        write(self.pocket / 'compiler/main.ts', 'compiler drift\n')
        self.rejected('build.sh', 'PocketJS source differs: compiler/main.ts', shell=True, idf_may_execute=True)
        self.assertEqual((self.pocket / 'engine/core.rs').read_text(), 'core original\n')

    def test_idf_ordinary_source_drift_fails_before_patching(self):
        write(self.idf / 'source.c', 'source drift\n')
        self.rejected('apply-idf-patches.py', 'unexpected tracked source change')
        self.assertEqual((self.idf / 'patched.c').read_text(), 'patched.c original\n')

    def test_submodule_source_drift_is_not_hidden_by_ignore_config(self):
        git(self.idf, 'config', 'submodule.deps/child.ignore', 'all')
        write(self.idf / 'deps/child/source.c', 'submodule drift\n')
        self.rejected('apply-idf-patches.py', 'source')

    def test_nested_submodule_staged_source_drift_is_rejected(self):
        leaf = self.idf / 'deps/child/nested/leaf'
        write(leaf / 'source.c', 'nested staged drift\n')
        git(leaf, 'add', 'source.c')
        self.rejected('apply-idf-patches.py', 'source')

    def test_submodule_commit_drift_is_rejected_before_setup_update(self):
        child = self.idf / 'deps/child'
        git(child, 'checkout', '-q', 'HEAD^')
        drift = git(child, 'rev-parse', 'HEAD')
        self.rejected('setup.sh', 'ESP-IDF', shell=True)
        self.assertEqual(git(child, 'rev-parse', 'HEAD'), drift)

    def test_staged_root_source_drift_is_rejected(self):
        write(self.idf / 'source.c', 'staged drift\n')
        git(self.idf, 'add', 'source.c')
        # 工作区恢复原文后，暂存区的漂移仍不能被隐藏。
        write(self.idf / 'source.c', 'original source\n')
        self.rejected('apply-idf-patches.py', 'unexpected tracked source change')

    def test_staged_patch_target_requires_known_content(self):
        write(self.idf / 'patched.c', 'staged drift\n')
        git(self.idf, 'add', 'patched.c')
        write(self.idf / 'patched.c', 'patched.c original\n')
        self.rejected('apply-idf-patches.py', 'staged patch source differs')

    def test_corrupt_archive_is_rejected_with_existing_marker(self):
        write(self.root / '.tools/downloads/pocketjs.tar.gz', 'corrupt archive\n')
        self.rejected('build.sh', 'PocketJS archive checksum mismatch', shell=True, idf_may_execute=True)
        Path(self.env['IDF_SENTINEL']).unlink()
        self.rejected('setup.sh', 'PocketJS SHA256 mismatch', shell=True)

    def test_symlink_replaced_with_identical_regular_file_is_rejected(self):
        target = self.pocket / 'AGENTS.md'
        contents = target.read_bytes()
        target.unlink()
        target.write_bytes(contents)
        self.rejected('apply-patches.py', 'PocketJS source differs: AGENTS.md')

    def test_pocket_extra_source_and_nested_dependency_paths_are_rejected(self):
        for path in ['compiler/extra.ts', 'engine/node_modules/extra.ts', 'extra.json']:
            with self.subTest(path=path):
                write(self.pocket / path, 'additional input\n')
                self.rejected('apply-patches.py', 'PocketJS unexpected source')
                (self.pocket / path).unlink()
                if path == 'engine/node_modules/extra.ts':
                    (self.pocket / 'engine/node_modules').rmdir()

    def test_idf_extra_source_and_submodule_untracked_source_are_rejected(self):
        write(self.idf / 'extra.c', 'additional input\n')
        self.rejected('apply-idf-patches.py', 'unexpected untracked source')
        (self.idf / 'extra.c').unlink()
        write(self.idf / 'deps/child/extra.c', 'additional submodule input\n')
        self.rejected('apply-idf-patches.py', 'source')

    def test_changed_idf_tools_never_executes_at_build_or_setup_entry(self):
        write(self.idf / 'tools/idf_tools.py', "import os\nfrom pathlib import Path\nPath(os.environ['IDF_SENTINEL']).touch()\n")
        self.rejected('build.sh', 'unexpected tracked source change', shell=True)
        self.rejected('setup.sh', 'unexpected tracked source change', shell=True)

    def test_cached_setup_downloads_missing_archive_and_validates_it(self):
        (self.root / '.tools/downloads/pocketjs.tar.gz').unlink()
        self.invoke('setup.sh', shell=True)
        self.assertTrue(Path(self.env['DOWNLOAD_SENTINEL']).exists())
        self.assertTrue(Path(self.env['IDF_SENTINEL']).exists())
        Path(self.env['IDF_SENTINEL']).unlink()
        (self.root / '.tools/downloads/pocketjs.tar.gz').unlink()
        write(Path(self.env['FIXTURE_ARCHIVE']), 'bad downloaded archive\n')
        self.rejected('setup.sh', 'PocketJS SHA256 mismatch', shell=True)

    def test_archive_owned_members_inside_derived_root_are_still_checked(self):
        write(self.pocket / 'node_modules/archive-owned.js', 'archive-owned drift\n')
        self.rejected('apply-patches.py', 'PocketJS source differs: node_modules/archive-owned.js')

    def test_archive_directory_and_symlink_target_types_are_checked(self):
        compiler = self.pocket / 'compiler'
        compiler.rename(self.pocket / 'moved-compiler')
        compiler.symlink_to('moved-compiler', target_is_directory=True)
        self.rejected('apply-patches.py', 'PocketJS source differs: compiler')
        compiler.unlink()
        (self.pocket / 'moved-compiler').rename(compiler)
        write(self.pocket / 'ALTERNATE.md', 'archive identity\n')
        (self.pocket / 'AGENTS.md').unlink()
        (self.pocket / 'AGENTS.md').symlink_to('ALTERNATE.md')
        self.rejected('apply-patches.py', 'PocketJS source differs: AGENTS.md')

    def test_pinned_commit_mismatch_is_rejected(self):
        with (self.root / 'tools/versions.sh').open('a') as output:
            output.write('POCKETJS_COMMIT=' + 'b' * 40 + '\nIDF_COMMIT=' + 'c' * 40 + '\n')
        self.rejected('apply-patches.py', 'PocketJS pinned commit mismatch')
        self.rejected('apply-idf-patches.py', 'ESP-IDF pinned commit mismatch')


if __name__ == '__main__':
    unittest.main(verbosity=2)
