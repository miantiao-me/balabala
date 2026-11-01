"""Verify both C/Rust diagnostic gating and immutable board/image budgets."""
import importlib.util
import pathlib
import shutil
import subprocess
import sys

build, config, enabled, phase = sys.argv[1:]
build = pathlib.Path(build)
config = pathlib.Path(config).read_text()
expected = [
    'CONFIG_ESPTOOLPY_FLASHMODE_DIO=y',
    'CONFIG_ESPTOOLPY_FLASHFREQ_40M=y',
    'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y',
    'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
    'CONFIG_ESP_MAIN_TASK_STACK_SIZE=65536',
    'CONFIG_PM_ENABLE=y',
    '# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set',
    '# CONFIG_ESP_WIFI_IRAM_OPT is not set',
    '# CONFIG_ESP32_WIFI_RX_IRAM_OPT is not set',
    '# CONFIG_ESP32_WIFI_IRAM_OPT is not set',
    'CONFIG_BT_ENABLED=y',
    'CONFIG_BT_NIMBLE_ENABLED=y',
    'CONFIG_BT_NIMBLE_MEMPOOL_RUNTIME_ALLOC=y',
    '# CONFIG_BT_NIMBLE_MEMPOOL_BLOCK_REUSED is not set',
    'CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC=y',
    'CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1',
    'CONFIG_NIMBLE_MAX_CONNECTIONS=1',
    'CONFIG_BT_CTRL_BLE_MAX_ACT=2',
    'CONFIG_BT_CTRL_BLE_MAX_ACT_EFF=2',
    'CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6',
    'CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM=6',
    'CONFIG_ESP_WIFI_RX_BA_WIN=6',
    'CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT=24',
    'CONFIG_BT_NIMBLE_TRANSPORT_ACL_SIZE=255',
    'CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE=70',
    'CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT=30',
    'CONFIG_BT_NIMBLE_TRANSPORT_EVT_DISCARD_COUNT=8',
    'CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=12',
    'CONFIG_BT_NIMBLE_MSYS_1_BLOCK_SIZE=256',
    'CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT=24',
    'CONFIG_BT_NIMBLE_MSYS_2_BLOCK_SIZE=320',
    'CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256',
    'CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES=64',
    'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1=y',
    '# CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_0 is not set',
    '# CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2 is not set',
    'CONFIG_PARTITION_TABLE_CUSTOM=y',
    'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"',
]
for line in expected:
    assert line in config.splitlines(), line
assert ('CONFIG_BALABALA_AUTOTEST=y' in config.splitlines()) == (enabled == 'ON')
if enabled == 'ON':
    assert f'CONFIG_BALABALA_AUTOTEST_PHASE={phase}' in config.splitlines()
# 校验实际生成的二进制分区表，不能只依赖CSV或镜像大小检查。
root = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location(
    'gen_esp32part', root / '.tools/esp-idf/components/partition_table/gen_esp32part.py')
partitions = importlib.util.module_from_spec(spec)
spec.loader.exec_module(partitions)
table = partitions.PartitionTable.from_binary((build / 'partition_table/partition-table.bin').read_bytes())
table.verify()
table.verify_size_fits(0x1000000)
geometry = [(p.name, p.type, p.subtype, p.offset, p.size) for p in table]
assert geometry == [
    ('nvs', 1, 2, 0x9000, 0x6000),
    ('phy_init', 1, 1, 0xF000, 0x1000),
    ('factory', 0, 0, 0x10000, 0xFF0000),
], geometry
assert table.flash_size() == 0x1000000, table.flash_size()
for name in ['balabala_microts_probe.bin', 'bootloader/bootloader.bin']:
    data = (build / name).read_bytes()
    assert data[0] == 0xE9 and data[2] == 2 and data[3] == 0x40, (name, data[:4].hex())
    if name.endswith('probe.bin'):
        assert len(data) <= table.find_by_name('factory').size, len(data)
        assert (b'autotest phase=' in data) == (enabled == 'ON')
nm = shutil.which('riscv32-esp-elf-nm')
assert nm, 'missing C3 nm'
symbols = subprocess.check_output([nm, str(build / 'balabala_microts_probe.elf')], text=True)
assert ('balabala_app_autotest_snapshot' in symbols) == (enabled == 'ON')
for symbol in ['esp_pm_configure', 'balabala_runtime_activity']:
    assert any(line.split()[-1:] == [symbol] for line in symbols.splitlines()), symbol
for symbol in ['balabala_autotest_tick', 'balabala_autotest_start', 'balabala_autotest_input_idle']:
    assert (symbol in symbols) == (enabled == 'ON'), symbol
if enabled == 'OFF':
    assert 'balabala_autotest_' not in symbols
for symbol in ['balabala_network_observe', 'balabala_autotest_before_display',
               'balabala_alloc_diag_failed', 'balabala_alloc_diag_checkpoint',
               'balabala_alloc_diag_observe', 'balabala_alloc_diag_blocked',
               'heap_caps_register_failed_alloc_callback']:
    assert (symbol in symbols) == (enabled == 'ON' and phase == '2'), symbol
if not (enabled == 'ON' and phase == '2'):
    assert 'balabala_alloc_diag_' not in symbols
    assert b'autotest alloc_' not in (build / 'balabala_microts_probe.bin').read_bytes()
if enabled == 'ON':
    objdump = shutil.which('riscv32-esp-elf-objdump')
    assert objdump, 'missing C3 objdump'
    table = subprocess.check_output([objdump, '-t', str(build / 'balabala_microts_probe.elf')], text=True)
    marker = [line for line in table.splitlines() if line.split()[-1:] == ['autotest_rtc']]
    assert len(marker) == 1 and '.rtc_noinit' in marker[0] and '00000028' in marker[0], marker
print(f'build contract passed: autotest={enabled} DIO/40MHz/16MB stack=65536 PM=on Wi-Fi IRAM/RX_IRAM=off runtime-mempool=on/reuse=off connections=1 activities=2 static-RX=6 ACL/MSYS/MTU/prepare/Security1 preserved factory=0x10000/0xff0000 end=0x1000000 BIN<=0xff0000')
