# MicroTS / ESP32-C3 应用验证

本记录提供 strict AOT 学习页面、宿主行为测试和 C3 固件链接的复现证据。应用与平台契约见 [学习应用](learning-app.md)，产品范围见 [验收表](acceptance.md)，早期工具链探索见 [工具链记录](toolchain-probe.md)。当前没有真机运行结论。

## 构建链与固定版本

```text
content/*.json + native/App.vue + native/src/*.rs
  → MicroTS strict AOT + 两种字号所需字形烘焙
  → .build/generated/ Rust / 样式 / 只读字体档案
  → Rust no_std staticlib (riscv32imc-unknown-none-elf)
  → ESP-IDF C ABI → 48000-byte 1bit framebuffer
```

固定版本集中在 [tools/versions.sh](../tools/versions.sh)：

| 依赖 | 固定版本 |
| --- | --- |
| PocketJS | `a9f186e2aa385fb353b05a12c1c8c592b76bec80`，来源 `pocket-nexus/pocketjs` |
| ESP-IDF | `v6.0.3` / `76f5dedd9950a3012fee8fb7d5586df21fc67802` |
| Rust | `1.99.1`，独立 `.tools/rustup` |
| Bun | `1.3.14` |
| Noto Sans CJK SC Regular | `notofonts/noto-cjk` 提交 `f8d157532fbfaeda587e826d4cd5b21a49186f7c` |

PocketJS tar SHA256 为 `056a708bf3cf6096760ce7c2ba893c96e3c0f9530f9bcd0b3d25acc91b508344`，Noto OTF SHA256 为 `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`。Bun 来自固定 release ZIP，JS 依赖使用上游 frozen lockfile。Cargo.lock 从固定上游 lock 裁剪并补入应用根包，SHA256 为 `ade1e30a1053302783136a0a6c8667ba207df3c05c950d74e12ed3c78125008d`；编译使用 `--locked`。serde `1.0.228` / serde_json `1.0.150` 均已锁定，许可见 [第三方归属](../licenses/README.md)。

[清单](../native/Cargo.toml) 复制到 `.build/native/` 使用，相对路径以该目录为基准。依赖、字体源和全部生成文件保存在忽略目录。脚本支持 macOS arm64 / Linux arm64、x64 的 Bun 下载，本轮只在 macOS arm64 / Python 3.14.5 实际执行，未在完全空缓存或 Linux 上验证安装。

## 运行时与字体

C3 的 `riscv32imc-unknown-none-elf` 不提供硬件 CAS。仅交叉目标设置 `--cfg portable_atomic_unsafe_assume_single_core`，由 portable-atomic 保存/屏蔽/恢复 `mstatus.MIE`；宿主没有该配置。没有引入 Xtensa、PSRAM 或双核假设。

Rust 分配通过 IDF `heap_caps_aligned_alloc` / `heap_caps_free`，使用内部 8bit heap。OOM 记录请求后 abort；panic 使用栈上消息缓冲记录并 abort，不跨 ABI unwind。PocketJS 上游补丁清单为空，使用公开 `bakeSlot`、`load_font_atlas` 与窗口渲染接口。

中文字体来自固定 Noto OTF，组合 U+0308 缺字由固定 PocketJS 中的 Inter 补齐，构建逐字检查 glyph 覆盖。构建扫描全部原生运行时 Rust 源中的文案，包括 C ABI 的恢复失败提示，扫描清单为 `.build/generated/ui-font-sources.json`。24px 档案有 563 glyph（477440 字节），48px 档案有 260 glyph（875696 字节），共 1353136 字节只读 Flash。档案不整体加载到 heap；每页仅收集所需字符，生成小子集，交给上游 Atlas 复制，立即释放临时子集；页绘制结束后释放 UI 和 Atlas。

`.build/font-memory.txt` 同时报告最终数据载荷与构造峰值。19 项测试保留 414 个实际视图渲染，以线程局部宿主分配计量器核对字体公式；新增 22000 个学习状态及 4 个卡组页的预算遍历，再实际渲染字体预算最大页：

