#!/usr/bin/env python3
"""实跑安装/导出隔离，并故障注入检查构建在生成资源前退出。"""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
out = root / '.build'
out.mkdir(exist_ok=True)

def snapshot(path):
    return [(str(p.relative_to(path)), p.stat().st_mtime_ns,
             hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else None)
            for p in sorted(path.rglob('*'))]

def run(args, env, logfile, expected=0):
    result = subprocess.run(args, cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / logfile).write_bytes(result.stdout)
    assert (result.returncode == 0) == (expected == 0), (args, result.returncode, result.stdout[-3000:])
    return result.stdout.decode()

with tempfile.TemporaryDirectory(prefix='balabala-idf-external-') as external, tempfile.TemporaryDirectory(prefix='balabala-env-bin-') as scratch:
    external = Path(external)
    (external / 'do-not-change').write_text('external environment sentinel\n')
    before = snapshot(external)
    scratch = Path(scratch)
    wrapper = scratch / 'python3'
    wrapper.write_text(f'''#!{sys.executable}
import os,sys
from pathlib import Path
if len(sys.argv)>1 and sys.argv[1].endswith('/idf_tools.py'):
    assert 'IDF_PYTHON_ENV_PATH' not in os.environ, 'inherited external Python environment'
    with open(os.environ['BALABALA_ENV_AUDIT'],'a') as audit: audit.write(' '.join(sys.argv[2:])+'\\n')
    if os.environ.get('BALABALA_EXPORT_FAIL')=='1' and 'export' in sys.argv:
        print('injected IDF export failure',file=sys.stderr)
        sys.exit(73)
os.execv({sys.executable!r},[{sys.executable!r},*sys.argv[1:]])
''')
    wrapper.chmod(0o755)
    env = dict(os.environ, IDF_PYTHON_ENV_PATH=str(external),
               PATH=str(scratch) + os.pathsep + os.environ['PATH'],
               BALABALA_ENV_AUDIT=str(out / 'env-audit.txt'))
    (out / 'env-audit.txt').write_text('')
    run(['/bin/sh', 'tools/setup.sh'], env, 'env-setup.log')
    text = run(['/bin/sh', '-ec', 'root=$PWD; . "$root/tools/env.sh"; printf "%s\\n" "$IDF_PYTHON_ENV_PATH"'], env, 'env-export.log')
    selected = text.strip().splitlines()[-1]
    assert selected.startswith(str(root / '.tools/idf-tools/python_env') + '/'), selected
    assert snapshot(external) == before, 'external environment changed'
    audit = (out / 'env-audit.txt').read_text()
    assert 'install --targets esp32c3' in audit and 'install-python-env' in audit and 'export --format shell' in audit
    print('PASS setup/export ignore inherited external IDF_PYTHON_ENV_PATH; external directory unchanged')

    generated = out / 'generated/aot.json'
    generated_before = (generated.stat().st_mtime_ns, hashlib.sha256(generated.read_bytes()).hexdigest())
    failed_env = dict(env, BALABALA_EXPORT_FAIL='1')
    text = run(['/bin/sh', 'tools/build.sh', 'test'], failed_env, 'env-export-failure.log', expected=1)
    assert 'injected IDF export failure' in text and 'ESP-IDF environment export failed' in text
    assert (generated.stat().st_mtime_ns, hashlib.sha256(generated.read_bytes()).hexdigest()) == generated_before
    print('PASS IDF export failure exits before build-native/resource generation')

    missing = scratch / 'missing-prerequisite'
    missing.mkdir()
    (missing / 'dirname').symlink_to(shutil.which('dirname'))
    missing_env = dict(env, PATH=str(missing))
    for entry in ['setup', 'build']:
        text = run(['/bin/sh', f'tools/{entry}.sh'], missing_env, f'env-no-shasum-{entry}.log', expected=1)
        assert 'Missing prerequisite: shasum' in text
    print('PASS setup/build reject missing shasum before dependency or resource writes')
