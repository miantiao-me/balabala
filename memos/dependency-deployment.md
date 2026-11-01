# 依赖升级候选的提交与烧录

2026-10-29，用户授权提交并烧录。依赖升级提交为 `974dde7cefea86dc49777292eff35452e7ae27d6`（`chore: 升级依赖并适配 ESP-IDF 6.1`）；本记录及验收状态在部署后单独提交。本轮未推送或发布。

## 设备与恢复准备

仅操作此前同一台原版 XTEINK X4 / ESP32-C3 rev0.4。烧录前重新确认 USB 串口无占用、16MB Flash、安全标志为 0、Secure Boot / Flash Encryption 关闭、USB ROM 可用，分区与原引导程序匹配。

- 私有证据目录：`.build/dependency-upgrade-device-20261029T093541Z/`，权限 0700，已被 Git 忽略。
- 完整备份：`before-full-16mb.bin`，16777216 B，分两段读取，再与设备独立摘要校验。
- 完整备份 SHA256：`7fe58d17b07d3f0e3499576ac46a88df9021fadfbc9e21013817993ba3552996`。
- 回退应用：`restore-previous-app.bin`，4599600 B，从本次备份的 `0x10000` 提取，匹配上次部署的 `116bcfd` 应用。
- 回退应用 SHA256：`ce58abf65cdc0d43778a7256985b941cd54e0adebcf45a854e34127e4808c120`。

恢复指引在私有目录的 `RECOVERY.md`。保留 USB ROM 和旧 ESP-IDF 6.0.3 bootloader，按该版本兼容的新应用与旧应用均可通过原通道写入；本轮未演练回退写入。完整 Flash 备份可能含设备私有配置，不上传或提交，且不包含外部 SD 卡。

## 镜像与写入范围

| 项目 | 结果 |
| --- | --- |
| 应用 | ESP-IDF 6.1 普通 z 候选，4620160 B，autotest 关闭 |
| BIN SHA256 | `0e0a32c67116bd67dc1344ebd94ffc4e69ca5fe00fc4082269cdeb83013bd9b2` |
| ELF SHA256 | `178421bfe68b52f9b4962a316f2b2f6cabfaabe029e4b9d8f67b14cad08ea38d` |
| 写入位置 | 仅 `0x10000` 应用分区，DIO / 40MHz / 16MB |
| 写入与独立 verify | 均成功，设备摘要匹配 |
| 启动前前 64KiB 回读 | bootloader、分区表、NVS、PHY 与备份逐字节一致 |
| eFuse / 整片擦除 | 均未执行 |

按固定 ESP-IDF 6.1 源码的 `docs/en/api-guides/bootloader.rst` 兼容说明，旧 bootloader 支持新 IDF 应用；需要回退旧应用时保留旧 bootloader。本轮保留已核验的 21280 B ESP-IDF 6.0.3 bootloader，不写构建得到的 21152 B 新 bootloader，分区布局保持原值。

使用此前已完成 106 项 Rust、120 项 C、1029 张冻结页、81 张 ABI 帧及 C3 验证的同一镜像。镜像在提交前构建，日志中的应用版本为 `be2a970-dirty`；提交后再次核对 167 个功能源码文件和完整产物摘要，映射到 `974dde7`，未因提交重新生成另一份固件。命令与返回码保存在 `commands.json`，启动命令另存 `start-command.log`。

## 25 秒启动观察

单串口复位并采集 25 秒，结束后关闭连接。运行 ELF 前缀 `178421bfe` 与冻结候选一致，日志确认 ESP-IDF 6.1 应用由保留的 6.0.3 bootloader 加载。

- USB 供电与 SD 挂载成功；旧档恢复为 `import=0 restore=0 status=4`。
- 首屏到达 `display-end`，全刷一次、首次尝试成功。
- 仅一次预期启动，未见 panic、异常重启、保存错误或显示失败。
- 新日志包含 GPIO 12 / 21 复用告警；SD 与首屏成功，连续共享 SPI、SD 和显示操作仍待专项验证。本轮未修改接线或消除告警。

| 采样点 | 空闲 heap | 累计最低空闲 | 最大连续块 | 主任务未用栈水位 |
| --- | ---: | ---: | ---: | ---: |
| `display-end` | 91988 B | 65352 B | 81920 B | 27460 B |
| `save-end` | 91812 B | 56136 B | 81920 B | 27460 B |

仅为启动阶段样本，不代表完整学习、无线共存及所有任务预算已通过。脱敏日志为 `boot-redacted.log`，汇总为 `startup-summary.json`、`deployment-summary.json`，文件摘要见 `SHA256SUMS`。

## SD 课程与待验项

主机没有挂载设备 SD 卷，本轮只烧录应用，未安装新课程正文或覆盖 SD 存档。设备端 SD 挂载与旧档恢复，不代表新 4000 张课程已安装。

配套正文为 `.build/sdcard/balabala/curriculum/`，`course.bin` 874632 B，SHA256 `5d04c2f6b109081cda19ece421b0a0b40297f0e42cdee1b979f7b83bb871819b`，CRC32 `154bbdf2`。需先备份 `/balabala/saves/`，关机后按 [README](../README.md#sd-卡课程目录) 安装全部正文及归属文件。

完整课程连续加载、实际答题保存与恢复、损坏/掉电、实体按键、局刷残影、休眠电流，以及官方 iPhone 配网、广播关闭和超时待专项验收。软件升级与验证见[依赖升级记录](dependency-upgrade.md)，累计证据见[验收文档](../docs/acceptance.md#5-依赖升级候选部署与启动观察2026-10-29)。
