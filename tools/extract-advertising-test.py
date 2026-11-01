#!/usr/bin/env python3
"""从固定摘要的真实 NimBLE transport 抽取广告路径，外部 BLE API 由宿主替身注入。"""
import hashlib
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
spec = json.loads((root / 'patches/esp-idf-6.1.json').read_text())['advertisingObserver']
item = next(item for item in spec['files'] if item['path'].endswith('protocomm_nimble.c'))
source = root / '.tools/esp-idf' / item['path']
data = source.read_bytes()
assert hashlib.sha256(data).hexdigest() == item['after'], 'patched advertising SHA256 mismatch'
text = data.decode()
parts = []
for name in ['protocomm_ble_set_advertising_callback', 'advertising_result',
             'simple_ble_advertise', 'simple_ble_on_sync']:
    match = re.search(r'^(?:static\s+)?(?:void|esp_err_t)\s+' + name + r'\([^;{}]*\)\n\{', text, re.M)
    assert match, name
    start = match.start()
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    parts.append(f'\n#line {text[:start].count(chr(10))+1} "{source}"\n' + text[start:end])
# 会话在 host 启动前捕获回调；测试实际setter禁止活动期覆盖，不以全局next值派发。
for field in ['advertising_cb', 'advertising_context']:
    assignment = f'ble_config->{field} = next_{field};'
    assert text.count(assignment) == 1
    assert text.index(assignment) < text.index('simple_ble_start(ble_config)')
(root / '.build/advertising-under-test.inc').write_text('\n'.join(parts))
print('IDF real advertising functions: 4; patched SHA256 and pre-host capture checked')