| 项目 | 字节 |
| --- | ---: |
| 示例页面 Atlas 数据载荷峰值（含子集与复制重叠） | 72960 |
| 414 视图字体构造请求峰值 | 82110 |
| 补全轮长/游标的内置字体上限，且最大页实测相等 | 83807 |
| 414 视图 mount/draw/render 请求峰值 | 110684 |
| 字体预算最大页 mount/draw/render 请求峰值 | 102729 |
| 审查反例的卡组预算校验请求峰值（无 Atlas） | 1764 |
| 四科活动内容 C3 容量计费上限 | 32768 |
| 内置 80 卡活动内容 C3 容量计费 | 18957 |
| 静态 1bit framebuffer | 48000 |
| 静态 480×8 RGB565 stripe | 7680 |

字体构造峰值包含拼接正文 String、字符 Vec 实际容量、序列化临时子集、已驻留 Atlas 及新 Atlas cmap/bitmap 复制。计量器对 realloc 保守地同时计入旧新请求。完整渲染峰值含 mount/draw 的其他请求，使用调用前已构造的 View；不含模型、输入包、帧缓冲或条带。

导入前复用字体加载的尺寸算法，将完整页面各字号的临时子集与 Atlas 副本重叠计入，以 83807 字节为导入上限；全部内置卡片通过。审查中的 128glyph 反例预算 218180 字节，被拒绝且原卡组/进度保留；边界样本 80282 接受、90481 拒绝。另限制替换后四科活动内容总计费为 32KiB，计费含 Vec 分配槽及所有字符串 capacity，规则见 [学习应用](learning-app.md)。

这些值来自 64 位宿主的分配请求或明确的 C3 静态容量计费，不包含分配器元数据/对齐，不能视作 C3 可用 heap 或 BLE/Wi-Fi 共存保证。链接区域剩余字节还要承担 FreeRTOS、64KiB 主任务栈、进度/轮次和 UI、导入输入缓冲及旧/新内容重叠、SD/保存缓冲及无线任务。当前尚未分配/测定平台服务预留；实机必须测峰值与最大连续空闲块。

## ABI 与条带

[头文件](../firmware/main/balabala_probe.h) 定义 12 个 `balabala_app_*` 接口：对象创建/销毁、输入、状态、渲染、显示完成、卡组导入、双槽加载/导出/确认、平台请求和配网状态。最终 ELF 用强制保留链接选项保留全部接口，符号证据在 `.build/app-abi-symbols.txt`。兼容旧探针的 `balabala_probe_render()` 当前渲染桌面。

渲染以 480×8 RGB565 窗口分 100 条带，转换为行优先、MSB 在左、1 白/0 黑的 480×800 1bit 缓冲；RGB 展开后以 `299R + 587G + 114B >= 128000` 判白。固件没有整屏 RGB565 缓冲。宿主仅为 96×128 独立参考测试分配完整 RGB565。

