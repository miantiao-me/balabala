#!/usr/bin/env python3
"""校验完整事件源码与原始公共头，生成被忽略的RTOS include转接头。"""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
idf = root / '.tools/esp-idf'
manifest = json.loads((root / 'patches/esp-idf-6.1.json').read_text())
assert subprocess.check_output(['git', '-C', str(idf), 'rev-parse', 'HEAD'], text=True).strip() == manifest['commit']
expected = {item['path']: item['after'] for item in manifest['instanceRemoval']['files']}
for path in ['components/esp_event/default_event_loop.c', 'components/esp_event/include/esp_event.h',
             'components/esp_event/include/esp_event_base.h', 'components/esp_event/private_include/esp_event_private.h',
             'components/protocomm/src/common/protocomm.c', 'components/protocomm/src/common/protocomm_priv.h',
             'components/protocomm/include/common/protocomm.h', 'components/protocomm/include/security/protocomm_security.h']:
    original = subprocess.check_output(['git', '-C', str(idf), 'show', manifest['commit'] + ':' + path])
    expected[path] = hashlib.sha256(original).hexdigest()
for path, digest in expected.items():
    assert hashlib.sha256((idf / path).read_bytes()).hexdigest() == digest, path
out = root / '.build/mock-event'
for path in ['freertos/FreeRTOS.h', 'freertos/task.h', 'freertos/semphr.h', 'freertos/queue.h',
             'esp_err.h', 'esp_log.h', 'esp_timer.h', 'esp_task.h', 'sdkconfig.h', 'esp_attr.h', 'esp_heap_caps.h']:
    target = out / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('#include "mock-event-rtos.h"\n')
(root / '.build/ble-instance-test-sources.json').write_text(json.dumps(expected, indent=2) + '\n')
print('Full esp_event.c/default_event_loop.c + real internal/public headers: 6 SHA256 verified; profiling=1; protocomm.c + 3 headers SHA256 verified')
