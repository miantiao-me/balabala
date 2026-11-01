# 第三方归属

本应用使用以下上游组件；生成字体、Rust 源码和产物保存在忽略目录，分发相应产物时需附带适用的许可文本。

- **PocketJS / MicroTS**：Copyright (c) 2026 Yifeng "Evan" Wang，固定提交 `8bc0525776efe779ef9592e0b91574cfbc91f60b`，来源 <https://github.com/pocket-nexus/pocketjs>。本探针使用 MIT 的 MicroTS compiler、core、raster 及其依赖模块；未链接 Pocket3D。许可全文见 [LICENSE-pocketjs.txt](LICENSE-pocketjs.txt)。维护的[本地补丁](../patches/README.md)包含依赖升级补丁（保留 TypeScript 6.0.3 兼容锁）、最终 Atlas 直接构造入口、初始布局容量提示/只读投影计数以及 mono 单色字库支持；五项补丁顺序、目标文件应用前后摘要及幂等脚本固定在项目内，仅应用于 `.tools/pocketjs` 派生副本，仍保留原 MIT 归属。
- **Noto Sans CJK SC Regular**：Adobe / Google，来源 <https://github.com/notofonts/noto-cjk>，固定提交 `f8d157532fbfaeda587e826d4cd5b21a49186f7c` 的 `Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`，SHA256 `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`。保留24px / 48px旧模板字档、原导入宽度度量与刷机最终提示；字体源仅在 `.tools/fonts/`。SIL OFL 1.1 及原始归属见 [OFL-NotoSansCJK.txt](OFL-NotoSansCJK.txt)。
- **霞鹜新晰黑 / IPAex Gothic**：LXGW / Information-technology Promotion Agency, Japan（IPA），来源 <https://github.com/lxgw/LxgwNeoXiHei>，固定 `v1.308` / 提交 `f786a5c5c51c2339542be296598e25d279ce79ef`。`LXGWNeoXiHei.ttf` 的 SHA256 为 `80415d3666d9f51cf42ecf82c5350488e6b3f282ab46a3b90dcaa5e212564ecc`，用于24 / 32 / 48px界面字档与176 / 112px特大字。字体与派生字形数据适用[IPA Font License Agreement v1.0](IPA-Font-License-v1.0.txt)；固定源、转换源码、原版IPAex 004.01下载及替换方法见[字体说明](LxgwNeoXiHei.md)。
- **霞鹜文楷（历史候选）**：Copyright 2021–2026 LXGW、Copyright 2020 The Klee Project Authors，来源 <https://github.com/lxgw/LxgwWenKai>，固定版本 `v1.530` / 提交 `4becfb4d5cf0c92cfdb60a6e9b504bda43315325`。历史候选中`LXGWWenKai-Regular.ttf`的SHA256为`2d73eea94ad808999d90c0000c239a84a33b09d4bc6ab6c5b8bbf1eda8a7fc8b`，用于24 / 32px；`LXGWWenKai-Medium.ttf`的SHA256为`f4b8a34d1f838fe411540cb7c97b1c384ac9738a876c4104500bc42508b2312a`，用于48px与特大字。当前构建已改用新晰黑；保留历史产物适用的[SIL OFL 1.1及归属](OFL-LxgwWenKai.txt)。
- **resvg-js**：Copyright yisibl 及贡献者，`@resvg/resvg-js` 2.6.2，来源 <https://github.com/yisibl/resvg-js>，MPL-2.0。仅在构建期由 `tools/sprites.ts` 把线稿和字体轮廓栅格化为位图，不链接进固件；版本与 integrity 固定在 `tools/svg/bun.lock`，安装到忽略目录 `.tools/svg/`，许可全文随包保留在 `.tools/svg/node_modules/@resvg/resvg-js/LICENSE`。
- **课程词表与汉字读音**：NGSL 1.2（2023）的官方词目/补充原形表按 CC BY-SA 4.0 使用；Unihan 18.0.0 的读音属性按 Unicode License V3 使用。固定来源、摘要、筛选与编排变更、分发要求见[课程数据归属](curriculum.md)，适用全文见[CC BY-SA 4.0](CC-BY-SA-4.0.txt)和[Unicode License V3](Unicode-License-V3.txt)。课程主题词、中文释义、拼音与数学题为项目编排，未复制教材题目。
- **界面线稿**：`native/art/*.svg`（键帽图标、播放、锁、星星、答对鼓励图、休眠图）为本项目原创绘制，不来自第三方素材。
- **Inter**：Copyright (c) 2016 The Inter Project Authors，<https://github.com/rsms/inter>。字体来自上述 PocketJS 固定提交的 `assets/fonts/Inter-Regular.ttf`，补齐旧Noto缺失的组合U+0308，历史文楷候选也使用过该回退字体；未修改原字体；SIL OFL 1.1 全文见 [OFL-Inter.txt](OFL-Inter.txt)。
- **giligili**：Copyright 2026 The giligili Authors，<https://github.com/ccbikai/giligili>。参考快照提交 `2239ef10e7f62badefdea9eaac8fa8c8f5006cb7`；本探针的 AOT 入口、分配桥接/栈上 panic 缓冲、渲染等价测试参考其 `tools/build-native.ts`、`native/src/lib.rs`、`tools/test-native-render.rs`。实现已适配 C3、独立依赖下载、模型驱动页面与 1bit 条带输出，不依赖参考项目。Apache-2.0 全文见 [LICENSE-giligili.txt](LICENSE-giligili.txt)。
- **Open X4 E-Paper community-sdk**：Copyright (c) 2025 Open X4 E-Paper Contributors，固定提交 `7d86603ad27709a9a766bb5ad893cfc39e60777e`，来源 <https://github.com/open-x4-epaper/community-sdk>。输入参考 `libs/hardware/InputManager/src/InputManager.cpp` 与 `include/InputManager.h` 的 GPIO、12bit ADC 分段边界和低有效电源；`firmware/main/balabala_display.c` 的 SSD1677 X4 reset/init/寻址/全刷/单缓冲局刷/休眠序列参考 `libs/display/EInkDisplay/src/EInkDisplay.cpp`；SD CS12 与上游 40MHz 事实取自 `libs/hardware/SDCardManager/src/SDCardManager.cpp`。本实现使用 IDF SPI2/SDSPI、纯 C 状态机、100 B 行旋转和有界重试，未引入 Arduino 驱动、LUT 或完整上游代码，物理行为仍待实机确认。MIT 全文见 [LICENSE-community-sdk.txt](LICENSE-community-sdk.txt)。
- **X4Term 引脚参考**：<https://github.com/penk/X4Term>，固定提交 `bd2f028bd57aebb8d0580dfb9b608c869bb521a3` 的 `lib/hal/HalGPIO.h` 核验 SCLK8 / MOSI10 / MISO7 / EPD CS21 / DC4 / RST5 / BUSY6。其配套`lib/hal/HalGPIO.cpp`的`isUsbConnected()`与头文件`UART0_RXD=20`也用于核对USB供电高电平事实。固定树未发现许可证文件；仅使用接线事实，没有移植其代码，不能将其描述为 MIT 组件。
- **network_provisioning**：Espressif Systems，Apache-2.0，固定组件 1.4.0 / 来源提交 `c492b27913079a61f05fa738903f33709f925e6c`，来源 <https://github.com/espressif/idf-extra-components/tree/c492b27913079a61f05fa738903f33709f925e6c/network_provisioning>。完整 registry ZIP 的 SHA256 为 `6e81719e22a1485c3d617e8512a1eff356c333ab3ea0eae3222b2a88d9fd12ad`；原始许可见 [LICENSE-network_provisioning.txt](LICENSE-network_provisioning.txt)。采用官方 BLE / FULL / Security 1 管理器，维护[本地生命周期补丁](../patches/network-provisioning-1.4.0-stop-lifecycle.patch)，处理 STOPPING 锁外清理、重入 deinit、私有 STOP 重试/token、instance 订阅/回滚、失败启动 PoP 清理与同步 END；扩展公开 manager.h 的 cleanup_error 查询、停止错误保留所有权及显式清理契约，原 instance 算法保持；原包摘要及目标文件应用前后摘要随组件锁保留。项目 CMake 关闭其日志以防凭据输出。
- **json_generator**：Espressif Systems，Apache-2.0，固定组件 2.1.0 / 来源提交 `0e17e634b2236848fcbbd30bba0f8d4d357e5195`，来源 <https://github.com/espressif/idf-extra-components/tree/0e17e634b2236848fcbbd30bba0f8d4d357e5195/json_generator>。完整 registry ZIP SHA256 为 `a7d9ae8464f98d6c34b1ed46ba47baa6e05c4e05f99b9aa7003bedeef8f67ac2`；原始许可见 [LICENSE-json_generator.txt](LICENSE-json_generator.txt)。两组件 ZIP URL、摘要及来源固定在 [components.lock.json](../tools/components.lock.json)，下载/解压内容仅进入忽略目录 `.tools/`，保留原始 SPDX/版权声明，不修改外部仓库。
- **ESP-IDF**：Espressif Systems，<https://github.com/espressif/esp-idf>，`v6.1` / `fff9895c82d744c7237be8847347bdd1b07c6643`。主许可 Apache-2.0 全文见 [LICENSE-esp-idf.txt](LICENSE-esp-idf.txt)；各组件保留上游源码内的独立许可和版权声明。仅在本项目派生副本应用[观察通知补丁](../patches/esp-idf-6.1-observer-notifications.patch)，将 Security1 / NimBLE 七处观察通知改为不等待，并以[事件实例补丁](../patches/esp-idf-6.1-instance-removal.patch)在回调内逻辑失效、派发/profiling 尾按实例回收，保留 legacy 路径；以[mempool 重初始化补丁](../patches/esp-idf-6.1-mempool-reinit.patch)修复注册表项注销重入；以[广告观察补丁](../patches/esp-idf-6.1-advertising-observer.patch)发布精确广告状态。[补丁锁](../patches/esp-idf-6.1.json) 固定版本、补丁和目标文件前后摘要。

