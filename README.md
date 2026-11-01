# 扒拉扒拉

面向原版 XTEINK X4 / ESP32-C3 的小学一至三年级离线卡片学习固件，覆盖汉字、拼音、英语和数学。孩子可以翻卡、四选一答题和复习；家长可调整学习设置、查看进度。在首页长按“返回”2 秒进入家长设置，完整操作见[使用说明](docs/usage.md)。

应用采用 MicroTS strict AOT 页面、Rust 模型与平台 C 运行时。页面在构建期编译，设备不提供浏览器或动态 JavaScript 环境；常规 NPM 包不能直接作为固件运行时依赖。

## 核心功能与课程

- 完整课程共 **4000 张卡片、135 个单元**：汉字 2200 张、拼音 150 张、英语 450 张、数学 1200 张。英语包含 225 个词的双向练习；汉字限 GB2312 一二级汉字，英文词限固定 NGSL 原形白名单，详见[课程说明](docs/curriculum.md)。
- 默认每轮 5 张，可选 10 张；同题每轮只评分一次。默认跨轮累计答对 3 次学会，可选 1 / 3 / 5 / 7 / 9 次；已学会卡片退出抽题，当前单元学会至少 80% 后解锁下一单元。
- 完整课程正文从 SD 卡按轮读取，最多同时加载 10 张。无课程包时可体验 128 张内置示范卡；无 SD 或未挂载时不能保存新进度，界面显示“存储不可用”。
- 支持特大字翻卡、四列单选、静态鼓励图案、主动翻题和双槽进度恢复。不配网也能学习。

## 硬件与平台约束

目标为原版 X4：ESP32-C3 rev0.4、单核 RISC-V、最高 160MHz、无外部 PSRAM，SSD1677 墨水屏采用 480×800 逻辑黑白布局。不要套用 X4 Pro、X4 Classic 或 ESP32-S3 的配置。

侧面上下键翻页，电源短按回首页、长按关机；正面四键在未答四选一时对应 A / B / C / D，其他页面对应左 / 右 / 返回 / 确认。实体 ADC 键序仍待真机核验，显示完成与松键门禁见[产品设计](docs/product.md#按键方案)。

Flash 固定 DIO / 40MHz / 16MB，`factory` 偏移为 `0x10000`、大小为 `0xFF0000`。接线、共享 SPI、显示和电源契约见[平台文档](docs/platform.md)；双槽各 32768 字节及原档保护规则见[双槽存档](docs/learning-app.md#双槽存档)。

BLE 配网使用乐鑫官方 iOS 客户端、Security 1 和每会话随机 6 位数字 PoP（配网码，保留前导 0）。NVS 未加密，底层 BLE stop 永不返回仍是已记录限制，详见[配网文档](docs/ble-provisioning.md)。

## 常用命令

安装前准备 Git、curl、unzip、Python 3.10+（含 venv）、rustup、shasum、patch 和宿主 C/C++ 编译器。已验证 macOS arm64 / Python 3.14.5；安装脚本也支持 Linux arm64 / x64，Linux 尚待验证。

在仓库根目录执行：

```sh
# 安装固定依赖、工具链与补丁
sh tools/setup.sh

# 完整宿主测试，包含 Rust、C 平台测试和 PBM 页面生成
sh tools/build.sh test

# 默认 s 配置的 C3 构建与尺寸检查
sh tools/build.sh build

# 当前设备候选使用 z，仅覆盖本次进程，项目默认仍为 s
CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build
```

脚本使用项目内 `.tools/`，不改变全局 Rust 默认工具链；固定 ESP-IDF 6.1、Rust 1.99.1、Bun 1.4.4，版本见 [versions.sh](tools/versions.sh)。不要在 `native/` 直接运行 Cargo。`.tools/`、`.build/` 均已忽略，固件产物为 `.build/firmware/balabala_microts_probe.{elf,map,bin}`。

筛选 Rust 测试及检查源码、环境：

```sh
sh tools/build.sh test network_
sh tools/build.sh test tests::read_flip_skip_and_back_do_not_score -- --exact
python3 tools/test-source-integrity.py
python3 tools/test-build-env.py
```

筛选 Rust 测试仍会运行完整 C 平台测试；环境检查会实际调用安装入口。旧依赖缓存不会自动覆盖：跨版本升级前在旧代码核验缓存，再将 `.tools/` 和 `.build/` 完整移到仓库外保留，随后重新安装、构建。私有设备备份须保留且避免公开。

项目不提供通用自动烧录脚本。烧录前须核验具体型号、Flash、分区、USB 锁定及恢复路径，完成完整 16MB 镜像备份；维护入口见[刷机模式](docs/download-mode.md)。

## SD 卡课程目录

将与固件同次构建的 `.build/sdcard/` 内容复制到 FAT32 SD 卡根目录：

```text
balabala/
├── curriculum/
│   ├── course.bin        # 课程正文
│   ├── manifest.json     # 版本、数量与校验信息
│   ├── NOTICE.md         # 数据归属
│   └── attribution/      # 许可、人工源与固定来源
└── saves/                # 存档槽，由固件维护
```

先让设备保存并关机，再取卡操作。更新前备份 `balabala/saves/`，复制时保留 `NOTICE.md` 和 `attribution/`，安全弹出后装回设备。课程格式、匹配检查与审校边界见[课程说明](docs/curriculum.md#sd-课程包)和[内容说明](content/README.md)。

## 文档索引

| 需要了解 | 文档 |
| --- | --- |
| 设备操作与学习规则 | [使用说明](docs/usage.md)、[产品设计](docs/product.md)、[课程与掌握体系](docs/curriculum.md) |
| 代码与应用接口 | [修改入口](docs/architecture.md)、[学习应用](docs/learning-app.md)、[性能基准](docs/performance.md) |
| 硬件与维护 | [平台运行时](docs/platform.md)、[刷机模式](docs/download-mode.md) |
| 配网操作与生命周期 | [BLE 配网](docs/ble-provisioning.md)、[分阶段配网](docs/staged-provisioning.md) |
| 依赖维护 | [补丁指南](patches/README.md)、[依赖升级备忘](memos/dependency-upgrade.md) |
| 验证与部署证据 | [验收记录](docs/acceptance.md)、[依赖升级部署](memos/dependency-deployment.md)、[发布复审](memos/release-review.md) |

## 开源许可与版权声明

原创源码采用 [Apache-2.0](LICENSE) 许可证，版权归原作者所有。第三方组件、字体、课程数据和工具链保留各自许可，完整归属及许可原文见[第三方说明](licenses/README.md)；分发课程时须保留配套归属文件。

## 验证状态与限制

最新宿主回归、页面比较和 C3 构建结果集中维护于[验收记录](docs/acceptance.md)。

原版 X4 测试机已部署睡眠页居中、12 幅奖励图案、重置防误触、可调清屏间隔与课程来源页面，独立镜像校验及 25 秒启动观察通过，确认 SD 挂载、旧档恢复和首屏全刷；保留原 ESP-IDF 6.0.3 引导程序。配套的完整 SD 课程已复制并逐项校验，本轮课程文件未变，无需重新拷贝；设置操作、完整课程识别及长间隔残影仍待真机交互确认。

4000 卡持续加载、真实无线 / RTOS、6 位 PoP 的官方 iPhone 端到端配网、广播关闭、ADC 键序、SD 掉电、动态 heap 与最大连续块、各任务栈高水位、功耗和唤醒仍待专项真机验收。宿主测试、构建或启动观察不能替代这些结果。
