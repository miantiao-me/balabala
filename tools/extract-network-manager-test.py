#!/usr/bin/env python3
"""从锁定派生源码提取原函数及原context声明；不改写被测逻辑。"""
import hashlib
import json
from pathlib import Path
import re
import sys

real_events = '--event-loop' in sys.argv

root = Path(__file__).resolve().parent.parent
source = root / '.tools/components/network_provisioning/src/manager.c'
data = source.read_bytes()
lock = json.loads((root / 'tools/components.lock.json').read_text())
expected = lock['network_provisioning']['patches'][0]['files'][0]['after']
assert hashlib.sha256(data).hexdigest() == expected, 'patched manager SHA256 mismatch'
text = data.decode()
functions = ['free_provisioning_data', 'stop_service_timers', 'network_prov_mgr_get_cleanup_error', 'unregister_service_instance', 'begin_cleanup_session', 'keep_transport_on_stop', 'free_sec_params',
             'execute_event_cb', 'prov_stop_and_notify', 'network_prov_mgr_stop_service',
             'stop_prov_timer_cb', 'cleanup_delay_timer_cb', 'network_prov_mgr_event_handler_internal',
             'network_prov_mgr_init', 'network_prov_mgr_deinit', 'network_prov_mgr_done',
             'network_prov_mgr_stop_provisioning', 'network_prov_mgr_get_wifi_state', 'wifi_connect_timer_cb',
             'network_prov_mgr_start_provisioning']
if real_events:
    functions.append('network_prov_mgr_start_service')
parts = [text[text.index('typedef enum {'):text.index('/* Mutex to lock/unlock')]]
parts += ['static SemaphoreHandle_t prov_ctx_lock;\nstatic struct network_prov_mgr_ctx *prov_ctx;\n'
          'static bool s_deinitializing;\nstatic uint32_t s_cleanup_token;\n'
          'static network_prov_mode_t s_mgr_mode = NETWORK_PROV_MODE_FULL;\n']
# 原函数相互调用的声明只供C编译器使用。
parts += ['static void wifi_connect_timer_cb(void *);\n'
          'static void network_prov_mgr_event_handler_internal(void *, esp_event_base_t, int32_t, void *);\n'
          'void network_prov_mgr_stop_provisioning(void);\nesp_err_t network_prov_mgr_deinit(void);\n'
          'static void unregister_service_instance(esp_event_base_t, int32_t, esp_event_handler_instance_t *);\n'
          'static esp_err_t network_prov_mgr_start_service(const char *, const char *);\n']
for name in functions:
    pattern = r'^(?:static )?(?:void|bool|esp_err_t) ' + name + r'\([^;{}]*\)\n\{'
    match = re.search(pattern, text, re.M)
    assert match, name
    begin = match.start()
    brace = text.index('{', match.start())
    depth = 1
    end = brace + 1
    while depth:
        if text[end] == '{': depth += 1
        if text[end] == '}': depth -= 1
        end += 1
    parts.append(f'\n#line {text[:begin].count(chr(10))+1} "{source}"\n' + text[begin:end] + '\n')
output = root / ('.build/manager-event-loop-under-test.inc' if real_events else '.build/manager-stop-under-test.inc')
# 同步终态桥同样逐字来自生产适配层，不另写行为模型。
bridge_source = root / 'firmware/main/balabala_network_idf.c'
bridge = bridge_source.read_text()
start = bridge.index('static void terminal(')
end = bridge.index('\nstatic ', start + 1)
parts.append(f'\n#line {bridge[:start].count(chr(10))+1} "{bridge_source}"\n' + bridge[start:end])
output.write_text('\n'.join(parts))
print(f'manager real functions: {len(functions)}; patched SHA256 {expected}')
