#!/usr/bin/env python3
"""Normalize cached SDK configuration for ordinary and diagnostic C3 builds."""
import pathlib,sys
source,dest,enabled,phase=sys.argv[1:]
p=pathlib.Path(source)
s=p.read_text() if p.exists() else pathlib.Path('firmware/sdkconfig.defaults').read_text()
s='\n'.join(line for line in s.splitlines() if 'CONFIG_BALABALA_AUTOTEST' not in line)+'\n'
# defaults不会覆盖已有4MB缓存，容量选择及派生字符串必须一起重置。
s='\n'.join(line for line in s.splitlines()
            if not line.startswith(('CONFIG_ESPTOOLPY_FLASHSIZE=',
                                    'CONFIG_ESPTOOLPY_FLASHSIZE_',
                                    '# CONFIG_ESPTOOLPY_FLASHSIZE_')))+'\n'
s+='CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y\n'
# 移除候选配置及IDF旧别名，防止缓存经重命名覆盖新值。
for key in ('CONFIG_PM_ENABLE',
            'CONFIG_ESP_WIFI_RX_IRAM_OPT', 'CONFIG_ESP_WIFI_IRAM_OPT',
            'CONFIG_ESP32_WIFI_RX_IRAM_OPT', 'CONFIG_ESP32_WIFI_IRAM_OPT',
            'CONFIG_BT_NIMBLE_MEMPOOL_RUNTIME_ALLOC', 'CONFIG_BT_NIMBLE_MEMPOOL_BLOCK_REUSED',
            'CONFIG_BT_NIMBLE_MAX_CONNECTIONS', 'CONFIG_NIMBLE_MAX_CONNECTIONS',
            'CONFIG_BT_CTRL_BLE_MAX_ACT', 'CONFIG_BT_CTRL_BLE_MAX_ACT_EFF',
            'CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM', 'CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM'):
    s='\n'.join(line for line in s.splitlines()
                if not line.startswith((key+'=', '# '+key+' ')))+'\n'
s+='CONFIG_PM_ENABLE=y\n'
s+='# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set\n# CONFIG_ESP_WIFI_IRAM_OPT is not set\n'
s+='CONFIG_BT_NIMBLE_MEMPOOL_RUNTIME_ALLOC=y\n# CONFIG_BT_NIMBLE_MEMPOOL_BLOCK_REUSED is not set\n'
s+='CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1\nCONFIG_BT_CTRL_BLE_MAX_ACT=2\nCONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6\n'
s+=(f'CONFIG_BALABALA_AUTOTEST=y\nCONFIG_BALABALA_AUTOTEST_PHASE={phase}\n' if enabled=='ON' else '# CONFIG_BALABALA_AUTOTEST is not set\n')
pathlib.Path(dest).write_text(s)
