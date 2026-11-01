#!/usr/bin/env python3
"""只在项目.tools固定派生源码中幂等应用已校验的本地补丁链。"""
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
from source_integrity import sha, verify_pocket

root = Path(__file__).resolve().parent.parent
source = root / '.tools/pocketjs'
manifest = json.loads((root / 'patches/manifest.json').read_text())
if source.resolve() != source or (source / '.balabala-version').read_text().strip() != manifest['commit']:
    raise SystemExit('PocketJS derived source path/version mismatch')
patches = manifest['patches']
initial = {}
for entry in patches:
    patch = root / 'patches' / entry['patch']
    if sha(patch) != entry['sha256']:
        raise SystemExit('PocketJS patch checksum mismatch')
    for item in entry['files']:
        if not (source / item['path']).resolve().is_relative_to(source):
            raise SystemExit('PocketJS patch target leaves derived source')
        initial.setdefault(item['path'], item['before'])
# 仅接受完整原始、完整已应用前缀或完整最终状态；任何混合/漂移均失败。
states = [initial]
for entry in patches:
    state = states[-1].copy()
    for item in entry['files']:
        if state[item['path']] != item['before']:
            raise SystemExit('PocketJS patch chain input mismatch')
        state[item['path']] = item['after']
    states.append(state)
actual = {path: sha(source / path) for path in initial}
try:
    applied = states.index(actual)
except ValueError:
    raise SystemExit('PocketJS patch source differs from pinned input')
verify_pocket(root, source, manifest, states[applied])
for index in range(applied, len(patches)):
    patch = root / 'patches' / patches[index]['patch']
    subprocess.run(['patch', '--batch', '--forward', '-p1', '-i', str(patch)], cwd=source, check=True)
    if {path: sha(source / path) for path in initial} != states[index + 1]:
        raise SystemExit('PocketJS patched output checksum mismatch')
print(f'PocketJS local patches verified ({len(patches)} applied)')
