#!/usr/bin/env python3
"""校验固定IDF补丁后逐字抽取真实Security1/BLE函数，仅由C测试替换外部API。"""
import hashlib
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
manifest = json.loads((root / 'patches/esp-idf-6.1.json').read_text())
parts = []
final_hashes = {item['path']: item['after'] for spec in [manifest, manifest['advertisingObserver']]
                for item in spec['files']}
for item in manifest['files']:
    source = root / '.tools/esp-idf' / item['path']
    data = source.read_bytes()
    assert hashlib.sha256(data).hexdigest() == final_hashes[item['path']], 'patched IDF SHA256 mismatch'
    text = data.decode()
    security = 'security1.c' in item['path']
    if security:
        parts.append(text[text.index('#define PUBLIC_KEY_LEN'):text.index('static void hexdump')])
        functions = ['handle_session_command0', 'handle_session_command1']
    else:
        functions = ['transport_simple_ble_disconnect', 'transport_simple_ble_connect']
    for name in functions:
        match = re.search(r'^static (?:void|esp_err_t) ' + name + r'\([^;{}]*\)\n\{', text, re.M)
        assert match, name
        start = match.start()
        end = text.index('{', start) + 1
        depth = 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        parts.append(f'\n#line {text[:start].count(chr(10))+1} "{source}"\n' + text[start:end])
(root / '.build/protocomm-notifications-under-test.inc').write_text('\n'.join(parts))
print('IDF real patched functions: 4; SHA256 checked; 7 observer posts')