下载入口另外使用同一固定 ESP-IDF 的 [`components/soc/esp32c3/register/soc/rtc_cntl_reg.h`](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/soc/esp32c3/register/soc/rtc_cntl_reg.h)、[`components/efuse/esp32c3/esp_efuse_table.c`](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/efuse/esp32c3/esp_efuse_table.c) 和公共 `esp_restart()` 定义，沿用 ESP-IDF Apache-2.0 归属。强制 Joint Download 行为参考 Espressif [ESP32-C3 TRM v1.4 §7.2](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)；文档仅作寄存器行为事实来源，未复制图文或修改 eFuse。下载界面沿用原 Noto 字体，无新素材。

Rust 运行时与派生宏依赖版本来自固定 Cargo.lock，21 个 crates.io registry 包的原始许可与版权声明保留在以下文件：

| 包 | 版本 | 本项目采用的许可 | 文本 |
| --- | --- | --- | --- |
| byteorder | 1.5.0 | MIT | [byteorder](rust/byteorder-1.5.0.txt) |
| hash32 | 0.3.1 | MIT | [hash32](rust/hash32-0.3.1.txt) |
| heapless | 0.9.3 | MIT | [heapless](rust/heapless-0.9.3.txt) |
| itoa | 1.0.18 | MIT | [itoa](rust/itoa-1.0.18.txt) |
| libm | 0.2.16 | MIT 及文本内的来源声明 | [libm](rust/libm-0.2.16.txt) |
| memchr | 2.8.3 | MIT，保留 COPYING | [memchr](rust/memchr-2.8.3.txt) |
| portable-atomic | 1.15.0 | MIT | [portable-atomic](rust/portable-atomic-1.15.0.txt) |
| proc-macro2 | 1.0.107 | MIT | [proc-macro2](rust/proc-macro2-1.0.107.txt) |
| quote | 1.0.47 | MIT | [quote](rust/quote-1.0.47.txt) |
| ryu-js | 1.0.3 | BSL-1.0 | [ryu-js](rust/ryu-js-1.0.3.txt) |
| serde | 1.0.229 | MIT | [serde](rust/serde-1.0.229.txt) |
| serde_core | 1.0.229 | MIT | [serde_core](rust/serde_core-1.0.229.txt) |
| serde_derive | 1.0.229 | MIT | [serde_derive](rust/serde_derive-1.0.229.txt) |
| serde_json | 1.0.154 | MIT | [serde_json](rust/serde_json-1.0.154.txt) |
| slotmap | 1.1.1 | Zlib | [slotmap](rust/slotmap-1.1.1.txt) |
| stable_deref_trait | 1.2.1 | MIT | [stable_deref_trait](rust/stable_deref_trait-1.2.1.txt) |
| syn | 3.0.9 | MIT | [syn](rust/syn-3.0.9.txt) |
| taffy | 0.15.1 | MIT | [taffy](rust/taffy-0.15.1.txt) |
| unicode-ident | 1.0.27 | MIT，保留 Unicode-3.0 归属 | [unicode-ident](rust/unicode-ident-1.0.27.txt) |
| version_check | 0.9.5 | MIT | [version_check](rust/version_check-0.9.5.txt) |
| zmij | 1.0.23 | MIT | [zmij](rust/zmij-1.0.23.txt) |

