# 技术调研与验证计划

本文记录创建项目指导时读取的上游资料；本项目尚未实现或验证固件。上游分支会变化，采用代码前需要固定提交。

## 本地事实

- 初始工作目录为空，没有 Git 元数据、README、包清单、CI、构建或检查配置，也没有已有 AGENTS / CLAUDE / Cursor 等项目指令需要迁移。
- 本次只创建文档；不存在可运行的安装、构建、测试、lint 或类型检查命令。
- `../giligili` 为同级参考工程。已读取其 `AGENTS.md`、`README.md`、`memos/runtime.md`、`memos/connectivity.md`，并检查 `setup.sh`、`tools/build-native.ts` 和 `native/Cargo.toml` 中的相关配置。

## 参考与适用边界

| 参考 | 已核实信息 | 本项目可借鉴与限制 |
| --- | --- | --- |
| [xteink-terminal](https://github.com/maddiedreese/xteink-terminal/blob/main/README.md) | ESP32-C3、GDEQ0426T82 / SSD1677、GxEPD2；尝试按变更行局刷；使用 WiFiManager AP 配网 | 借鉴显示驱动与刷新实验；AP 配网不满足本项目 BLE 需求，其刷新时间不是本项目保证 |
| [X4Term](https://github.com/penk/X4Term/blob/main/README.md) 与 [main.cpp](https://github.com/penk/X4Term/blob/main/src/main.cpp) | 800×480；使用 community-sdk；源码处理六个导航/确认/返回键和电源键，支持周期全刷 | 参考显示与按键层；逻辑键名不能代替实机位置、ADC 与唤醒验证 |
| [community-sdk](https://github.com/open-x4-epaper/community-sdk) | 面向 X4 的模块化 PlatformIO SDK，上游声明 MIT | 候选驱动来源；尚未验证与 ESP-IDF / Rust / MicroTS 的组合方式 |
| [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/README.md) | 默认分支 develop；同时支持 C3 和 S3 设备；SD 字体与缓存；提醒 C3 RAM 紧张 | 借鉴 X4 字体、存储、休眠和按键经验；不要误用其他机型功能。上游称约 380KB 可用 RAM，不是本项目实测余量 |
| [TRMNL](https://github.com/usetrmnl/trmnl-firmware/blob/main/README.md) | 按需联网、显示更新、休眠；支持多硬件环境 | 借鉴无线工作周期与低电量处理；不照搬其电池容量、引脚、续航或云端图片架构 |
| `../giligili` | ESP-IDF 6.1，S3；MicroTS strict AOT 将 App.vue 转为 Rust，链接原生静态库，构建时烘焙资源 | 借鉴应用编译链、原生接口与资源构建；不是可以直接移植的 X4 BSP |

`giligili` 的 `setup.sh` 固定 PocketJS 提交 `a9f186e2aa385fb353b05a12c1c8c592b76bec80`，可作为调研起点，尚未选定为本项目版本。其 `tools/build-native.ts` 使用 `buildAot(..., { strict: true, ... })`；这一方案不是设备内 JS 解释器，也不保证完整 TypeScript / Vue 支持。

## 候选架构（待验证）

- 板级层：C/C++ 管理屏幕、按键、SD、电源；优先评估已有 X4 驱动，避免重新猜测引脚与屏幕时序。
- 系统层：负责卡组读取、进度持久化、BLE 配网、无线生命周期和刷新调度。
- 应用层：MicroTS AOT 描述卡片视图与交互，通过窄接口访问系统服务；需要的原生逻辑可用 Rust 实现。具体支持的 MicroTS 语法与业务边界先验证。
- 内容层：独立于固件的卡组和素材；固件只读取数据，不执行下载的脚本。
- SDK、Arduino / ESP-IDF 集成方式、Rust 目标、PocketJS 版本和正式目录结构均未定稿。

## 首个技术关卡

| 验证 | 可验收的证据 |
| --- | --- |
| 具体硬件 | 记录型号/板版本、屏幕、Flash/SD、可用 RAM、每个物理按键对应的读取方式；区分普通键与复位 |
| 刷写与恢复 | 核实 USB 是否锁定、现有分区、备份和可行恢复方式；通过前不执行擦除或自定义固件烧录 |
| MicroTS on C3 | 最小视图编译、链接并在 C3 显示；记录工具链版本、固件大小、实际 heap 与最大可分配块 |
| 帧缓冲 | 800×480 的 1-bit 单缓冲为 48,000 字节；RGB565 单缓冲为 768,000 字节。核实 PocketJS 实际像素格式与分配方式，避免先分配整屏彩色缓冲再转黑白 |
| 峰值内存 | 测量屏幕刷新、中文字体、SD 读取、BLE 与 Wi-Fi 配网同时工作时的最小空闲 heap 与分配失败情况 |
| 显示与输入 | 连续快速翻卡、连续按评分键、长按释放和睡眠唤醒；无重复评分，刷新不积压；记录残影与全刷表现 |
| 字体与内容 | 显示汉字、shān、ü、带调拼音及加减符号；验证缺字、卡组缺失/损坏和读取失败时可退出 |
| 持久化 | 翻面不评分、跳过不评分、重复事件只计一次；重启恢复；写入中断后保留一份有效进度 |
| BLE 配网 | iOS / Android 客户端可达性、认证与加密、长凭据分包、错误密码、超时、取消、成功后关闭入口；日志不泄露密码 |
| 功耗 | 测量学习、待机、配网与休眠电流，验证唤醒；测量前不承诺电池续航 |

## 已知坑

- C3 与 S3 的指令集、可用内存和核数不同；不能沿用 `giligili` 的 Xtensa 编译器、8MB PSRAM 假设和 Core 1 绑定。
- `giligili` BLE 当前为无鉴权明文调试接口；可以参考状态反馈，不直接作为家庭产品凭据传输方案。
- CrossPoint 的 README 明确提示：部分 USB 锁定设备经解锁工具刷非受支持固件可能永久失去恢复路径；不能把“同样是 ESP32-C3”当作可安全刷写的证明。
- X4Term README 的 BMP Unicode 解码不意味着其终端字体包含中文；中文与拼音字形需要单独验证。
- 在上游 README 中看到构建成功、刷新时延或测试结果，不代表本项目通过；后续应保存本项目实际测量与命令。

## 文档验证

检查本项目 Markdown 的相对链接、文件存在性和行数；没有实现时不创建占位构建脚本，也不运行同级项目的构建来替代本项目验证。工具链落地后，再把已运行的安装、构建、定向测试和静态检查命令加入根 AGENTS.md。
