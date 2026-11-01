# C3 工具链探针

这是本项目首次交叉构建记录，仅用于证明编译和链接环境可用，不代表产品固件或 MicroTS 已通过验证。

## 环境与命令

- 主机：macOS arm64；Python 3.14.5。
- ESP-IDF：v6.0.3，提交 `76f5dedd9950a3012fee8fb7d5586df21fc67802`。
- RISC-V GCC：`esp-15.2.0_20251204`；CMake 4.0.3；Ninja 1.12.1。
- 源码与工具从本机已有缓存复制到本项目 `.tools/`；在本项目新建 Python 环境，未修改参考项目。
- Python 环境安装：`IDF_TOOLS_PATH="$PWD/.tools/idf-tools" python3 .tools/esp-idf/tools/idf_tools.py install-python-env`。
- 在项目根目录实际运行：`sh .build/probe.sh build size`，退出码 0。

`.build/probe.sh` 与探针源码属于临时验证产物，已忽略，尚不是可复现的正式安装/构建入口。脚本设置 `IDF_PATH`、`IDF_TOOLS_PATH`、`IDF_PYTHON_ENV_PATH`、工具 PATH、`IDF_TARGET=esp32c3` 和 `ESP_IDF_VERSION=6.0.3`。缺少最后一项时，当前 component manager 在 `Version.coerce(os.getenv('ESP_IDF_VERSION'))` 报 `TypeError: expected string or bytes-like object, got 'NoneType'`；补齐后构建通过。

## 产物与静态内存

产物：`.build/c3-probe/build/balabala_c3_probe.bin`、同目录 `.elf` 和 `.map`。日志：`.build/c3-probe-build.log`、`.build/c3-probe-size.log`。

探针使用 `static volatile uint8_t framebuffer[480 * 800 / 8];`，保留 48,000 字节单色缓冲，防止编译器仅保留访问的首字节。

| 项目 | 字节 |
| --- | ---: |
| DRAM 使用 | 97,456 |
| .bss | 51,984 |
| DRAM 链接区域剩余 | 223,840 |
| Flash Code | 64,576 |
| Flash Data | 31,520 |
| 总镜像大小（未计 bin 填充） | 141,368 |

这些数字仅属于空应用与一个帧缓冲。剩余链接空间不是运行时可用 heap，更不是加入 MicroTS、字体、SD、BLE 和 Wi-Fi 后的内存余量。

探针暂用 4MB Flash 与默认分区用于链接；它们未经过设备核验，不能作为 X4 烧录配置。未执行任何烧录或真机测试。构建中出现上游 Kconfig 的 bool 默认值与 hex 写法提示，未导致失败。
