"""固定派生源码的输入检查；补丁状态仍由现有 manifest 决定。"""
import hashlib
import os
from pathlib import Path
import stat
import subprocess
import tarfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def versions(root):
    return dict(line.split('=', 1) for line in (root / 'tools/versions.sh').read_text().splitlines()
                if line and not line.startswith('#'))


def verify_pocket(root, source, manifest, state):
    pins = versions(root)
    archive = root / '.tools/downloads/pocketjs.tar.gz'
    if manifest['commit'] != pins['POCKETJS_COMMIT']:
        raise SystemExit('PocketJS pinned commit mismatch')
    if not archive.is_file() or sha(archive) != pins['POCKETJS_SHA256']:
        raise SystemExit('PocketJS archive checksum mismatch')
    expected = set()
    with tarfile.open(archive, 'r:gz') as bundle:
        for member in bundle:
            parts = Path(member.name).parts
            if not parts or parts[0] != 'pocketjs-' + pins['POCKETJS_COMMIT'] or '..' in parts:
                raise SystemExit('PocketJS archive path mismatch')
            relative = Path(*parts[1:])
            expected.add(relative.as_posix())
            target = source / relative
            try:
                mode = target.lstat().st_mode
            except FileNotFoundError:
                raise SystemExit(f'PocketJS source missing: {relative}')
            valid = False
            if member.isfile() and stat.S_ISREG(mode):
                digest = hashlib.sha256(bundle.extractfile(member).read()).hexdigest()
                valid = sha(target) == state.get(relative.as_posix(), digest)
            elif member.isdir():
                valid = stat.S_ISDIR(mode)
            elif member.issym() and stat.S_ISLNK(mode):
                valid = os.readlink(target) == member.linkname
            if not valid:
                raise SystemExit(f'PocketJS source differs: {relative}')
    # 仅根目录安装依赖和版本标记是归档外的派生产物；归档成员始终逐项检查。
    def extras(directory):
        for target in directory.iterdir():
            relative = target.relative_to(source).as_posix()
            if relative in {'.balabala-version', 'node_modules'}:
                continue
            if relative not in expected:
                raise SystemExit(f'PocketJS unexpected source: {relative}')
            if target.is_dir() and not target.is_symlink():
                extras(target)
    extras(source)


def git(source, *args):
    return subprocess.check_output([
        'git', '-C', str(source), '-c', 'core.quotePath=false',
        '-c', 'diff.ignoreSubmodules=none', *args], text=True)


def verify_idf(root, source, manifest, targets):
    if manifest['commit'] != versions(root)['IDF_COMMIT'] or git(source, 'rev-parse', 'HEAD').strip() != manifest['commit']:
        raise SystemExit('ESP-IDF pinned commit mismatch')
    # 分别检查 index 与工作树，避免暂存后反向修改隐藏漂移。
    for args in [('diff', '--cached'), ('diff',)]:
        changed = git(source, *args, '--name-only', '-z', '--ignore-submodules=none').split('\0')
        if any(path and path not in targets for path in changed):
            raise SystemExit('ESP-IDF unexpected tracked source change')
        if '--cached' in args:
            for path in filter(None, changed):
                staged = subprocess.check_output(['git', '-C', str(source), 'show', ':' + path])
                if hashlib.sha256(staged).hexdigest() not in targets[path]:
                    raise SystemExit('ESP-IDF staged patch source differs')
    if git(source, 'ls-files', '--others', '--exclude-standard', '-z'):
        raise SystemExit('ESP-IDF unexpected untracked source')
    for line in git(source, 'submodule', 'status', '--recursive').splitlines():
        if not line:
            continue
        if line[0] != ' ':
            raise SystemExit('ESP-IDF submodule commit mismatch')
        path = line[42:].split(' (', 1)[0]
        if git(source / path, 'status', '--porcelain=v1', '-z', '--untracked-files=all', '--ignore-submodules=none'):
            raise SystemExit(f'ESP-IDF submodule source differs: {path}')
    for path in targets:
        if not stat.S_ISREG((source / path).lstat().st_mode):
            raise SystemExit(f'ESP-IDF patch target type differs: {path}')