Taffy 的 crates.io 源码包未附带独立 LICENSE 文件；这里保留其包内 `.cargo_vcs_info.json` 对应提交 `0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a` 的[上游许可](https://github.com/DioxusLabs/taffy/blob/0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a/LICENSE)（MIT）。构建工具及宿主依赖的许可保留在 `.tools/` 中的固定版本源码和安装包。

## C 平台与 ESP-IDF 链接组件

固件链接了以下 ESP-IDF（v6.1 / `fff9895c82d744c7237be8847347bdd1b07c6643`）内部组件，各组件保留其独立上游授权：

- **lwIP**：Copyright (c) 2001, 2002 Swedish Institute of Computer Science. BSD-3-Clause。文本来自 `.tools/esp-idf/components/lwip/lwip/COPYING`，见 [LICENSE-lwip.txt](LICENSE-lwip.txt)。
- **NimBLE**：Apache Mynewt NimBLE, Copyright 2015-2022 The Apache Software Foundation, Modifications Copyright 2017-2022 Espressif Systems (Shanghai) CO., LTD., Portions Copyright 2015 Runtime Inc. Apache-2.0。NOTICE 文件见 [NOTICE-nimble.txt](NOTICE-nimble.txt)；Apache-2.0 许可复用 [LICENSE-esp-idf.txt](LICENSE-esp-idf.txt)。
- **FreeRTOS Kernel**：FreeRTOS Kernel V10.5.1 (ESP-IDF SMP modified), Copyright (C) 2021 Amazon.com, Inc. or its affiliates, SPDX-FileContributor: 2023-2025 Espressif Systems (Shanghai) CO LTD. MIT。提取自 `.tools/esp-idf/components/freertos/FreeRTOS-Kernel/tasks.c` 头部，见 [LICENSE-freertos.txt](LICENSE-freertos.txt)。
- **TLSF**：Copyright (c) 2006-2016 Matthew Conte. BSD-3-Clause。算法实现位于 `.tools/esp-idf/components/heap/tlsf/`，许可文本由 Matthew Conte 版权与标准 BSD-3-Clause 全文组装，见 [LICENSE-tlsf.txt](LICENSE-tlsf.txt)。
- **FatFS**：FatFs - Generic FAT Filesystem Module R0.16, Copyright (C) 2025, ChaN. ChaN 许可。来源 `.tools/esp-idf/components/fatfs/src/ff.c` 首部注释，见 [LICENSE-fatfs.txt](LICENSE-fatfs.txt)。
- **protobuf-c**：Copyright (c) 2008-2022, Dave Benson and the protobuf-c authors. BSD-2-Clause。来源 `.tools/esp-idf/components/protobuf-c/protobuf-c/LICENSE`，见 [LICENSE-protobuf-c.txt](LICENSE-protobuf-c.txt)。
- **wpa_supplicant**：Copyright (c) 2002-2022, Jouni Malinen <j@w1.fi> and contributors. BSD-3-Clause。来源 `.tools/esp-idf/components/wpa_supplicant/COPYING` 与 `README`，见 [LICENSE-wpa_supplicant.txt](LICENSE-wpa_supplicant.txt)。
- **MbedTLS / TF-PSA-Crypto**：Arm Limited / Espressif. Apache-2.0。来源 `.tools/esp-idf/components/mbedtls/mbedtls/LICENSE`，主许可复用 [LICENSE-esp-idf.txt](LICENSE-esp-idf.txt)。
- **无线与射频预编译库**：`esp_wifi`、`esp_phy`、`esp_coex` 等二进制组件由乐鑫按 Apache-2.0 许可提供预编译对象，保留其归属说明。

## 工具链与 C 运行时

- **Picolibc**：固件配置 `CONFIG_LIBC_PICOLIBC=y`，使用工具链 `esp-15.2.0_20251204` 内置 Picolibc，含 Keith Packard、Newlib 及 BSD 衍生实现。许可文本完整复制自工具链 `share/licenses/picolibc/`，见 [COPYING.picolibc](COPYING.picolibc) 与 [COPYING.NEWLIB](COPYING.NEWLIB)。
- **GCC 运行时库**：Free Software Foundation, Inc. 适用 GPL-3.0 及 GCC Runtime Library Exception v3.1。文本复制自工具链 `share/licenses/gcc/`，见 [COPYING.RUNTIME](COPYING.RUNTIME) 与 [COPYING3](COPYING3)。依据运行时例外，链接该库不影响固件原创代码采用 Apache-2.0。

## Rust 核心库与底层内建

- **Rust core / alloc 标准库**：固定 Rust 1.99.1，The Rust Project Developers 及贡献者，Apache-2.0 OR MIT 双许可；本分发选择 Apache-2.0。归属依据本地工具链 `share/doc/rust/COPYRIGHT-library.html`，记录于 [rust-core-alloc.txt](rust/rust-core-alloc.txt)；`library/core/src/unicode` 的 Unicode-3.0 数据保留 Copyright © 1991-2024 Unicode, Inc.，许可见 [Unicode-License-V3.txt](Unicode-License-V3.txt)。
- **compiler-builtins**：0.1.160，Rust 团队，声明 `MIT AND Apache-2.0 WITH LLVM-exception AND (MIT OR Apache-2.0)`。保留固定 Rust 源中的 `library/compiler-builtins/LICENSE.txt` 原文于 [compiler-builtins-0.1.160.txt](rust/compiler-builtins-0.1.160.txt)；其内置 `libm` 的独立许可与 musl 等来源声明另见 [compiler-builtins-libm.txt](rust/compiler-builtins-libm.txt)，原文来自同树 `libm/LICENSE.txt`。