外部参考 [community-sdk EInkDisplay.h](https://github.com/open-x4-epaper/community-sdk/blob/7d86603ad27709a9a766bb5ad893cfc39e60777e/libs/display/EInkDisplay/include/EInkDisplay.h) 的固定提交默认原生 800×480、每行 100 字节；其 52272 字节最大缓冲兼容 X3，不代表 X4 需求。后续板驱动须将逻辑竖屏旋转/转置，方向待实机核验，不能直接发送现有 portrait bytes。

## 实际验证

本轮 Review 修复后，以下命令在项目根目录实际执行，均退出 0：

```sh
sh tools/build.sh test -- --nocapture
sh tools/build.sh build
```

前一阶段另已执行 `python3 tools/test-build-env.py` 通过；本轮未改环境脚本，未重复运行该安装检查。环境测试实际重跑 `sh tools/setup.sh`，使用现有工具缓存。设置外部 dummy `IDF_PYTHON_ENV_PATH` 后，包装器核验每次 idf_tools.py 调用前该变量已清除；实际安装/export 指向项目环境，外部文件 SHA256 与 mtime 均不变。注入 export 退出 73 时，构建在生成 AOT 之前失败，已有 `aot.json` 内容及 mtime 不变，报 `ESP-IDF environment export failed`。最小 PATH 缺 shasum 时 setup/build 都报 `Missing prerequisite: shasum`。

19 项宿主测试通过，覆盖翻面/跳过无评分、四选一和单次评分、回看、反馈主动左右/确认/返回、旧键释放与显示 revision 门禁、繁忙导航合并、Home/Shutdown、5/10/重置/复习、导入原子性/稳定 ID/缺字/超限、双槽所有截断点/坏槽/确认事务、C ABI 及渲染等价。本轮新增真实 C ABI 双坏槽错误页 render → display_complete → 正面退出、页面字体预算边界/反例、常驻增长拒绝，以及重排后重启的 ID/结果恢复。

测试生成 `.build/pages/` 下 30 个 PBM，包括原 28 页，以及 `save-error.pbm`、`font-budget-maximum.pbm`。414 视图仍检查实际字宽和固定分隔线；22004 个内置内容显示组合只计算预算，其中最大页另经实际渲染核对。可复查 `.build/review-host-test.log`、`.build/font-memory.txt`、`.build/generated/assets.json`、`.build/review-fixes-evidence.json`。

最终 C3 构建与分区检查通过，日志 `.build/review-c3-build.log`，产物 `.build/firmware/balabala_microts_probe.{elf,map,bin}`：

| 最终链接报告 | 字节 |
| --- | ---: |
| DRAM 使用 | 99336 |
| .bss | 59648 |
| .data | 6106 |
| DRAM 中代码 | 33582 |
| DRAM 链接区域剩余 | 221960 |
| Flash Code | 468344 |
| Flash Data | 1442616 |
| 总镜像（未计 bin 填充） | 1950448 |
| 实际 .bin | 1975744 |
| 3MiB factory 剩余 | 1169984 |

默认 1MiB factory 曾因镜像超限失败。经批准新增 [交叉构建分区](../firmware/partitions.csv)，factory 从 0x10000 起占 3MiB；4MB Flash 仍是未核验假设，该布局不是烧录指令。

## 栈证据

advisor 从早期 ELF 发现 `balabala_probe_render` 固定栈帧 26592 字节，超过当时 16384 字节主任务栈。已将 [默认配置](../firmware/sdkconfig.defaults) 和实际 `.build/sdkconfig` 都改为 65536 字节，并重新构建最终产品 ELF。

最终反汇编与报告命令：

```sh
root=$PWD
. "$root/tools/env.sh"
riscv32-esp-elf-objdump -d -C .build/firmware/balabala_microts_probe.elf > .build/app-disassembly.txt
python3 tools/report-stack.py .build/app-disassembly.txt > .build/app-stack.txt
```

命令需先加载 `tools/env.sh`。报告从最终机器码提取固定栈帧并追踪可解析直接调用/尾调用。产品 create 入口 208 字节，最长已解析调用链求和 2352 字节；render 入口 176 字节，内部产品 render 固定帧 28080 字节，含 draw/sort 的已解析链求和 34960 字节，加 app_main 的 64 字节为 35024 字节。64KiB 比该链有 30512 字节余量。本轮另用同一解析脚本在忽略目录生成含 `balabala_app_import_deck` 入口的报告 `.build/review-import-stack.txt`：入口固定帧 16 字节、已解析链求和 3104 字节，存在循环/间接调用，仍不是上界。

脚本标注了循环及间接调用；上面的直接链求和不是栈上界证明，不包含未解析间接调用、递归次数、运行时中断或实测高水位。64KiB 只是有余量的起点，不能据此声明栈安全。真机须监测栈高水位并测 C3 heap，必要时将大型 UI 临时值移到 heap 并重新预算。

## 未验证范围

SSD1677 显示、ADC 输入、电源长短按电气行为、SD 落盘与掉电、BLE/iOS 官方客户端、安全配网、无线共存、功耗和屏幕残影均未验证。中文覆盖和 PBM 可视化是宿主结果，不代表真机显示质量；组合符号完整整形也未承诺。

本轮没有执行烧录、擦除、解锁、发布或修改外部参考仓库，没有修改 AGENTS.md。当前目录没有 Git 元数据，未初始化、暂存或提交文件；所有工具生成内容均在忽略目录。最终独立 Review 由主 Agent 安排。
