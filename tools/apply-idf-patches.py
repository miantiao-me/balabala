#!/usr/bin/env python3
"""校验/修改固定IDF副本的通知、事件回收与mempool重初始化。"""
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
from source_integrity import sha, verify_idf

root = Path(__file__).resolve().parent.parent
source = root / '.tools/esp-idf'
manifest = json.loads((root / 'patches/esp-idf-6.1.json').read_text())
if source.resolve() != source:
    raise SystemExit('ESP-IDF derived source path/version mismatch')
specs = [manifest, manifest['instanceRemoval'], manifest['mempoolReinit'], manifest['advertisingObserver']]
targets = {}
initial = {}
for spec in specs:
    patch = root / 'patches' / spec['patch']
    if sha(patch) != spec['sha256']:
        raise SystemExit('ESP-IDF patch checksum mismatch')
    for item in spec['files']:
        if not (source / item['path']).resolve().is_relative_to(source):
            raise SystemExit('ESP-IDF patch target leaves derived source')
        initial.setdefault(item['path'], item['before'])
        targets.setdefault(item['path'], set()).update([item['before'], item['after']])
verify_idf(root, source, manifest, targets)
# 先识别完整补丁前缀，重叠目标也必须吻合；检查全部输入后才允许修改。
states = [initial]
for spec in specs:
    state = states[-1].copy()
    for item in spec['files']:
        if state[item['path']] != item['before']:
            raise SystemExit('ESP-IDF patch chain input mismatch')
        state[item['path']] = item['after']
    states.append(state)
actual = {path: sha(source / path) for path in initial}
try:
    applied = states.index(actual)
except ValueError:
    raise SystemExit('ESP-IDF patch source differs from pinned input/output')
for index in range(applied, len(specs)):
    patch = root / 'patches' / specs[index]['patch']
    subprocess.run(['patch', '--batch', '--forward', '-p1', '-i', str(patch)], cwd=source, check=True)
    if {path: sha(source / path) for path in initial} != states[index + 1]:
        raise SystemExit('ESP-IDF patched output checksum mismatch')
print('ESP-IDF local patches verified (7 observer posts + instance retirement + mempool reinit + generation-bound advertising observer; legacy unchanged)')
