# 首版交付与验收

本轮可调清屏间隔与课程来源见[清屏设置与课程来源验收](#清屏设置与课程来源验收)；此前重置防误触、跨页局刷与 SD 安装见[重置与刷新优化验收](#重置与刷新优化验收)，页面调整见[睡眠页与奖励图案验收](#睡眠页与奖励图案验收)；依赖与其余待验项目见[最新依赖升级验收](#最新依赖升级验收)。历史记录按原候选保留：早期 4MB / 3MiB 分区、16 位 PoP 和旧部署结果均不代表当前 16MB / 6 位 PoP 候选。

操作见[使用说明](usage.md)，交互见[产品设计](product.md)，硬件契约见[平台文档](platform.md)，存档修复历史见[当前存档保护验证](#当前存档保护验证)。

## 交付范围

- 原版 XTEINK X4 / ESP32-C3 固件；优先验证 PocketJS / MicroTS AOT。若不可行，提供证据和替代方案，等待用户决策。
- 桌面、四科学科/卡组选择、认一认翻卡、四选一、答对静态黑白鼓励图案、答错解释、主动翻页、复习、进度保存与恢复、家长设置。
- 每科至少 20 张示范卡及四个选项、唯一正确答案；检查内容、字形覆盖和授权。
- 默认每轮 5 张，可设 10 张；SD 卡导入、格式校验、缺失/损坏资源的可恢复处理、重置进度二次确认。
- BLE 配网使用乐鑫官方 iOS 客户端，不开发自有手机端；核验协议、安全配置、凭据保护、状态反馈、取消、超时及完成后关闭入口。
- 实际 C3 交叉构建、宿主行为测试、独立 Review、问题修复和回归验证。

不包含 Android 专项交付、无线内容导入、OTA、语音、录音、发音评分、云端 AI、应用商店和复杂动画。设备验证已获授权，范围限定为已完整备份并核验的原设备及原 factory 内 app 写入、自动测试；不扩大为解锁、整片擦除或发布。后续候选仍须先经主 Agent 审核再部署。

## 行为验收

| 行为 | 所需证据 | 当前状态 |
| --- | --- | --- |
| 翻面与跳过不评分，四选一判断正确 | 状态机行为测试 | 宿主通过 |
| 每题每轮仅评分一次，返回已答题保留结果 | 连按与回看测试 | 宿主通过 |
| 答对有黑白图案，反馈停留到主动翻页 | 视图输出与状态转换测试 | 宿主通过，PBM 可审查 |
| 新题显示完成、上一按键释放后才接收答案 | 输入与刷新时序测试 | 模型/C ABI/运行时宿主通过；物理时序待验证 |
| 电源短按回桌面，长按不先触发短按 | 按下/保持/松开时序测试 | C 输入和运行时宿主通过；GPIO/ADC 与睡眠待真机 |
| 屏幕文字与机身朝向一致 | 最终转换四角/边界/非对称测试及实机观察 | 最终传输转换相对旧输出旋转 180 度，离线回归通过；修正版部署后用户已确认屏幕方向正确，键位未改且物理映射仍待验证 |
| 正面四键、侧面上下键覆盖学习与菜单操作 | 页面导航行为测试 | 宿主通过，含反馈页左右/确认/返回 |
| 退出、重启恢复；写入中断不破坏有效进度 | 持久化与故障测试 | 双槽及真实 storage/ABI/runtime 回归通过；启动读失败、卡组回退不可解释时持续保护原档，修复后新启动完整恢复；SD 掉电仍待真机 |
| 稳定卡片 ID 保留更新后的进度 | 卡组更新测试 | 宿主通过，含重排后重启保留原轮次 ID/游标/结果 |
| 卡组缺失、损坏、缺字可退出并纠正 | 内容校验及错误页面测试 | 宿主通过，含真实 C ABI/C 存储；SD 读取已接入，真机待验证 |
| BLE 连接、失败、取消、超时、完成关闭 | 固定官方协议源码、C 状态机/适配故障测试、Rust 页面/ABI、C3 构建与真机 | 完整宿主及默认 s / phase2 z C3 通过；phase2 z 真机预热＋3轮自动生命周期通过；旧16位PoP普通 z 已审核部署且用户确认官方 iPhone 配网成功。本次6位PoP、失败/退出、空中广播关闭及300秒超时仍待真机验证 |
| 刷机模式保存/清理/精确提示后进入，取消/关机优先 | 模型/AOT/PBM、真实 Rust ABI runtime 故障注入、C3 镜像头 | 完整宿主/C3通过；ROM/USB/屏幕保持未真机验证 |
| C3 固件链接成功且资源预算合理 | 构建命令、产物位置、大小报告 | 最新构建、尺寸与分区检查见[最新依赖升级验收](#最新依赖升级验收)；[UI 验收](#ui-与-ux-重写验收)中的 s/z 指标属于历史候选。完整运行内存预算仍待真机闭合 |
| 独立 Review 阻止交付的问题全部解决 | 发现、修复、回归证据 | 当前 UI 的长解释排版兼容、休眠帧确认两项 P2 已修复并独立复核闭合，未发现有证据的 P0/P1；历史 BLE 和存档修复的审查及设备证据保留在各节，均不替代当前候选真机验收 |

## 清屏设置与课程来源验收

家长设置第一项“学习设置”新增清屏间隔，按确认依次切换 8 / 16 / 32 / 64 / 128 次，默认 8；按实际局刷成功次数累计，修改不清零计数，调小后达到上限则下一帧全刷。设置随现有 v2 存档保存，旧 v1/v2 缺字段时默认 8，非法值使该槽语义无效；双槽上限、原档保护与重置进度所保留的偏好不变。开机、关键页面进出、休眠和显示失败后的全刷门禁继续生效，未改波形、旧帧同步或帧缓冲。

家长区第六项为课程来源，五行菜单向下滚动可见，原蓝牙第四项和刷机第五项不变。三页显示 Unicode Unihan 18.0.0 / Unicode License V3、NGSL 1.2（2023）及作者 / CC BY-SA 4.0，以及拼音、词义、数学与主题的项目编排范围；保留非教材官方年级排序、尚未逐题人工审校的说明。完整归属、许可和修改说明随 SD 课程包提供。

| 验证 | 命令 / 结果 |
| --- | --- |
| 完整宿主 | `BALABALA_COMPARE_PAGES="$PWD/.build/parent-display-settings/baseline-pages" sh tools/build.sh test`：114 项 Rust 通过、1 项性能基准忽略；120 项 C 平台与集成测试通过，含真实 Rust ABI、ASan/UBSan |
| 行为覆盖 | 五档切换、显示回执与松键门禁、保存恢复、旧 v1/v2 缺字段默认值和非法值拒绝；通过真实菜单/ABI逐字节核验五档各两轮局刷与全刷、128 次边界及调小后及时全刷；来源翻页/退出与原入口位置 |
| 页面回归 | 1043 份冻结 View 和 1 份存储错误页重渲染逐字节一致；当前模型同名 1034 份 PBM 不变、10 份仅家长/学习设置按预期变化，15 份新增设置与来源页面已检查排版、文字边界及渲染内存，无其他变化 |
| C3 普通 z 构建 | `CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build`：通过；DIO / 40MHz / 16MB、`factory=0x10000/0xff0000`、65536 B 主任务栈与普通固件契约通过，清单默认仍为 s |
| 固件与素材 | BIN 4,674,896 B，factory 剩余 12,036,784 B；插图仍 291,014 B，字体归档 2,521,267 B，增加 5,625 B 字形，无新素材依赖 |

证据位于忽略目录 `.build/parent-display-settings/`：`host-tests.log`、`build-z.log`、`page-comparison.json`、`result.json`，以及 `settings-preview.png`、`sources-preview.png`。BIN SHA-256 为 `0ed22eebb27de15ed594506970dbe051ff44caf6fa8c6c19900482dd436f7b23`。真实按键、设置后断电恢复、来源阅读观感及 16–128 次连续局刷残影仍待真机交互验证；测试替身不代表面板标定。

用户授权后已部署到同一台原版 X4 / ESP32-C3 rev0.4。写前重新核验 16MB、USB ROM、安全标志 0、分区和原 bootloader，完整 16,777,216 B Flash 备份经设备独立摘要校验，SHA-256 为 `86a09bd7334605eb1a60af310f2f1365fef249115babd0bb94ed01b826c7604f`，回退应用匹配 `3c6f6e0`。仅写 `0x10000` 应用并独立 verify，启动前前 64KiB 与备份逐字节一致；未写 bootloader、分区、NVS、PHY 或 eFuse，未整片擦除。私有备份、回退应用、`RECOVERY.md` 和 228 个功能文件摘要位于 `.build/parent-display-settings/device/`，权限 0700 且已忽略；恢复写入未演练。

25 秒启动观察确认 ELF 前缀 `5340dc0c1` 匹配本轮镜像，USB 供电、SD 挂载、存档恢复 `import=0 restore=0 status=4` 与首屏全刷成功，刷新日志确认旧档默认上限 8，无异常重启、保存或显示错误。`display-end` 空闲 heap / 累计最低 / 最大连续块为 128352 / 89420 / 81920 B，`save-end` 为 128176 / 84412 / 81920 B，主任务未用栈水位 27460 B；仅是启动采样。配套 SD 课程 12 文件再次逐项 SHA-256 通过，与此前安装版本一致，无需重新拷贝；设备端菜单操作、断电后设置恢复、连续局刷残影与完整课程加载仍待交互验证。

## 重置与刷新优化验收

重置前须用 A/B/C/D 连续答对 3 道两位数乘法加数题；每题只有一个正确选项，正确键随题目变化。错题立即退出，电源取消和关机优先；显示完成与松键门禁同样适用。三题通过只打开默认取消的最终确认，退出或重启即清除临时验证状态；验证不影响学习记录和存档格式，原档保护继续生效。

普通页面之间切换也允许局刷，减少首页、卡组、学习与家长区切换时的全刷。驱动仍每连续 8 次局刷后全刷清理残影，首次显示、配网、刷机准备/提示、错误、休眠及显示失败后的重试保留全刷；没有修改波形、旧帧同步或刷新上限。

| 验证 | 命令 / 结果 |
| --- | --- |
| 完整宿主 | `BALABALA_COMPARE_PAGES="$PWD/.build/reset-guard/baseline-pages" sh tools/build.sh test`：111 项 Rust 通过、1 项性能基准忽略；120 项 C 平台与集成测试通过，含真实 Rust ABI、ASan/UBSan |
| 行为覆盖 | 三题后另行确认、默认取消、错题/电源取消、显示未完成/旧键/连按、重启与直接确认不可绕过、设置及原档保护；全页面刷新真值表、跨页局刷、关键页进出全刷、失败重试与 8 次局刷上限 |
| 页面回归 | 1040 份冻结 View 和 1 份存储错误页逐字节一致；当前模型的这 1041 份 PBM 也未变化，新增 3 份家长验证页已检查排版与文字边界 |
| C3 普通 z 构建 | `CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build`：通过；DIO / 40MHz / 16MB、`factory=0x10000/0xff0000` 与普通固件契约通过，清单默认仍为 s |
| 固件与素材 | BIN 4,667,360 B，factory 剩余 12,044,320 B；插图 291,014 B、字体归档 2,515,642 B，与上次候选一致 |

证据位于忽略目录 `.build/reset-guard/`：`host-tests.log`、`build-z.log`、`page-comparison.json`、`result.json`，以及 `reset-check-preview.png`、`reset-confirm-preview.png`。BIN SHA-256 为 `ff5ab4e2ba4291c72860cfa14999a301a7f9e34af505868e1abf4e4e5ee9f6aa`。真实按键误触、连续跨页残影及刷新观感仍待真机交互验证，测试替身不代表面板实测。

用户授权后已部署到此前同一台原版 X4 / ESP32-C3 rev0.4。写前重新核实 16MB、USB ROM、安全标志为 0、分区和原引导程序，完整 16,777,216 B Flash 备份经设备独立摘要校验，SHA-256 为 `01987dd16a85c5c331608ebff86e4b47ed5648a3ac08a04f460a0e0e29dab93f`。从备份提取的上版应用匹配已部署 `7213974`；仅写 `0x10000` 应用并独立 verify，启动前前 64KiB 与备份逐字节一致，未修改 bootloader、分区、NVS、PHY 或 eFuse，未整片擦除。

25 秒启动观察确认 ELF 前缀 `9ff30d1a6` 匹配本轮镜像，USB 供电、SD 挂载、存档恢复 `import=0 restore=0 status=4` 与首屏全刷成功，无异常重启、保存或显示错误。`display-end` 的空闲 heap / 累计最低 / 最大连续块为 128388 / 89420 / 81920 B，`save-end` 为 128212 / 84412 / 81920 B，主任务未用栈水位均为 27460 B；这些是启动采样，不替代完整学习、重置按键或刷新残影验收。私有部署证据位于 `.build/reset-guard/device/`，权限 0700 且已忽略，包含完整备份、回退应用、`RECOVERY.md`、命令及脱敏启动日志；`candidate.json` 冻结 227 个功能文件摘要及 ELF 身份，供提交前后核对，恢复写入未演练。

用户插入的 FAT32 SD 卡已安装完整课程：先备份原 `balabala/`，复制 12 个课程文件到 `/balabala/curriculum/`，同步后逐项 SHA-256 校验通过，两个存档槽前后摘要一致，并已安全弹出。`course.bin` 为 874,632 B，SHA-256 `5d04c2f6b109081cda19ece421b0a0b40297f0e42cdee1b979f7b83bb871819b`，与设备已部署的 `7213974` 及本轮构建产物一致。安装证据和私有备份在 `.build/reset-guard/sd-install-20261029-213031/`，权限 0700 且已忽略。电脑端校验已完成；装回设备重启后的完整课程识别与持续按轮加载仍待验证。

## 睡眠页与奖励图案验收

睡眠页使用独立布局，月亮墨迹纵向范围为 y=306–493，中心对齐 800px 屏幕的 y=400；标题和唤醒提示位于下方。答对奖励从 3 幅扩充至 12 幅原创黑白线稿，按已存档的轮次序号和题目位置轮换；5 / 10 题同轮不重复，回看或恢复后保留同题图案，反馈仍等待主动翻页。

| 验证 | 命令 / 结果 |
| --- | --- |
| 完整宿主 | `BALABALA_COMPARE_PAGES="$PWD/.build/ui-art-refresh/baseline-pages" sh tools/build.sh test`：107 项 Rust 通过、1 项性能基准忽略；120 项 C 平台与集成测试通过，含真实 Rust ABI、ASan/UBSan |
| 页面回归 | 1029 份冻结 PBM 按相同 View 输入重渲染逐字节一致；当前模型同名输出 914 份不变、114 份仅奖励插图区域变化、1 份睡眠页变化，无非预期变化；额外检查全部 12 幅奖励页 |
| C3 普通 z 构建 | `CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build`：通过；DIO / 40MHz / 16MB、`factory=0x10000/0xff0000` 与普通固件契约通过，清单默认仍为 s |
| 固件与素材 | BIN 4,666,032 B，factory 剩余 12,045,648 B；新增位图 45,000 B，直接借用 Flash，不新增运行时图案缓存 |

证据位于忽略目录 `.build/ui-art-refresh/`：`host-tests-final.log`、`build-z-final.log`、`page-comparison.json`、`result.json`，以及 `sleep-comparison.png`、`reward-gallery.png` 预览。BIN SHA-256 为 `6ab7c2baed355e1d2993797a8ab5ac5146d14095fb71c1bf8d1293802fdb5bd9`。

用户授权后已部署到同一台原版 X4 / ESP32-C3 rev0.4。写前重新核实 USB ROM、16MB、安全标志为 0、分区及原引导程序，完整 16777216 B Flash 备份经设备独立摘要校验，SHA-256 为 `05a98875880ce8533c8fb7df9aaa9840b536e69f4381c26a809be27ef14119ad`。仅写 `0x10000` 应用并独立 verify；启动前前 64KiB 与备份逐字节一致，保留 bootloader、分区、NVS 和 PHY，不写 eFuse、不整片擦除。

25 秒启动观察确认 ELF 前缀 `c0b56835b` 匹配新镜像，SD 挂载、旧档恢复 `import=0 restore=0 status=4` 和首次全刷成功，无异常重启、保存或显示错误。`display-end` 的空闲 heap / 累计最低 / 最大连续块为 88516 / 61756 / 63488 B，`save-end` 为 88340 / 43448 / 63488 B，主任务未用栈水位均为 27460 B。这些仅是启动采样，睡眠与奖励实机观感、连续学习及其余硬件指标仍待专项验证。

私有部署证据位于 `.build/ui-art-refresh/device/`，权限 0700 且已忽略，包含备份、回退应用、`RECOVERY.md`、命令日志和脱敏启动摘要。镜像在提交前构建，`candidate.json` 冻结 225 个功能文件用于提交后再次核对；回退写入未演练。电脑未挂载 SD 卷，本轮未安装完整课程；配套 12 个文件的校验清单位于 `.build/ui-art-refresh/sdcard-SHA256SUMS`。


## 最新依赖升级验收

本节对应 ESP-IDF 6.1、PocketJS `8bc05257` 及 Unihan 18.0.0 的依赖升级候选；以下宿主与构建结果不替代真机验收。

### 1. 验证命令与测试结果

| 验证项目 | 执行命令 / 证据文件 | 结果 | 关键指标与说明 |
| --- | --- | --- | --- |
| 宿主单元与集成测试 | `BALABALA_COMPARE_PAGES="$PWD/.build/dependency-upgrade/baseline-pages" sh tools/build.sh test` | 106 passed, 0 failed, 1 ignored | Rust 106 项通过，1 项性能基准（`tests::performance_render_import_and_save`）默认忽略 |
| C 平台与运行时测试 | 随完整宿主命令运行，见 `.build/dependency-upgrade/host-tests.log` | 120 passed, 0 failed | 22 平台 + 50 runtime / 真实 Rust ABI + 17 network + 8 IDF adapter + 8 manager + 1 protocomm + 14 event；ASan/UBSan 无报错 |
| 事件、广告与内存池回归 | 同一宿主日志 | passed | 事件集成：2665 次分配、10 次分配故障注入、8 次队列丢弃、25 次重开；广告场景通过；NimBLE mempool 2561 次分配/释放配对 |
| 页面像素一致性 | 宿主测试自动比对 | 1029 / 1029 逐字节一致 | 1028 冻结 View 输入与 1 存储错误页生成的 PBM 与基线完全一致 |
| 真实 ABI 面板帧 | 比对 `.build/provisioning-ui-memory-current/abi-pages`（记录见 `panel-pages-comparison.json`） | 81 / 81 逐字节一致 | 81 组真实 ABI 驱动的配网状态面板帧与基线逐字节一致 |
| 源码完整性门禁 | `python3 tools/test-source-integrity.py` | 19 / 19 passed | 覆盖 PocketJS 归档与补丁、ESP-IDF 根目录与递归子模块、文件类型/内容漂移与补丁重放，耗时 6.450s |
| 构建环境隔离 | `python3 tools/test-build-env.py` | 3 组全部 PASS | 验证环境隔离、IDF Python 虚拟环境与失败传播 |
| C3 交叉构建 | `CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build` | exit 0 | 成功生成 ELF、MAP 与 BIN，后置镜像头与分区尺寸校验通过 |

### 2. 固件指标与分区契约

当前 `opt-level = "z"` 构建产物指标：

- 应用 BIN 尺寸：4,620,160 字节，SHA-256 为 `0e0a32c67116bd67dc1344ebd94ffc4e69ca5fe00fc4082269cdeb83013bd9b2`
- 未填充应用镜像尺寸：4,619,761 字节
- Bootloader 尺寸：21,152 字节
- 分区表尺寸：3,072 字节，SHA-256 为 `655cd209ba03a1c8b6108ac5762de3ca6e2610f23a4fbf2e99792d3894a610cf`
- Flash 配置：DIO / 40MHz / 16MB，`factory` 起始偏移 `0x10000`，容量 `0xff0000`（覆盖至 16MiB 边界）
- `factory` 分区剩余空间：12,091,520 字节（超过 11.5MB）
- 链接器 DRAM 统计：已用 173,649 字节（包含 .text 81,644 字节、.bss 77,000 字节、.data 15,005 字节；静态 .data + .bss 为 92,005 字节）；链接表剩余 147,647 字节（链接器剩余不等于运行时可用 heap）
- 尺寸对比：较旧 z 固件（4,599,600 字节）增长 20,560 字节（+0.45%）；本轮未重新测量运行性能
- 项目清单文件（`Cargo.toml`）默认配置仍为 `opt-level = "s"`，`z` 仅用于候选构建

### 3. 课程配套文件变化

- 读音源升级至 Unihan 18.0.0，课程保持 4000 张卡、135 单元，稳定 ID 无增删
- 新版读音属性与词境修订使 1399 张汉字卡的选项变化
- 反面及解释只有 `hz-u5566`（哗啦中的“啦”）从 `la` 改为 `lā`；保留“看啊”的 `a` 与“钥匙”的 `shi`，三项词境均由行为测试覆盖
- 新课程包：`course.bin` 尺寸为 874,632 字节（SHA-256 `5d04c2f6b109081cda19ece421b0a0b40297f0e42cdee1b979f7b83bb871819b`），`manifest.json` 尺寸为 388 字节（SHA-256 `cdd307c8967d70f47ad3c323f5b7b20fc4738f7c8eb3be7c9e9cedf1088ac796`，CRC32 `154bbdf2`）
- `course.bin` 必须与同次构建的固件配套复制到 SD 卡，不可跨版本混用
- `assets.json` 与 `projection.json` 摘要与基线完全一致

### 4. 设备现状与未验边界

- **设备现状**：原版 X4 测试机已运行 `974dde7` 对应的依赖升级应用，写入、独立校验及 25 秒启动观察通过；保留原 ESP-IDF 6.0.3 引导程序以支持旧应用回退，部署范围与证据见下节。
- **未验边界**：
  - 同次构建的新 SD 课程尚未安装，4000 张卡片在物理 SD 卡上的持续加载性能待验；
  - 学习与无线全流程的 C3 动态堆、最大连续块和各任务实际栈高水位（本次仅采集启动阶段）；
  - 乐鑫官方 iOS 客户端使用 6 位数字 PoP 的端到端配网流程、空中广播关闭与 300 秒超时；
  - 整机休眠待机电流、ADC 物理按键长期手感与墨水屏局刷残影。
- 完整升级记录与证据索引见[依赖升级备忘](../memos/dependency-upgrade.md)。

### 5. 依赖升级候选部署与启动观察（2026-10-29）

用户明确授权后，将 `974dde7` 对应的已验证普通 z 应用写入此前同一台原版 X4 / ESP32-C3 rev0.4。写前重新核验 16MB、USB ROM、安全标志为 0、分区和原引导程序，并读取完整 16777216 B Flash 备份，与设备独立摘要核对。备份 SHA256 为 `7fe58d17b07d3f0e3499576ac46a88df9021fadfbc9e21013817993ba3552996`，旧应用提取后匹配上次部署。

仅写 `0x10000` 的 4620160 B 应用，独立 verify 成功；启动前回读前 64KiB，与备份逐字节一致。保留 ESP-IDF 6.0.3 bootloader、分区、NVS 和 PHY，不写 eFuse、不整片擦除。ESP-IDF 6.1 官方引导程序兼容说明支持旧 bootloader 启动新应用，保留旧 bootloader 也为应用回退保留兼容性；构建得到的 21152 B 新 bootloader 本次未写入。

25 秒单串口观察确认运行 ELF 前缀 `178421bfe` 匹配冻结镜像，应用为 ESP-IDF 6.1；USB 连接、SD 挂载、旧档恢复 `import=0 restore=0 status=4` 成功，首屏全刷一次、首次尝试成功。仅一次预期启动，未见 panic、异常重启、保存错误或显示失败。镜像在提交前构建，日志中应用版本保留 `be2a970-dirty`；167 个功能源码文件及完整镜像摘要在提交后再次核对，映射到源码提交 `974dde7`。

| 启动采样点 | 空闲 heap | 累计最低空闲 | 最大连续块 | 主任务未用栈水位 |
| --- | ---: | ---: | ---: | ---: |
| `display-end` | 91988 B | 65352 B | 81920 B | 27460 B |
| `save-end` | 91812 B | 56136 B | 81920 B | 27460 B |

本次启动新增 GPIO 12 / 21 复用告警，SD 挂载与首屏显示仍成功；连续共享 SPI、SD 和显示操作未专项验证。这些启动采样不代表完整学习、无线共存或所有任务的运行预算通过。主机没有挂载 SD 卷，本次未安装新课程正文或覆盖 SD 存档。

私有证据位于 `.build/dependency-upgrade-device-20261029T093541Z/`，权限 0700 且已忽略，包含完整备份、回退应用、`RECOVERY.md`、命令日志、脱敏启动日志、结构化汇总与摘要。恢复写入未演练；完整过程见[依赖升级部署备忘](../memos/dependency-deployment.md)。

## 刷机模式命名更新

中文菜单、确认页、准备页及相关错误提示统一使用“刷机模式”；ROM 最终提示仍为 `Download Mode` / `Keep USB connected`。此次只调整文案及对应测试预期，保存、网络清理、显示确认与重启契约保持。

最新验证在 `.build/flashing-mode-rename/`：`CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/download-final/pages" sh tools/build.sh test` 和 `CARGO_NET_OFFLINE=true sh tools/build.sh build` 均退出0，53项Rust及完整C测试链通过。六个相关View核对通过，菜单、确认页和错误页的生成PBM已检查，无缺字或截断。普通s BIN为3142560 B，原factory余3168 B，SHA256为 `c622ea98e94ba953a590f3a74d0e91ae39f47256febf119f873e39cb1486ce46`；后置契约通过，保留DIO/40MHz/4MB及65536主栈。本次未重新烧录设备，下节的s/z尺寸与部署记录对应改名前的冻结存档修复候选。

## 当前存档保护验证

最终功能审查发现两项 P1：唯一有效槽启动时读取失败，或自定义卡组未读入而内置回退删除原档中的稳定 ID，后续保存可能覆盖原进度。当前源码在 SD 未挂载时由平台阻断全部保存；执行恢复时，根目录不可用、任一槽 IO/内存错误或最高有效档 ID 无法解释会设置模型会话保护，拒绝全部导出并清除 pending；保护跨 mutation、重置和 C storage 重新 init，迟到 ack 无效。只有成功恢复有效且可解释的档才解锁。确认 CRC/语义/长度无效的槽仍可降级，成功导入科目的合法删除仍可迁移；不更改存档格式，详见[双槽存档](learning-app.md#双槽存档)。

证据根 `.build/storage-protection-20261028T012320Z/` 保存原代码、最终代码、最小差异、摘要和退出码。旧实现两项 Rust 回归退出101，真实 storage/ABI/runtime 故障回归退出134，修复后以下命令均退出0：

```sh
CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/download-final/pages" sh tools/build.sh test storage_protection_
CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/download-final/pages" sh tools/build.sh test
CARGO_NET_OFFLINE=true sh tools/build.sh build
CARGO_PROFILE_RELEASE_OPT_LEVEL=z CARGO_NET_OFFLINE=true sh tools/build.sh build
```

默认 s 构建明确清除了进程级优化覆盖，完成并冻结后才构建 z，生产清单仍为 s。最终完整宿主为53项Rust、19项平台、31项真实Rust ABI/runtime、17项网络、8项IDF适配、8项manager、1项通知及14组事件集成；广告及真实NimBLE mempool检查通过，2561次分配/释放配对。实际 storage + ABI + runtime 覆盖唯一slot0读取IO，以及自定义卡组缺失、坏JSON、读取IO四种故障；清除故障并重新init后，自动、显式与关机保存仍失败，原slot0逐字节不变且不创建slot1；修复后新启动恢复完整Snapshot。未直接注入malloc失败；NO_MEMORY走同一保护分支，不能称该故障已独立实测。

冻结普通 s/z BIN分别为3142496/3054800 B，原0x300000 factory余3232/90928 B；两份重新通过完整后置契约，保持DIO/40MHz/4MB、65536主栈、原分区及无线/Security1配置，无autotest符号。静态DRAM均171745 B，链接表剩余149551 B，不是可用heap。z的BIN SHA256为 `d94ba1ed44f29782864e02eabbdbe157c50fa378a93582be31c25a593899b584`。冻结源码、构建与镜像摘要分别在 `source-manifest.json`、`build-{s,z}.json`、`build-{s,z}/artifacts.json`。

45份冻结View加独立保存错误提示共46张PBM，按冻结输入重渲染46/46字节一致。当前模型同名页与旧download-final基准39/46一致；7张配网页差异来自本任务之前的分阶段布局和6位PoP，逐View字段差异保存在 `pbm-comparison.json`。本次未修改页面渲染与字体。最高有效档含合成未知ID的压力夹具现验证保护失败，不作为成功恢复的内存峰证明。

当前存档修复已通过独立复核，两项原 P1 已清除，未发现新的阻塞问题。复核逐字节确认9个工作区文件与冻结源码一致，并核对红绿回归、完整宿主日志、s/z镜像及46/46冻结输入PBM；结论限于所列文件和测试场景，不把源码检查当作真机故障注入。额外运行 `CARGO_NET_OFFLINE=true sh tools/build.sh test-autotest` 退出0，包含快照及phase1/phase2宿主集成；不代表新增设备生命周期结果。

部署证据冻结在 `.build/storage-protection-device-20261028T014614Z/`：再次核对同一C3 rev0.4、16MB Flash、安全flags为0及当前分区后，仅在0x10000写入上述3054800 B普通z应用，独立verify-flash退出0；bootloader、分区和NVS未列入写入命令。20秒启动观察记录一次ROM启动，SD挂载及首屏display-end通过，未见崩溃、重启或故障标记；该时刻free/minimum/largest/stack_free为110816/80784/102400/29796 B。运行时NVS未做前后字节比对，不能声称内容未变。设备未注入存档IO或卡组故障，不能据启动通过称新存档保护已完成真机可靠性验收；真实SD掉电、物理键序、关机唤醒、6位官方iPhone交互和本候选完整无线内存仍待验证。

## 当前分阶段配网验证

本节的6位PoP源码和设备记录先于存档保护修复；其后续候选与当前审查状态以上节为准。

当前源码在原分阶段面板上将PoP改为每会话随机6位ASCII数字（含前导0），保留Security1、非空PoP和原300秒生命周期；仅缩短PoP并同步生成、ABI、显示及测试契约。以下尺寸、峰值与逐帧比较均为原16位PoP阶段的历史证据。本次6位候选的离线验证见本节末尾。

此前源码包含拒绝 START 时推进一次 revision 的 P1 修复、固定面板节点精简及最终显示方向修正。P1 独立复核通过，重复拒绝仍同步所有权/故障状态，空闲 step 不持续刷新。节点精简后，同口径宿主 render 峰从78495 B降至49877 B，最大请求22672 B；81张真实 ABI 最终帧逐字节一致，活跃 overlay 更新仍零分配。完整宿主及 release s/z 验证已通过；方向修正后再次完整宿主通过，83张逻辑 PBM、72份 View 和15项生成物保持一致。证据分别在 `.build/staged-panel-compact-20261027T123348Z/`、`.build/rust-opt-ab-20261027T130311Z/` 和 `.build/display-orientation-20261027T134735Z/`。

方向修正版默认 s 为3141936 B、原3MiB factory余3792 B；phase2 z 为3061104 B、余84624 B，两者均通过原尺寸及完整后置契约。生产清单仍为 s，z 仅用进程环境覆盖；未改分区、安全协议、无线栈、主栈或内容/字体限额。原4640 B及节点精简后3296 B超限属于历史 phase2 s，不是当前 phase2 z 状态。宿主 z 完整渲染耗时增加，尚不能推算 C3 性能或据此切换生产默认。

真机自动生命周期证据为 `.build/z-phase2-device-20261027T132805Z/` 及方向修正版 `.build/display-orientation-device-20261027T140747Z/`，后者应连同 `REVIEW-CORRECTIONS.md` 阅读。修正版预热＋3轮共4代 start/stop/END/deinit/runtime release 各1次；仅 start/deinit/runtime release 返回0，stop 为 void，END 是事件。自然清理由 END、deinit/release 和 clean 状态共同证明；未观测 OOM、分配失败、panic、watchdog 或显示故障。四次稳定 Home 的 INTERNAL|8BIT free/largest 均为104348/29696 B，主任务高水位未用栈29732 B，累计内部8bit最低空闲21876 B；这些采样不覆盖全部瞬态峰值或其他任务栈，不构成长期无泄漏或全面 OOM 结案。

cycle0/1 取消后先回 Settings，再由 Home 键完成显示推进；cycle2/3 的退出显示推进仍属配网页，最终 Home 由后续完整 render 至 display-end、idle、Home/clean/digest 检查及稳定2秒门槛共同证明。最终 Home 的具体 revision 和独立 ack 时间未输出，不补造数值。方向修正版部署后，用户已确认屏幕方向正确；此为后续新增事实，旧冻结报告中“方向待确认”保留其历史时点。

上述设备验收仅覆盖该候选、该设备、该轮自动生命周期。esptool 历史操作只写 app，未写 bootloader/分区/NVS；运行时 NVS 是否变化没有前后比对。phase2 会自动退出配网，不适合手机手动验收。旧16位PoP普通 z 候选 `.build/ios-provisioning-candidate-20261027T153436Z/` 已独立 Review、由主 Agent app-only 部署并校验，启动证据为 `.build/ios-normal-device/`；用户随后确认官方 iPhone 配网成功。该确认只覆盖旧候选成功流程，失败、取消、超时、完整退出/重开、空中广播停止、物理按键、完整内存预算及性能仍未验收。本次6位PoP候选已独立 Review，通过app-only写入与独立Flash校验并正常启动；6位手机交互仍须按[官方 iOS 路径](ble-provisioning.md#官方-ios-操作路径)验证。

本次6位PoP离线证据冻结在 `.build/pop6-20261028T004400Z/`。`CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/download-final/pages" BALABALA_UI_MEMORY_OUT="$PWD/.build/pop6-20261028T004400Z/ui-memory" sh tools/build.sh test -- --nocapture` 退出0：50项Rust、19项平台、30项真实Rust ABI/runtime、17项网络、8项真实IDF适配（含15启动/8清理故障点）、8项manager、真实通知/广告函数及14组事件集成通过，真实NimBLE仍为2561次分配/释放配对。45份冻结View及额外保存错误提示共46张PBM逐字节一致。真实IDF生成路径使用可控随机源，覆盖000000、999999、最后可接受值4293999999，以及4294000000/UINT32_MAX拒绝后重采样；Rust ABI拒绝5/7/旧16位、非ASCII数字和单侧空身份，双空清除、重开和原显示门禁保留。首次使用早期34页基准导致两项字体用例因基准不完整失败，原日志保留在 `attempt-1/`，未修改测试门禁或历史基准。`CARGO_NET_OFFLINE=true BALABALA_AUTOTEST_OUT="$PWD/.build/pop6-20261028T004400Z/autotest" sh tools/build.sh test-autotest` 退出0，含Rust autotest快照、phase1/phase2真实App/runtime宿主回归；不代表新增真机结果。

`CARGO_PROFILE_RELEASE_OPT_LEVEL=z CARGO_NET_OFFLINE=true CARGO_TERM_VERBOSE=true sh tools/build.sh build` 退出0，默认清单仍为s。普通z BIN为3054416 B，比旧普通z小32 B，原factory余91312 B；SHA256为 `4381aee3864f53a5c5ac43af4f07231fd4375a20d7a2bd4dc73a8f4a19edc178`。冻结镜像再通过完整后置契约：DIO/40MHz/4MB、原分区、65536主栈、原无线与Security1配置保留，sdkconfig/bootloader/Cargo清单及锁/15项生成物与旧候选一致，普通镜像不含autotest或phase2分配诊断。83张当前模型PBM中70张不变、13张仅原PoP矩形变化；81张真实ABI帧中45张不变、36张仅该矩形变化，区域外逐像素不变。活跃overlay更新继续零分配；宿主峰值不推导C3预算闭合。新旧源码diff、逐文件摘要、镜像配置与PBM比较分别见证据根的 `source-vs-old-candidate.diff`、`source-final.json`、`image-comparison.json` 和 `page-comparison.json`。独立Review已通过，部署证据冻结在 `.build/pop6-device-20261028T005729Z/`：再次核对同一C3 rev0.4/MAC、16MB Flash、安全flags为0、当前分区与旧部署一致后，仅写入地址0x10000的3054416 B应用镜像，独立verify-flash退出0；未向esptool写入清单加入bootloader、分区或NVS。启动观察20秒，SD挂载及首屏display-end通过，记录一次ROM启动且未见崩溃/重启；首屏display-end的free/largest/stack_free为110816/102400/29796 B。该采样不证明完整内存预算；运行时NVS未做前后字节比较，6位官方iPhone配网仍待用户确认。部署后的文档事实另行记录，不修改离线候选的冻结源码和历史报告。

## 分阶段配网初版软件验证（历史）

以下记录初版节点与 phase2 s 的失败及当时待 Review 状态，保留作历史证据；当前状态以上节为准。

固定面板和状态表见[分阶段配网契约](staged-provisioning.md)。正常返回/Home/关机须等待明确资源释放；清理失败且可能驻留时保留固定面板与按键响应，撤销关机、锁止重开，暂停完整 AOT 和自动保存。dirty 保存失败在无线启动前进入可退出错误页且保留学习状态；clean 不强制写 SD。下载入口也在任何保存前检查网络资源已释放。下述历史“清理故障后继续学习”不再是当前契约。

本轮证据根为 `.build/staged-provisioning-20261027T105525Z/`，包含源码/基线差异、布局与生成物、锁定补丁重放、PBM、内存记录、双构建配置/ELF/map/bin、原始日志和 SHA256 manifest。没有 Git 元数据，差异以本次修改前的 94 文件快照为准，不表示 Git HEAD；历史证据未覆盖。

| 验证 | 结果与边界 |
| --- | --- |
| `sh tools/build.sh test` | exit 0；48 项 Rust、19 项平台、27 项 runtime、17 项网络、7 项 IDF（15 启动/8 清理故障点）、8 项 manager、真实通知/广告函数及 14 组事件集成通过；原真实 NimBLE 2561 次分配/释放配对 |
| `sh tools/test-autotest.sh` | exit 0；phase1 和 phase2 均通过，保留每代计数、ack/清理门禁及原内存阈值 |
| Rust `cargo test --locked --manifest-path .build/native/Cargo.toml --features autotest --lib autotest::` | 补齐测试专用 App 初始化后 exit 0，1 项通过/48 过滤；初次编译失败日志保留 |
| 固定面板定向行为测试 | exit 0；最终补充空身份首帧、全部 9 状态、身份擦除与区域边界，活跃更新 peak/最大请求均为 0 |
| 冻结 View 输入 PBM 比较 | 34 张逐字节一致；当前模型同名页面 32/34 一致，只有 `network.pbm` 和 `settings.pbm` 为预期变化 |
| 四个 IDF 补丁从固定 commit 重放 | before/patch/after SHA256 全部匹配；包含 generation 绑定的真实广告观察补丁 |
| `sh tools/build.sh build` | exit 0；默认 BIN 3143344 B，原 3145728 B factory 剩余 2384 B |
| `BALABALA_AUTOTEST_PHASE=2 BALABALA_AUTOTEST_OUT=<证据根>/phase2 sh tools/build.sh build-autotest` | exit 2；链接及 BIN 生成完成，尺寸检查失败：3150368 B 超原 factory 4640 B；标准后置构建契约校验未执行 |

默认和 phase2 的有效配置及 bootloader/app 镜像头均保持 DIO/40MHz/4MB，原 factory `0x10000`/`0x300000`、主栈、内容/字体限额、无线配置和协议未降低。phase2 不算构建通过；按主 Agent 决策停在可审查阶段，不自行删除功能或改分区。只读符号/节比较显示 phase2 相对默认 BIN 增加 7024 B，其中 `.flash.text` 增加 5920 B、`.flash.rodata` 增加 668 B；详细列表在 `flash-comparison.json`。

新固定面板的宿主完整 render 峰最大 78495 B（最大请求 37496 B），含近限额内容的完整生命周期峰最大 121680 B。空身份/真实身份准备帧 render 峰分别为 74841/77396 B，drop 后净新增驻留为 0；两帧不能当作同一峰相加。原历史碎片假设下的 81 个敏感性样本全部拒绝。新面板峰明显高于旧面板历史值，宿主分配器和指针宽度不代表 C3；未证明第二准备帧、manager、Security 1、连续块或栈预算闭合，不宣称 OOM 已解决。

本轮没有访问设备或烧录。独立功能 Review、phase2 Flash 方案及后续实现/回归仍待主 Agent 安排；物理擦除、无线资源归还、官方 iPhone 端到端和动态内存仍未验证。底层 BLE stop 永不返回、持续显示失败仍是明确限制。

## 刷机模式软件验证（历史）

家长第五项、默认取消的二次确认、白底两行英文提示及严格提交顺序见[刷机模式](download-mode.md)。`BALABALA_COMPARE_PAGES="$PWD/.build/ble-baseline/pages" sh tools/build.sh test -- --nocapture`：37项Rust、19项C输入/存储、25项runtime、16项网络、5项IDF适配、8项manager、1项通知及14组事件集成通过；原34张冻结View输入PBM逐字节一致；当前模型32/34一致，`network.pbm` 既有BLE身份与 `settings.pbm` 第五项为允许变化。`sh tools/build.sh build`通过C3、分区和尺寸检查，DIO/40MHz在源码默认、生成sdkconfig和镜像头核验，4MB镜像/原分区保持。原48px覆盖与导入范围保留、全部13种固定错误字体覆盖通过。

新证据冻结在 `.build/download-final/`，不覆盖 `.build/ble-final-*`。未访问设备或烧录；独立Review、实际eFuse/ROM/USB枚举、屏幕保持、正常复位恢复与动态heap/栈尚未完成。持续显示故障不保证错误可见；底层BLE stop永不返回仍是既有限制。

下述段落保留更早 BLE 阶段的历史测试、尺寸与 Review 证据，当前分阶段配网结果与契约以前节为准；下载流程设计见[刷机模式](download-mode.md)。

测试须验证行为，不能用参考项目测试或编译结果代替本项目证据。本轮在项目根目录执行 `python3 tools/test-build-env.py`（3 项）与 `BALABALA_COMPARE_PAGES="$PWD/.build/ble-baseline/pages" sh tools/build.sh test -- --nocapture`：33 项 Rust（无过滤）、19 项 C 输入/存储、17 项 C 运行时、16 项 C 配网控制器、5 项真实 IDF 适配 mock、8 项真实补丁 manager 函数、1 项真实 IDF 通知函数及 14 组完整事件循环/protocomm 全部通过。C 用 ASan/UBSan，runtime 连接真实 Rust ABI。冻结 View 输入的原 34 张 PBM 逐字节一致；当前模型产物与原基准为 33/34 一致（`network.pbm` 配网页预期变化），另新增 6 张配网页验证字形、设备名与 16 位 PoP。`sh tools/build.sh build` 通过当前 C3 构建、保留内部人工诊断符号及尺寸检查。精确命令/退出码、当前固件 SHA256 与证据路径见 [BLE 配网](ble-provisioning.md)，原有阶段记录见 [平台运行时](platform.md) 和 [学习应用](learning-app.md)。AGENTS.md 未修改，交叉构建不等同于硬件验收。

BLE 验收覆盖成功后手机查询窗口、成功前 300 秒超时、截止点成功优先/迟到成功拒绝、取消/退出/关机、错误密码/AP 失败、旧配置 RAM 备份与恢复、15 个启动与 8 个清理故障点、重开与旧 generation、线程并发事件风暴不丢 END。新增 SUCCESS+CANCEL/SHUTDOWN（含 END）保留新配置、SUCCESS/FAIL 两顺序与同批语义、成功 Front4 退出关闭入口、5/6→4 恢复/释放错误经真实 Rust ABI 呈现、STA 底层三步无 abort 失败回收。

真实补丁函数使用 mutex/condition barrier 与 3 秒期限验证 phone/autostop 同路径停止、事件循环锁反转、BLE host 排空、重入 stop/deinit、私有投递连续失败后重试、END 观察副本丢弃仍完成、同步 END 后立即 deinit/init/start、旧栈和已拷 timer 回调不影响新会话。真实 IDF 四函数验证满队列七处观察通知不等待，保留协议状态/返回与会话处理；密码学 API 为 mock。项目从主任务安全点调用生命周期；同步终态桥和默认事件回调只归并无凭据位，显示 service 不做生命周期调用。恢复/清理失败报告失败并锁止重开；NVS 未加密，断电期间不保证旧凭据回滚，不自动 erase NVS。

本轮全量包含 14 组完整事件循环/真实 protocomm（实例 7 + transport 7），并执行全部 33 项 Rust，覆盖窄关机故障 ABI。三个注册 OOM 先注销、成功排空在途 endpoint 请求后才释放表/pc/PoP；停止返回错误时保留所有权且不 END/不自动重试，显式 deinit 一次只重试一次。真实 ABI 验证清理故障撤销关机、显示既有失败页、不发 START、保持输入/学习，手工清理后不重放旧关机。覆盖 end_pending 的正常/已 END/未启动及纯 SESSION_ONLY 计数，满队列观察失败不影响释放成功返回。`balabala_network_retry_cleanup()` 仅内部主线程人工诊断，不绑定按钮，成功后仍锁止本次开机配网；最终 ELF 已保留此符号。底层停止若永不返回仍未解决。最终产物与逐文件摘要位于 `.build/ble-final-firmware/`，验收摘要位于 `.build/ble-final-evidence.json`。

## 墨水屏局刷待验收

第 3 阶段沿用控制器 0x26 RAM 保存旧帧，符合条件的同页更新请求局刷（不做像素差异或面积筛选）；首帧、换页、配网面板、刷机准备/最终提示和错误页继续全刷。驱动使用 DF，连续 8 次局刷后补一次全刷，失败/休眠使基准失效；没有新增整帧缓冲。固定上游核对与本轮离线验证日志保存在 `.build/partial-refresh-20261028/`。

本轮完整离线宿主测试通过：Rust 63 项、C 平台 19 项、runtime 36 项及后续网络/真实 IDF 集成检查通过；日志包含 `Frozen exact-input PBMs compared: 46; all byte-identical`。默认 s 的普通 C3 构建通过，BIN 为 1921232 B，构建契约确认 autotest=OFF、DIO/40MHz/4MB、主栈 65536 B 及镜像分区限额。

本轮不连接设备或烧录。真机待测项包括菜单移动、翻卡、答题反馈的实际耗时（目标约 0.5s）、连续操作残影与周期全刷清理效果、初始化边框 0x3C=01 对局刷的影响、0xDF 与 0xFF 的取舍、连续局刷上限 N=8 的标定，以及失败恢复/休眠唤醒后的首帧全刷。宿主传输序列与 PBM 一致不能替代这些物理结果。

## 真机验证边界

当前已完成指定候选的自动生命周期诊断，用户确认修正版屏幕方向正确及旧16位PoP普通候选官方iPhone配网成功；6位候选也已审核部署并通过启动观察，尚未完成其手机交互及全面真机验收。以下项目不能由这些结果或宿主测试推导通过：

- 刷新时间、残影及完整中文显示效果。屏幕方向确认不代表实体键序、全部页面视觉或显示性能通过。
- 物理按键与 ADC 映射、去抖、关机、唤醒和恢复路径。
- SD 卡读写稳定性、断电恢复、BLE / Wi-Fi 并行工作的完整瞬态内存峰值及其他任务栈。
- 新6位PoP的乐鑫官方 iOS 客户端交互、失败重试和空中广播关闭；旧16位候选成功流程已有用户确认。
- 功耗、续航、性能及长期运行内存。已有 free/largest/主栈样本只覆盖本轮自动场景。

硬件验收清单见 [research.md](research.md#首个技术关卡)。上机前先核实设备型号、USB 锁定状态、备份及恢复路径。

## 阻塞处理

工具链、MicroTS 可行性、官方 iOS 协议、许可证或缺失凭据影响交付时，记录可复现证据和选项，停止受阻路径并请求决策；继续不依赖该决策的工作。不得自行更换技术路线或以占位实现宣称完成。

历史完整事件循环的 14 组保留实例 7 组验收：两个 stop 来源的同步 END 重开/第二轮停止、满队列禁止分配注销、实例身份/重复注销/legacy 隔离/派发与 profiling 尾回收、三个注册失败回滚及逐次分配基线、真实锁屏障、注销异常受控 fatal、40 轮 loop 删除重建。END 的契约为资源已释放、三个订阅已逻辑失效；物理节点当次派发尾回收，支持同 loop 同步重开。不能在回调里销毁当前运行 loop。独立局部复审已解锁并通过最终全量宿主/PBM/C3；本轮仅新增 CMake 符号保留和验证文档，仍需主 Agent 核对最终证据并同步 AGENTS。详细命令和范围见 [配网证据](ble-provisioning.md)。

## mono 字库第 1 阶段验证（2026-10-28）

本轮将 24px（5 号槽）和 48px（14 号槽）的 DCFA 字档改为每像素 1 位。DCFA VERSION 3、头部/cmap 与字形度量保持，头部 byte 13 增加 `FLAG_MONO1 = 2`；density=1，每行按 `ceil(coverage_w / 8)` 字节填充，高位在左，原覆盖度 `>=128` 为 1。mono 解析/烘焙拒绝其他密度，动态 streaming 显式拒绝，detach 保留；旧 8bit 路径继续可用。实现、格式常量和烘焙代码保存在第四项[可重放补丁](../patches/pocketjs-mono-font-atlas.patch)，补丁 SHA256 为 `04040d5688608d8647344d1c6a319b8270c773dabf429765a36a511d3c7ef2b0`，逐文件 before/after 链见 [manifest](../patches/manifest.json)。本阶段未改页面、View、状态机、文案或内容/字体限额。

证据目录为 `.build/mono-font-20261028/`。修改前先运行完整宿主基线，并冻结原 assets、两份 8bit 字档和 s BIN；`before/` 保存本轮改动前文件，`frozen-inputs/` 保存相同的 46 张参考 PBM 和 45 份 View 输入。原源码快照保存在仓库外且未覆盖；工作区没有 Git 元数据，未暂存或提交。

| 命令 / 检查 | 退出码与关键结果 | 证据 |
| --- | --- | --- |
| 修改前 `sh tools/build.sh test` | 0；53 项 Rust 及完整 C 测试链通过 | `baseline-test.log`、`baseline-test.exit` |
| `python3 .build/mono-font-20261028/replay-check.py`（从固定原始归档恢复全部补丁目标，再执行 `python3 tools/apply-patches.py`） | 0；四项补丁按顺序重放，全部 before / patch / after 摘要匹配，再次执行入口的幂等检查通过 | `patch-replay.log`、`patch-replay.exit`、`replay-check.log`、`replay-check.exit` |
| `CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/download-final/pages" sh tools/build.sh test -- --nocapture` | 0；56 项 Rust，无过滤；46 张相同冻结输入 PBM 逐字节一致 | `mono-test.log`、`mono-test.exit` |
| 同步上游 raster 测试夹具后，`CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/download-final/pages" sh tools/build.sh test` | 0；最终完整测试及 46 张冻结 PBM 比较通过，另核对冻结输入数量与来源 | `mono-final-test.log`、`mono-final-test.exit`、`final-page-check.json` |
| `CARGO_NET_OFFLINE=true CARGO_HOME="$PWD/.tools/cargo" RUSTUP_HOME="$PWD/.tools/rustup" CARGO_TARGET_DIR="$PWD/.build/mono-font-20261028/raster-target" rustup run 1.99.1 cargo test --manifest-path "$PWD/.tools/pocketjs/engine/raster/Cargo.toml" --test driver` | 101；上游完整 workspace 解析依赖时因未缓存的 `rust-psp` git 依赖而停止，未到编译；独立上游 driver 集成测试未验证 | `raster-driver.log`、`raster-driver.exit` |
| `sh tools/build.sh build` | 0；普通 s；配置、bootloader/app 镜像头、原分区及尺寸检查通过 | `build-s.log`、`build-s.exit`、`s/` |
| `CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build` | 0；普通 z；相同完整后置契约通过，默认清单仍为 s | `build-z.log`、`build-z.exit`、`z/` |
| `.tools/bun/bun .build/mono-font-20261028/bake-check.ts` | 0；默认 8bit 重烘焙与原两字档逐字节一致；mono 全部 1463280 个源像素按阈值打包无差异；density=2 被拒绝 | `bake-check.log`、`bake-check.json`、`bake-check.exit` |

最终完整宿主链包含 56 项 Rust、19 项平台、31 项真实 Rust ABI/runtime、17 项网络、8 项 IDF 适配（15 启动 / 8 清理故障点）、8 项 manager、1 项真实通知及 14 组事件集成；广告检查及真实 NimBLE mempool 回归通过，2561 次分配/释放配对。日志关键输出为 `Frozen exact-input PBMs compared: 46; all byte-identical`。首次完整 mono 回归也通过，保留为 `mono-test-first.*`；随后在同源渲染测试增加明确的逐像素阈值断言，再执行相同完整命令，Rust 56/56 通过，耗时 64.06s。最后仅为上游 raster 测试三个旧 8bit 字面量补 `flags: 0`，重新清洁重放四项补丁，并再次运行无 `--nocapture` 的完整入口；该次结果见 `mono-final-test.log`：Rust 56/56 通过，耗时 63.48s；冻结页比较用例通过，`final-page-check.json` 记录全部 46 张基准、45 份 View 输入及证据副本校验。

新增三个行为用例位于 [test-font-flash.rs](../tools/test-font-flash.rs)：解析/长度/所有截断点拒绝、非整字节宽度与行步长、保留源 gid 的 static 子集及重排 gid 的 owned 子集；同源 0/127/128/255 覆盖度在黑字白底和白字黑底下经真实 RGB565 blend 与 `pack_monochrome` 等价，并断言具体阈值像素；mono 密度/动态 streaming 拒绝及 detach 保留 Flash。旧 8bit 密度及 streaming 回归继续通过。合成碎片实验仍用显式解压的历史 8bit fixture 触发 16800 B 分配失败，mono owned 最大请求 2100 B、borrowed 最大请求 40 B 均成功；该实验不是 C3 allocator 重放。

| 字档 | 字形与 cell | 原 8bit 字节 | mono 字节 | 减少 |
| --- | --- | ---: | ---: | ---: |
| 5 号槽 / 24px | 606 / 24×35 | 513904 | 68494 | 445410 |
| 14 号槽 / 48px | 284 / 48×70 | 956528 | 121568 | 834960 |
| 合计 | 890 字形 | 1470432 | 190062 | 1280370 |

| 镜像 | BIN 字节 | 原 3MiB factory 剩余 | SHA256 |
| --- | ---: | ---: | --- |
| 修改前普通 s | 3142560 | 3168 | `c622ea98e94ba953a590f3a74d0e91ae39f47256febf119f873e39cb1486ce46` |
| mono 普通 s | 1862336 | 1283392 | `a3a586af18f9e6ac78d285d5fad0d6529f68fd35152286745fb6e3ae0f814cf8` |
| mono 普通 z | 1774624 | 1371104 | `9313a5f1ad3164a7d6c29aaf984791954e049eb69fd338527796422c0da4aa35` |

s BIN 净减少 1280224 B；字体物理字节减少 1280370 B，BIN 同时受代码与对齐影响。mono z 比同轮 s 小 87712 B；本轮未重新构建修改前 z，不以历史 z 代替同轮基线。两档保持 DIO/40MHz/4MB、原 `0x10000` / `0x300000` factory、65536 B 主栈及原无线 / Security1 配置，普通镜像关闭 autotest。各档 ELF、map、BIN、配置、bootloader、分区与 `size.json` 保存在 `s/`、`z/`；原/新字体体积分别见 `baseline-assets.json`、`mono-assets.json`。最终同步仅涉及未由项目入口编译的上游集成测试夹具和补丁/文档，`artifact-source-check.json` 确认所有生产改动源文件与两档构建时字节相同，因此未重复 s/z 构建。

每页字体逻辑预算继续使用 `16 + g × (8 + cellWidth × cellHeight)`，原 83807 B 门禁及最大内置页 60072 B 计费保持；真实 Flash 大小、cmap heap 与逻辑预算分开记录。本轮没有访问设备、烧录或新增真机结果，未验证 mono 在 C3 上的实际显示时延、动态 heap / 连续块、栈高水位和长期运行；已有历史设备记录仅适用于其原候选。独立复核由主 Agent 组织，本记录不把实现自查写成独立 Review 通过。

## UI 重写第 4 阶段素材与排版管线（2026-10-28）

本轮只建立新模板所需的素材、字号与排版契约，状态机仍输出旧页面；新首页、选一选和认一认正面仅由宿主测试用构造 View 渲染，交互未切换。

- 坐标唯一来源为 [tools/ui-layout.ts](../tools/ui-layout.ts)：构建期替换 `native/App.vue` 中的 `$名称` 占位（未知名称直接失败），同时生成叠加层使用的 Rust 常量。
- 字号槽：5 号 24px Regular、6 号 32px Regular、14 号 48px Regular（仅旧模板，迁移完成后删除）、15 号 48px Bold；32px 与 48px Bold 覆盖 24px 全部字形，任何可导入字符都能排版。生成的 `font_text` 按实际选中的字号分支取字，不再按动态 class 所有分支的并集加载。
- [native/src/fit.rs](../native/src/fit.rs) 统一字号选择与均衡折行：题目 48px Bold→24px；选项四项共用一档 48px Bold（≤2 行）→32px（≤3 行）→24px（≤5 行，最后才逐字拆词）；认一认正面先试特大字 176px / 112px，再退回 48px Bold。导入校验调用同一函数，原导入限额不收窄。
- [tools/sprites.ts](../tools/sprites.ts) 在构建期用 `@resvg/resvg-js` 把 `native/art/*.svg` 原创线稿和 Noto Bold 特大字轮廓栅格化为 1bpp（50% 阈值），特大字按墨迹外框裁剪；叠加层直接从 Flash 绘制，不复制到 heap。

| 命令 / 检查 | 结果 | 证据 |
| --- | --- | --- |
| `CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/download-final/pages" sh tools/build.sh test -- --nocapture` | 0；Rust 71 项及完整 C 测试链通过；46 张冻结 PBM 逐字节一致 | `.build/redesign-phase4-20261028/test.log` |
| `CARGO_NET_OFFLINE=true sh tools/build.sh build` | 0；普通 s，BIN 2472384 B，原 3MiB factory 剩余 673344 B；构建契约通过 | `build-s.log` |
| `python3 tools/test-build-env.py` | 0；安装与环境隔离检查通过（新增 resvg 安装段与 Noto Bold 校验） | `test-build-env.log` |

宿主门禁 `redesign_templates_gate` 覆盖 85 个新模板 View：重复渲染逐字节一致；叠加前的文字墨迹只落在布局区域内且有字的区域必有墨迹；首页焦点切换只改变焦点块，切回逐字节复原；新模板宿主 render 峰值 34460 B，不高于同次测得的旧模板 36166 B，最多 15 个投影节点。内置内容中英语选项 32px 17 题、24px 3 题，其余三科全部 48px Bold；认一认正面 176px 73 张、112px 7 张。字档合计 578556 B，位图素材 198356 B。

字体逻辑预算公式与 83807 B 门禁本轮未改，按位深重新标定留到页面迁移完成后。本轮未访问设备或烧录，真机 heap、连续块、栈高水位与显示耗时未验证。

## UI 与 UX 重写验收

本次候选已切换真实模型与按键流程，覆盖首页、四科学科选择、认一认正反面、四列选一选、答对/答错反馈、轮末总结、家长设置、学习情况、重置确认、刷机确认与准备、配网固定面板、错误提示和休眠页。首页五个入口无需滚动；家长区仅在首页按住返回2秒进入。坐标暂按四等分 `60/180/300/420`，实体键位待核对，后续只需调整集中几何源。

采用 MicroTS 文字与 Rust 单色叠加层的路线：24px Regular、32px Regular、48px Bold，认一认正面优先176/112px。四个选项共用能完整排下的最大字号；按当前186px选项区及64/46/36px行距，可容纳2/4/5行。32px覆盖英语17题，24px覆盖3题，其余三科均48px Bold。23幅原创SVG线稿及特大字在构建期生成1bpp素材，直接从Flash绘制；字体档案合计587400 B，素材216040 B。48px Regular槽仍服务于刷机最终提示和旧View兼容，其删除计划已取消。刷机最终提示继续保留原两行英文和原像素输出。

本次新模板按mono位深计算字体逻辑构造预算，仍执行83807 B门禁；旧冻结模板使用原8bit等效计费，以维持同输入对照。四科活动内容32768 B、48000 B帧缓冲、7680 B条带和65536 B主栈配置均保留，未通过缩小限额换取构建通过。同页状态变化可请求局刷，网络/刷机/错误/休眠及跨页强制全刷；驱动基准失效或连续8次局刷后回全刷。局刷参数与周期尚需设备标定，不能据宿主命令序列测试宣称耗时或残影达标。

最终证据目录为 `.build/ui-redesign-final-r2/`，包含原始日志、590份View及独立保存错误帧、PBM摘要、s/z固件与配置。`preview/index.html`可按类别浏览冻结页面，`preview/pages.png`提供主要页面总览；生成物均位于忽略目录。冻结来源是本次完整测试的输出，每个View均有对应PBM，额外的诊断图片不混入可重放基准。

| 命令 / 检查 | 结果 | 证据文件 |
| --- | --- | --- |
| `CARGO_NET_OFFLINE=true sh tools/build.sh test -- --nocapture` | 退出0；76项Rust及完整C测试链通过 | `test.log` |
| `CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/ui-redesign-final-r2/pages" sh tools/build.sh test compare_frozen_page_inputs_when_supplied -- --nocapture` | 退出0；591张冻结输入PBM逐字节一致；同时运行完整C测试链 | `frozen-compare.log` |
| `CARGO_NET_OFFLINE=true sh tools/build.sh test-autotest` | 退出0；诊断快照及phase1/phase2宿主流程通过 | `test-autotest.log` |
| `CARGO_NET_OFFLINE=true sh tools/build.sh build`（已清除优化覆盖） | 退出0；默认s，后置契约通过 | `build-s.log`、`build-s/` |
| `CARGO_PROFILE_RELEASE_OPT_LEVEL=z CARGO_NET_OFFLINE=true sh tools/build.sh build` | 退出0；普通z，后置契约通过 | `build-z.log`、`build-z/` |

C测试包含20项平台、40项真实Rust ABI/runtime、17项网络控制器、8项IDF适配、8项补丁manager、1项通知及14组事件集成；使用ASan/UBSan，密码学、无线及RTOS边界仍为替身。按键门禁覆盖首页长按起点、松开后确认及跨页长按不得穿透；学习流程保留翻面/跳过不评分、单题单轮只评分一次、鼓励页主动翻题、退出和恢复。配网保留两次精确revision门禁、活跃期零AOT/零保存与更新零分配测试。

`all_redesigned_pages_have_bounded_text_and_heap` 覆盖431个真实新页面View，验证文字墨迹边界、重复绘制及heap；同口径render峰42444 B，低于51166 B门限。该数值只覆盖render片段。完整Model/View/render生命周期的近内容限额样本峰141821 B、最大请求48000 B，是64位宿主合成arena的请求口径，不包含C存储、IDF任务栈、无线和真实分配器开销，也不是全部合法内容的上界。591张新基准的一致性证明可重复输出；新页面与旧设计存在预期差异，不宣称全部旧页面逐字节相同。

| 普通候选 | BIN字节 | 原0x300000 factory剩余 | BIN SHA256 |
| --- | ---: | ---: | --- |
| 默认s | 2518176 | 627552 | `68e312b78b4c55d67fa6557f5b9460a02a0f2eabbfc99a495859049f5c82b893` |
| 进程覆盖z | 2425296 | 720432 | `6b42b3871795ff92a0866f4912fa430edfae308493533ba7aa07111eb37aceda` |

两档均为C3、DIO/40MHz/4MB镜像、原分区及关闭autotest的普通固件；静态DRAM171777 B，链接表remain149519 B不是可用heap。默认清单仍为s，z仅作进程级覆盖。构建产物已分别冻结；`.build/firmware/`保留最后构建的z产物。

独立只读审查发现两项P2，均修复并通过复审：原合法长解释因整词排成四行而被拒绝，现最小字号超宽或超行数时均逐字兜底，导入和实际反馈共用三行限制；休眠帧首次刷新失败时过早关屏，现保存仅一次、先确认已有pending，再等待休眠目标revision全刷确认后关屏，沿用1000ms间隔及最多三次尝试，耗尽后保留原故障关机退路。对应回归为 `imported_explanation_uses_three_lines_even_when_whole_words_need_four` 及runtime的休眠重试、已有pending、重试耗尽三组行为用例。本次审查未发现有证据的P0/P1，结论限于源码、回归及日志。

软件验证阶段未访问设备或烧录；随后普通 z 候选的部署与启动观察见下节。实体键位、2秒长按边界、四列阅读与儿童操作、局刷残影/边框/耗时、完整运行期heap和连续块、各任务栈、保存错误与休眠/唤醒、SD掉电、配网固定面板和官方iPhone六位PoP端到端仍待真机验收。历史设备成功记录只适用于对应旧候选，不迁移为本次结论。底层BLE stop永不返回和持续显示故障仍是已有边界。

### UI 普通 z 候选部署与启动观察（2026-10-28）

用户明确授权后，向 ESP32-C3 rev 0.4 实机烧录本次普通 z 候选；公开记录已移除设备标识。证据目录为 `.build/ui-redesign-device-20261028T112533Z/`。只读确认安全flags为0、Secure Boot/Flash Encryption关闭、实际Flash为16MB；固件保持DIO/40MHz/4MB。当前bootloader及分区表与候选逐字节匹配，完整16MB原始备份及分片重算通过，并额外保存当前3MiB应用分区。

仅写入 `0x10000` 的2425296 B应用，SHA256 `6b42b3871795ff92a0866f4912fa430edfae308493533ba7aa07111eb37aceda`；写入及独立 `verify-flash` 均退出0，设备摘要匹配。未写bootloader、分区表或eFuse，未擦除NVS或整片Flash。原始完整恢复后的启动尚未演练。

设备已复位运行新固件，连续观察25秒，运行ELF前缀 `c6a8259cc` 与候选一致；SD挂载成功，`Startup storage: import=0 restore=0 status=4`，首帧到达 `display-end`；无崩溃、异常重启或显示失败标记。首帧后free heap为110776 B、历史最低84136 B、最大连续块102400 B、主栈剩余29568 B；随后的保存阶段历史最低heap为76796 B、最大连续块65536 B。这是启动样本，不能替代全部交互、无线共存及长期预算；实物画面、按键、局刷和配网仍待手动验收。

## 16MB Flash 扩容验收

用户明确要求利用完整16MB后，将构建容量从4MB迁移到已实测的16MB；本节替代上述历史候选的当前容量说明，历史尺寸、镜像和设备记录保留。证据冻结在 `.build/flash16-expansion/`，普通候选仍使用z，默认清单仍为s。应用、页面和网络生命周期实现未变。

| 区域 | 起始地址 | 大小 | 结束地址（不含） |
| --- | --- | --- | --- |
| 启动程序、分区表及预留 | `0x0` | `0x9000` | `0x9000` |
| nvs | `0x9000` | `0x6000`（24KiB） | `0xF000` |
| phy_init | `0xF000` | `0x1000`（4KiB） | `0x10000` |
| factory | `0x10000` | `0xFF0000`（15.9375MiB） | `0x1000000` |

[默认配置](../firmware/sdkconfig.defaults)使用 `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`，DIO/40MHz、64KiB主栈、内容/字体限额及无线配置保留。[构建入口](../tools/build.sh)会剔除旧容量choice、未选中choice注释和派生字符串，再设置16MB；记录中的旧缓存确为4MB，新s/z/phase2的最终有效配置均为16MB。[后置契约](../tools/check-autotest-build.py)同时核验choice与派生字符串、bootloader/app的DIO40MHz/16MB头、实际生成分区表的MD5/几何/容量，以及应用大小不超过 `0xFF0000`。扩展factory提供物理Flash存储预算，不扩大RAM；C3约8MiB的单路映射地址范围仍约束未来大型映射段，不能仅凭分区尺寸推导任意近16MiB代码/只读数据布局均可链接或启动。

| 验证命令 | 结果 | 本地日志 |
| --- | --- | --- |
| `python3 tools/test-build-env.py` | 退出0；安装/导出隔离、故障提前退出均通过 | `environment.log` |
| `CARGO_NET_OFFLINE=true sh tools/build.sh test` | 退出0；76项Rust及完整C平台、runtime、网络、IDF集成链通过 | `full-host.log` |
| `CARGO_NET_OFFLINE=true sh tools/build.sh build` | 退出0；普通s、16MB完整构建契约通过 | `build-s.log` |
| `CARGO_NET_OFFLINE=true CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build` | 退出0；普通z、16MB完整构建契约通过 | `build-z.log` |
| `CARGO_NET_OFFLINE=true CARGO_PROFILE_RELEASE_OPT_LEVEL=z BALABALA_AUTOTEST_PHASE=2 BALABALA_AUTOTEST_OUT="$PWD/.build/flash16-expansion/autotest-phase2" sh tools/build.sh build-autotest` | 退出0；phase2 z、16MB完整构建契约通过，仅离线构建 | `build-autotest-phase2.log` |
| 加载 `tools/env.sh` 后运行 `python3 .build/flash16-expansion/test-contract.py` | 1个有效候选通过；旧4MB配置、两个旧镜像头、旧3MiB表、越界表及超大应用共6个变体均被拒绝 | `contract-regressions.json` |

| 镜像 | BIN字节 | factory剩余 | SHA256 |
| --- | ---: | ---: | --- |
| 普通s | 2518176 | 14193504 | `69f45ac429554cac6da563e9960437c4a2b321f597ef6e646cbf5160072ca8b4` |
| 普通z（已部署） | 2425296 | 14286384 | `cad9813d1819fe546eb6a6b37f35ea22dc8678fd922d61c1c9554a02e2a2300f` |
| phase2 z（未部署） | 2432128 | 14279552 | `2a7fa20649492bf20a8ae1d237382c51ecaac3aaf36befef3b085261b10ebac3` |

只读独立复核未发现阻塞扩容的P1；确认本地固定IDF的C3启动程序支持16MB，以及分区大小 `0xFF0000` 在加载器范围内。已按建议同步当前文档，并将三个独立镜像写入、verify及启动前读回都显式保持下载模式，避免启动应用改变配置区后再比较。

### 设备迁移与启动

仍为同一台 ESP32-C3 rev0.4 实机（公开记录已移除设备标识）、安全flags为0，Secure Boot/Flash Encryption关闭。迁移前保存当前完整16MiB：`device/before-full-16mb.bin`，SHA256 `66b0c1c45dd55b33093a6fa5ba6930ac9f90db997af3e7b331efe60d41e8f8f6`；两个8MiB分片与完整文件一致，并通过同设备独立 `verify-flash` 摘要校验。完整备份和日志保存在权限0700的本地 `device/` 目录，原始备份不提交Git。

仅写入配套bootloader `0x0`、partition table `0x8000` 及普通z app `0x10000`。写入和三个区域的独立verify均退出0；没有整片擦除或eFuse写入。启动前回读NVS/PHY `[0x9000,0x10000)` 与本次备份逐字节一致，设备分区表与候选一致。详见 `device/commands.json`、`device/flash-passed.json` 和各步骤日志。

单独复位后观察25秒：启动程序报告 `SPI Flash Size : 16MB`，factory为 `00010000 / 00ff0000`；应用ELF前缀 `41e9d98f8` 与候选一致。SD挂载和首帧 `display-end` 完成，`Startup storage: import=0 restore=0 status=4`；记录一次预期启动，无崩溃、异常重启、显示失败或容量不匹配警告。首帧free/minimum/largest/stackFree分别为110776/84136/102400/29568 B，后续保存阶段minimum/largest为76796/65536 B。详见 `device/boot.log` 与 `device/startup-summary.json`。

设备当前运行普通16MB UI固件。启动样本不替代实体按键、局刷、睡眠唤醒、SD掉电、完整运行内存和官方iPhone配网验收；本轮未运行phase2诊断固件。完整回写恢复后的启动仍未演练，恢复指引保存在本地证据目录。

## 省电优化验收

省电候选在保留5ms ADC输入采样的基础上启用40–160MHz动态调频，并加入默认300000ms无操作自动深睡。按键按下、松开、按住及去抖候选会延后计时；显示在途、输入异常、配网/未释放无线、清理故障和刷机事务不触发自动休眠。期满先保存，成功且dirty已清才沿用休眠页全刷、精确revision确认、屏幕关屏与电源释放30ms门禁；无卡、保存失败或缺少ack时取消自动关机，保持当前页可操作。诊断固件禁用自动休眠计时，保留DFS。实现细节见[省电与自动休眠](platform.md#省电与自动休眠)，使用方法见[使用说明](usage.md#省电与自动休眠)。

本轮没有启用自动轻度睡眠：ADC电阻分压按键不能仅凭GPIO电平可靠唤醒全部按键，仍每5ms采样。空闲降频由IDF电源管理执行，任务及驱动锁在工作时保留CPU/APB性能；实际平均电流变化必须实测。主栈65536 B、唯一48000 B帧缓冲、7680 B stripe和内容/字体门禁保持。

证据目录为 `.build/power-optimization/`，改动前源码保存在 `before/`，候选源码与摘要保存在 `source/` 与 `source-changes.json`。所有产物位于Git忽略目录。命令均在仓库根目录执行，离线依赖使用 `CARGO_NET_OFFLINE=true`。

| 命令 | 结果 | 证据 |
| --- | --- | --- |
| `BALABALA_COMPARE_PAGES="$PWD/.build/ui-redesign-final-r2/pages" sh tools/build.sh test -- --nocapture` | 76 Rust、21 C输入/存储、47 runtime、17控制器、8 IDF适配、8 manager及14组事件集成通过；591张冻结PBM逐字节一致 | `host.log` |
| `sh tools/build.sh test-autotest` | Rust只读快照和完整C诊断回归通过，含phase2真实runtime + IDF替身 | `test-autotest.log` |
| `sh tools/build.sh build` | 普通s构建与镜像/分区/PM/符号契约通过 | `build-s.log` |
| `CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build` | 普通z构建与契约通过 | `build-z.log` |
| `CARGO_PROFILE_RELEASE_OPT_LEVEL=z BALABALA_AUTOTEST_PHASE=2 BALABALA_AUTOTEST_OUT="$PWD/.build/power-optimization/autotest-phase2" sh tools/build.sh build-autotest` | phase2 z构建与诊断符号隔离通过 | `build-autotest.log` |
| 加载 `tools/env.sh` 后 `python3 .build/power-optimization/test-contract.py` | 有效PM候选通过；PM关闭缓存变体被拒绝 | `contract-results.json` |

新增回归验证：5分钟截止前后、未改变页面的输入也重置计时、长按与松开、ADC/电源去抖候选、采样错误、无卡/保存失败/缺少ack保持唤醒及后续手动关机、无线活动/清理故障/刷机排除、在途显示结束后重新计时、自动休眠帧失败重试且只保存一次、NULL回调关闭自动休眠。C测试沿用ASan/UBSan与真实Rust ABI、SSD1677驱动；RTOS、射频和实际采样不是宿主替身的验证范围。

| 镜像 | BIN字节 | factory剩余 | SHA256 |
| --- | ---: | ---: | --- |
| 普通s（未部署） | 2524368 | 14187312 | `32d37f92d1ac6ed96978fcf4c28c0f65723c3418a1f07240476b97c65f3b1f34` |
| 普通z（未部署） | 2431520 | 14280160 | `5ba7d4757a769a211cdb849814bf38ccff3c6762bfb81ceb0914435443a8b55c` |
| phase2 z（未部署） | 2438144 | 14273536 | `32b120a63a09eab105c38de8f4405d3b5e99757df4f8db542c746720cf2da999` |

普通z相对已部署16MB候选增加6224 B，链接DRAM为172663 B，较前候选171777 B增加886 B；链接剩余148633 B不等于动态可用heap。NVS/PHY和factory起始地址、16MB/DIO/40MHz及分区表保持。诊断首轮构建因`balabala_autotest_input_idle`被裁剪遭后置检查拒绝；保留诊断输入门禁引用后重新构建s/z/phase2，全部通过，失败日志保存在`build-autotest-initial.log`。

本轮未烧录。设备仍运行上节已部署的16MB UI固件。待真机验收：非USB供电下的静置/翻页/配网/深睡整机电流，全部ADC键及长短按，自动休眠前真实SD保存与唤醒恢复，DFS下的共享SPI/屏幕时序、动态heap/连续块/任务栈和官方iPhone配网。当前结果不能换算为电池续航或宣称节省百分比。

## 首页与实体按键对齐修正

中文产品名为“扒拉扒拉”。首页移除顶部名称和返回键旁的锁图标；家长入口仍为首页长按返回2秒。根据原版X4实拍照片 `2026-10-28 21.05.16.jpg`，把屏幕四角映射到480×800逻辑坐标，估算四键中心为90.7 / 184.7 / 293.7 / 387.6，采用90 / 185 / 295 / 390。导航键帽、答题列、选项、A–D字母和作答后的导航图标均由[共享几何](../tools/ui-layout.ts)生成；照片存在透视与人工取点误差，精确对齐仍须正视真机核验。

导航键帽宽80px，答题列宽88px、文字区宽80px，避免内收后重叠。选项继续整题选择48 / 32 / 24px，24px行距从36px调整为30px，最多6行，保持原16字符/24px宽370px导入限额。新增16个W、m、@与15个汉字的排版及实际渲染边界回归；没有截断字段。

证据目录为 `.build/ui-photo-correction/`：`source-before.tar.gz`保存首批修改前文件，`pages/`和`page-sha256.json`保存592张可重放冻结PBM，`preview.png`展示首页、英语和长选项边界。旧基准中36张相同View的PBM保持一致，含刷机最终提示、配网和旧模板；525张相同View因名称/锁移除及几何改变产生预期差异，另29个View因选项折行或字号发生变化。不能将新基准重放一致解释为与旧UI全部一致。

| 命令 | 结果 | 证据 |
| --- | --- | --- |
| `CARGO_NET_OFFLINE=true sh tools/build.sh test -- --nocapture` | 76项Rust及完整C平台链通过；432个页面通过文字边界、重复渲染与heap门禁，最大宿主render峰42285 B，低于51166 B门限 | `host.log` |
| 加载`tools/env.sh`并设置`BALABALA_GENERATED`后，`CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/ui-photo-correction/pages" rustup run "$RUST_VERSION" cargo test --locked --manifest-path .build/native/Cargo.toml --lib compare_frozen_page_inputs_when_supplied -- --nocapture` | 592张冻结PBM相同输入重放逐字节一致 | `frozen-compare.log` |
| `CARGO_PROFILE_RELEASE_OPT_LEVEL=z CARGO_NET_OFFLINE=true sh tools/build.sh build` | 普通C3 z构建及DIO/40MHz/16MB、PM开启、分区和非诊断符号契约通过 | `build-z.log` |

普通z候选为 `build-z/balabala_microts_probe.bin`，2431248 B，factory剩余14280432 B，SHA256为 `c19738f60a3e131c0fd34bef31f54545531e8f9b6df8df475665ff4d1375eedd`。初轮小视口条带测试依赖首页左上角文字，名称移除后视口成为空白；已改用独立固定文字View，仍校验有墨迹及整帧/条带一致，初轮日志保存在`host-initial.log`。

该版本随后以app-only方式写入同一台原版X4，写后独立校验通过，启动前bootloader/分区/NVS/PHY与备份逐字节一致。25秒启动观察匹配ELF身份，SD挂载、恢复与显示完成，无保存错误或崩溃日志；证据在 `.build/ui-followup-device/`，其中完整16MB备份已经设备摘要校验。实体键位对齐、原先“保存失败”的复现和省电真机项目仍待验证。

## 霞鹜文楷与内置内容扩充

学习页、菜单、配网面板及特大字改用官方[霞鹜文楷v1.530](https://github.com/lxgw/LxgwWenKai/releases/tag/v1.530)：24/32px使用Regular，48px和特大字使用Medium。版本、来源commit及两个TTF摘要锁定在[版本文件](../tools/versions.sh)，安装入口与构建入口均校验文件摘要；[OFL许可](../licenses/OFL-LxgwWenKai.txt)及[第三方归属](../licenses/README.md)已保留。刷机模式最终两行英文提示保留原字体和像素输出。

四科各追加12张内置卡，共32×4=128张，卡组版本升为2。汉字新增天气、方位与植物，拼音新增声母与韵母，英语新增动物、颜色与日常词汇，数学新增数量、20以内运算和比较。与上一部署源码快照逐字段核验，各科原20张的ID、题目、答案和说明完整保留；旧进度继续按ID恢复。兼容性证据为 `.build/wenkai-followup-r2/content-compatibility.json`。

存储错误新增固定操作阶段，保存日志可区分open/write/flush/sync/close/verify及恢复保护拒绝，并保留准确errno；补充同步失败和校验阶段失败的回归。原先write路径会把某些已清除或陈旧的errno带入日志，现从实际失败步骤记录。保存失败仍保留dirty和旧槽，不自动格式化、不绕过恢复保护。本次只完成诊断与错误码修正，不能据此认定用户设备的保存故障已修复。

闪屏策略未改：页面切换、连续8次局刷后的下一次刷新、基准失效及特定系统页面使用全刷；SSD1677全刷可产生数次黑白变化。显示失败最多尝试3次。新增日志记录实际full/partial、连续局刷计数及attempt，便于区分正常全刷与失败重试；尚未采集用户所述多次闪烁当时的日志。

本轮证据目录为 `.build/wenkai-followup-r2/`，`pages/`保存880组可重放View/PBM，`preview.png`展示新字体与新增卡片。新旧共同View的比较见`baseline-comparison.json`；普通文字和特大字变化符合字体替换预期，`download-mode`保持逐字节一致。新基准重放一致不表示与旧字体页面一致。

| 命令 | 结果 | 证据 |
| --- | --- | --- |
| `CARGO_NET_OFFLINE=true sh tools/build.sh test -- --nocapture` | 76项Rust及完整C平台链通过；672个页面通过文字边界、重复渲染与heap门禁，最大宿主render峰42421 B，低于51166 B门限 | `host.log` |
| 加载`tools/env.sh`并设置`BALABALA_GENERATED`后，`CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/wenkai-followup-r2/pages" rustup run "$RUST_VERSION" cargo test --locked --manifest-path .build/native/Cargo.toml --lib compare_frozen_page_inputs_when_supplied -- --nocapture` | 880张冻结PBM相同输入重放逐字节一致 | `frozen-compare.log` |
| `CARGO_PROFILE_RELEASE_OPT_LEVEL=s CARGO_NET_OFFLINE=true sh tools/build.sh build` | 普通C3 s构建及固件契约通过 | `build-s.log` |
| `CARGO_PROFILE_RELEASE_OPT_LEVEL=z CARGO_NET_OFFLINE=true sh tools/build.sh build` | 普通C3 z构建及固件契约通过 | `build-z.log` |
| `python3 tools/test-build-env.py` | 安装入口与IDF环境隔离三项检查通过 | `build-env.log` |

普通z候选为 `build-z/balabala_microts_probe.bin`，2553792 B，factory剩余14157888 B；SHA256为 `1f093231202fec6f67d8056e92ea022f12b69d8b84dc728670c799b1466c4b1d`。DIO/40MHz/16MB、64KiB主栈、PM开启、非诊断配置和分区契约均通过。宿主内存指标不等于C3可用heap。

该文楷候选随后已app-only部署并独立Flash校验，部署记录位于 `.build/wenkai-followup-device/`。25秒启动观察确认对应ELF、SD挂载、存档恢复与首屏完成；未见保存或显示错误，不能由此认定用户所述保存失败已修复。后续字体与充电策略以本节之后的记录为准。

## 霞鹜新晰黑与充电休眠

当前学习页、菜单、配网面板与特大字采用官方[霞鹜新晰黑v1.308](https://github.com/lxgw/LxgwNeoXiHei/releases/tag/v1.308)。24 / 32 / 48px及176 / 112px使用同一字重，字形、拼音与导入内容排版通过宿主门禁。来源提交和SHA256锁定在[版本文件](../tools/versions.sh)，构建入口校验下载摘要；[IPA许可](../licenses/IPA-Font-License-v1.0.txt)、归属与[原版字体替换方法](../licenses/LxgwNeoXiHei.md)已保留。原版IPAex替换构建通过，切回默认字体后候选逐字节复现，见 `.build/neoxihei-followup/restoration-check.log`。配网文字格按24px字档调整；两次准备帧与活跃期零分配更新契约保持。刷机最终两行提示仍使用原Noto字体。

普通固件新增GPIO20外部供电检测：接USB电源时禁止自动休眠，充满但保持接线也一样；拔线后重新计时五分钟。检测初始化失败时禁止自动休眠。自动休眠过程中接电会撤销该事务和休眠帧的确认资格、返回可操作首页；手动长按关机仍有效。接线来源和运行时复核点见[平台省电策略](platform.md#省电与自动休眠)。检测依据为USB供电状态，不读取电池充电百分比。

新增真实Rust ABI/runtime回归覆盖：持续接电不休眠、拔线后完整五分钟门槛、待重试休眠帧/关屏前后/同步显示期间插线撤销、旧休眠revision不能迟到确认、撤销后导航可用，以及接电时手动关机。存档、显示重试、BLE生命周期与128张内置卡片保持既有契约。

本轮证据在 `.build/neoxihei-charging/`，此前字体预览和880组冻结View/PBM位于 `.build/neoxihei-followup/`。与文楷旧页比较时，学习文字、特大字和配网字格允许变化，最终刷机提示逐字节不变；本轮充电逻辑不改变页面输出。

| 命令 | 结果 | 证据 |
| --- | --- | --- |
| `CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/neoxihei-followup/pages" sh tools/build.sh test -- --nocapture` | 76项Rust、21项C输入/存储、50项真实ABI/runtime及完整网络测试链通过；880张PBM同输入重放逐字节一致 | `host.log` |
| `CARGO_NET_OFFLINE=true sh tools/build.sh test-autotest` | 诊断快照及真实学习/保存/恢复/重启等诊断回归通过 | `test-autotest.log` |
| `CARGO_NET_OFFLINE=true CARGO_PROFILE_RELEASE_OPT_LEVEL=s sh tools/build.sh build` | 普通C3 s构建与固件契约通过 | `build-s.log` |
| `CARGO_NET_OFFLINE=true CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build` | 普通C3 z构建与固件契约通过 | `build-z.log` |

672个完整页面通过文字边界、重复渲染与heap门禁，最大宿主render峰42421 B，低于51166 B门限；宿主指标不代表真机heap。普通z候选为 `build-z/balabala_microts_probe.bin`，2440176 B，factory剩余14271504 B；SHA256为 `fb54b8e47d4e2405e90e77fc78c1607703e8b49b3c4595621ae1d73617744200`。DIO / 40MHz / 16MB、64KiB主栈、PM开启与非诊断配置均通过。

本候选已app-only烧录并独立校验，写入地址`0x10000`。烧录前保存完整16777216 B备份并与设备摘要核验，SHA256为`bae1bdc4c3fef968be76a0d5f3f851b53af52ec0268c59a3569d73e54a09de7e`；写入后复核bootloader、分区、NVS和PHY所在的前64KiB逐字节不变，未写eFuse、未擦除整片。备份与部署证据位于 `.build/neoxihei-charging-device/`，均排除在Git之外。

25秒启动观察确认ELF前缀`71522cf85`与本候选一致，日志报告USB已连接并禁自动休眠；SD挂载、存档恢复与首屏全刷完成，未见保存错误、显示错误、看门狗或heap异常。该观察没有触发一次真实保存，也没有覆盖五分钟门槛。仍需真机确认拔线电平、接电静置超过五分钟不休眠、拔线后自动保存/休眠与唤醒、新字体屏上可读性，以及保存故障/多次闪屏当时的日志。

## 代码审查与简化

模块和修改入口见[代码结构](architecture.md)。本次审查对照Git基线`cf63e7745182183e9e36b610f3cb0002e24e6f9f`；模型按内容、有界解析、存档、配网和View组装分层，应用根库分离渲染、C ABI及目标分配器，构建与测试也按职责组织。投影和字体遍历共用文字解析，复用编译器的`AotProgram`、`AotNode`和`PROP`定义；继续使用现有serde及标准库。

下表为入口文件长度，子模块另计：

| 入口 | 基线行数 | 当前行数 |
| --- | ---: | ---: |
| `native/src/model.rs` | 1684 | 575 |
| `native/src/lib.rs` | 678 | 61 |
| `tools/build-native.ts` | 164 | 25 |
| `tools/test-native-render.rs` | 2334 | 72 |

行为修正与覆盖：

- 导入拒绝纯空白字段；按与View相同的排版结果拒绝视觉重复选项，包括首尾空格、连续空格和标点黏附。失败导入保持Snapshot、revision及活动卡组，仍允许带空格且可正常显示的有效内容。
- 均衡折行统一空格计宽和实际输出；二分时遍历借用切片计算行数，仅在最终输出时分配行字符串。保留最小字号逐字兜底和已有导入上限。
- PoP清除测试直接检查设备名和口令字段，替换失效的旧文本前缀检查；volatile清除实现保留。
- 普通启动无SD时同时启用模型保存保护，平台保存门禁与模型拒绝导出共同生效；不删除启动故障提示。
- C内部事件位标志统一为`BALABALA_NET_EVENT_*`，与原有网络状态枚举区分；数值和C ABI保持。显示旋转仅外提循环不变量。

证据目录为`.build/project-review/`，全部排除在Git之外。冻结目录包含变更前的View和PBM；比较使用相同输入重放，880张PBM逐字节一致。672个完整页面的最大宿主render峰为42421 B，低于51166 B门限；配网零分配、完整生命周期内存和错误释放测试通过。

| 命令 | 结果 | 日志 |
| --- | --- | --- |
| `python3 tools/test-build-env.py` | 安装入口和环境隔离检查通过 | `environment.log` |
| `CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/project-review/frozen-pages" sh tools/build.sh test -- --nocapture` | 78项Rust；21项C输入/存储、50项真实ABI/runtime及完整网络测试链通过；含ASan/UBSan | `host.log` |
| `CARGO_NET_OFFLINE=true sh tools/build.sh test-autotest` | 诊断宿主回归通过 | `test-autotest.log` |
| `CARGO_NET_OFFLINE=true sh tools/build.sh build` | 默认s普通C3构建及契约通过 | `build-s.log` |
| `CARGO_NET_OFFLINE=true CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build` | z普通C3构建及契约通过 | `build-z.log` |
| `CARGO_NET_OFFLINE=true CARGO_PROFILE_RELEASE_OPT_LEVEL=z BALABALA_AUTOTEST_PHASE=2 BALABALA_AUTOTEST_OUT="$PWD/.build/project-review/autotest-phase2" sh tools/build.sh build-autotest` | phase2诊断C3构建及契约通过 | `build-autotest-phase2.log` |

普通s / z镜像分别为2579728 / 2440816 B；z的factory剩余14270864 B，普通候选保存在`build-z/balabala_microts_probe.bin`，SHA256为`1eb2095905b330cf1142f61543eab0bf183ed8a34382b8bffea23321b64ccea7`。DIO / 40MHz / 16MB、64KiB主栈和普通固件关闭诊断等契约均通过。类型复用及AOT编译不等于完整TypeScript类型检查。

独立只读复审确认原两项P2闭合，未发现本次diff引入且尚未关闭的P0/P1/P2；没有独立重跑测试，测试数据来自上述主Agent执行的命令。复审记录保存为`final-review.md`。

本次重构尚未烧录，真机运行、真实SD保存、BLE/iPhone端到端和动态heap仍需按既有边界验收；此前保存失败及多次闪屏尚无故障时日志，不能仅凭本次宿主测试判定已解决。

## 性能优化

对照提交`fe1bc25`，优化1bpp位图合并、按行圆角填充/反白、RGB565黑白转换、导入字体预算的View复制及CRC32。相同宿主release配置下，六种页面的完整渲染耗时下降43.1%–57.6%，32KiB CRC下降49.8%；汉字/英语32卡导入各减少803次分配请求。六页render和两组导入的heap峰值保持原值。详细样本、测量范围及复现方法见[性能记录](performance.md)。这些比例不能换算为C3翻页提速或续航。

证据目录为`.build/performance/`：`before-release.json`与`after-release.json`保存相同基准的五批中位数，`source-before/`与`source-after/`保存运行时源码，`firmware-summary.json`记录尺寸和校验表段归属。新CRC表在普通z ELF中仅匹配一次，位于`.flash.rodata`的`0x3c22e838`，64 B、不可写，没有迁入DRAM。

新增回归覆盖八种位对齐、负坐标和边缘裁剪、素材padding、圆角反白，以及全部65536种RGB565颜色；IEEE CRC已知向量、分块输入和旧位算法生成记录均通过。完整宿主测试为82项通过、1项性能基准默认忽略；21项C输入/存储、50项真实ABI/runtime及完整网络链通过。672个完整页面的宿主render峰值仍为42421 B，880张冻结PBM相同输入重放逐字节一致。

| 命令 | 结果 | 日志 |
| --- | --- | --- |
| `CARGO_NET_OFFLINE=true BALABALA_COMPARE_PAGES="$PWD/.build/project-review/frozen-pages" sh tools/build.sh test -- --nocapture` | 完整Rust/C宿主测试及冻结PBM比较通过 | `host.log` |
| `CARGO_NET_OFFLINE=true sh tools/build.sh test-autotest` | 诊断宿主测试通过 | `test-autotest.log` |
| `CARGO_NET_OFFLINE=true sh tools/build.sh build` | 默认s普通C3构建及固件契约通过 | `build-s.log` |
| `CARGO_NET_OFFLINE=true CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build` | z普通C3构建及固件契约通过 | `build-z.log` |
| `CARGO_NET_OFFLINE=true CARGO_PROFILE_RELEASE_OPT_LEVEL=z BALABALA_AUTOTEST_PHASE=2 BALABALA_AUTOTEST_OUT="$PWD/.build/performance/autotest-phase2" sh tools/build.sh build-autotest` | phase2诊断C3构建及固件契约通过 | `build-autotest-phase2.log` |

普通s / z镜像为2580160 / 2440976 B，较基线分别增加432 / 160 B；z的factory剩余14270704 B。普通候选为`build-z/balabala_microts_probe.bin`，SHA256为`2b1b2268fed1a2f8d2e72bcbbf2d93448c00b335496a30f43731fc66a21134ee`。DIO / 40MHz / 16MB、64KiB主栈、PM及普通固件关闭诊断等契约通过。局刷/全刷策略、C ABI和存档格式保持。

独立只读评审建议保留全部优化，未发现本diff引入的P0/P1/P2，记录为`final-review.md`；评审未独立运行测试。

本候选已在用户确认后app-only烧录至同一台原版X4的`0x10000`并独立校验，设备为ESP32-C3 rev0.4、16MB。烧录前保存完整16777216 B备份并与设备摘要核验，SHA256为`7b98d60693dc411c10e53543159a8705934f047cf072c57ed9285e144e1dc7a0`；写后复核前64KiB，bootloader、分区表、NVS和PHY与备份逐字节一致，未写eFuse或擦除整片。部署与私有备份位于`.build/performance-device/`，均排除在Git之外。

25秒启动观察匹配ELF前缀`aa5f15c11`，确认USB供电、SD挂载、现有存档成功恢复（`restore=0 status=4`）及首屏完成；仅一次全刷尝试，未见保存错误、显示错误或崩溃。该观察没有触发新的学习保存，也没有完成交互性能基准。C3实际执行时间、动态heap/栈高水位、SD写入延迟与屏幕响应仍待专项真机测量。

## 小学课程与累计掌握

本轮课程候选提供4000张、135个单元：汉字2200、拼音150、英语450、数学1200。英语为225个学习词的双向卡，不代表450个独立词。年级及主题顺序为项目编排，尚未逐题人工教学审校；来源、计数及维护入口见[内容说明](../content/README.md)，操作和存档协议见[课程说明](curriculum.md)。

默认跨轮累计答对3次学会，可设1 / 3 / 5 / 7 / 9次；答错不清零，同题每轮仅累计一次，已学会卡退出普通学习与复习。各科当前单元达到80%学会后解锁下一单元，向上取整；手动起点确认默认取消。SD正文仅按轮读取，存档v2保留32768字节双槽上限并兼容v1；旧“认识”只迁移一次正确，退役英语ID不转移给不同词。

| 命令 | 本轮结果 | 证据 |
| --- | --- | --- |
| `BALABALA_COMPARE_PAGES="$PWD/.build/curriculum-baseline" sh tools/build.sh test -- --nocapture` | 95项Rust通过、1项性能基准默认忽略；22项C输入/存储、50项真实ABI/runtime及完整网络测试链通过，C使用ASan/UBSan | `.build/curriculum-host.log` |
| `sh tools/build.sh test-autotest` | 诊断快照及真实学习、保存、恢复、重启测试通过；phase2清理、RTC、分配故障及主流程通过 | `.build/curriculum-autotest.log` |
| `CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build` | 普通z C3构建通过；DIO / 40MHz / 16MB、64KiB主栈、PM、分区及关闭autotest等契约通过 | `.build/curriculum-build-z.log` |
| `python3 .build/verify-curriculum.py` | 独立复算1200张数学答案、核对来源摘要及原ID保留；超NGSL、非GB2312汉字、重复学习词和字段超长四项故意污染均被拒绝，人工源已恢复 | `.build/independent-check-report.json`、`.build/negative-check-report.json` |

4000张课程逐张经过真实Rust解析器、排版和字体预算检查。820个页面通过文字边界及heap门禁，宿主render峰43696 B，低于51166 B上限；该值不是C3动态heap。新学习设置、四科起点、135个单元选择、默认取消、课程完成及四位数统计页均有PBM检查；抽看12张页面并复核完整统计和末单元页面，未见文字溢出。页面预览为`.build/curriculum-final/page-contact.png`。

旧冻结目录已核实存在，取其中872张未涉及预期变化的View重放，PBM逐字节一致。8张设置和统计页因新入口及累计掌握语义发生预期变化，未纳入旧图一致性断言；原目录和筛选理由保存在`.build/curriculum-baseline-selection.json`，没有将预期变化描述为全部880张一致。

完整4000卡进度测试记录为20216 B，低于32768 B槽上限；覆盖累计次数、门槛调整、单次评分、80%向上取整、已学会退出、短轮不重复、选项恢复、起点默认取消、v1迁移、退役词不继承证据、未知课程/键和同版本CRC冲突保护、当前轮次读取失败及迟到ack。C平台另覆盖课程流式校验、损坏拒绝和缺失课程时的原档保护。

最终只读复审未发现能给出明确触发条件的P1/P2问题，并独立核对4000卡、135单元及课程CRC；评审没有独立执行构建或测试。复审及固件/课程摘要与冻结页面保存在`.build/curriculum-final/`，全部排除在Git之外。SD分发目录包含`NOTICE.md`、完整数据许可、人工源及来源锁，须连同正文一起复制。

本轮普通z已在用户授权后app-only烧录至同一台原版X4：ESP32-C3 rev0.4、16MB，USB ROM可用、安全标志为0，设备分区及bootloader与已验证版本和当前构建一致。烧录前保存完整16777216 B备份并独立核对设备摘要，SHA256为`61c9017fe24133ed2f8b929578017c9d5be562def8a9792161cf7f6390f5b40e`。仅写`0x10000`应用，BIN为4598240 B，SHA256为`dce2b02bd5257b8a1527cca979108e7b2317eec67fee72b030be2863e2eba3fc`；写后独立校验成功，启动前复读前64KiB，bootloader、分区表、NVS及PHY与备份逐字节一致，未写eFuse或擦除整片。

25秒启动观察匹配ELF前缀`3c5491fe6`，确认USB供电、SD挂载、原存档恢复（`import=0 restore=0 status=4`）及首屏完成；一次全刷尝试，未见保存错误、显示错误或崩溃。首屏`display-end`采样INTERNAL|8BIT free/largest为93644/81920 B，累计最低空闲66792 B，主任务未用栈水位27792 B；此后`save-end`采样累计最低空闲57792 B。它们只是本次启动的阶段样本，不是完整课程及全任务的预算闭合。部署、私有完整备份、恢复说明和脱敏启动日志位于`.build/curriculum-device/`，均排除在Git之外。

电脑只识别USB串口，没有挂载SD卷；本次烧录未安装或验证4000张课程正文。完整课程仍须关机后将配套`curriculum`目录复制至SD的`/balabala/`，保留既有`saves`；SD挂载和原存档恢复不能作为4000卡安装成功的证据。完整课程真实取卡/写入、连续学习、动态heap及最大连续块、全部任务栈高水位、掉电恢复、实体按键及官方iPhone配网仍待验收。普通候选使用z，未重建默认s；此前设备性能数据仍仅适用于各自冻结镜像。

## 发布前全量检查与源码门禁

本轮审查对学习模块拆分、4000 卡目录查询、配网面板去重、按键释放及进度迁移修复执行了完整宿主回归、源码门禁测试与 C3 构建。原始日志和冻结页面保存在已忽略的 `.build/release-review/`，下列结论限定于本次源码及测试环境。

### 门禁执行与结果汇总

| 检查类别 | 执行命令 / 证据文件 | 检验结果 | 说明 |
| --- | --- | --- | --- |
| Rust 宿主测试 | `BALABALA_COMPARE_PAGES="$PWD/.build/release-review/baseline-pages" sh tools/build.sh test -- --nocapture` | 105 passed, 0 failed, 1 ignored | 1 项耗时性能基准默认忽略；覆盖学习调度、双槽存档、AOT 栅格化等 |
| C 平台与运行时测试 | 同上（由 `build.sh test` 自动串行拉起 `test-platform.sh`） | 120 passed, 0 failed | 22 平台输入/存储 + 50 runtime/ABI + 17 网络控制器 + 8 IDF 适配 + 8 manager + 1 protocomm + 14 事件集成；ASan/UBSan 零报错 |
| 页面像素一致性 | `.build/release-review/render-source-acceptance.json` | 1029 / 1029 逐字节一致 | 1028 冻结 View 输入 + 1 存储错误页，重放生成的 PBM 与基线逐字节一致 |
| 真实 ABI 面板帧 | `.build/release-review/panel-pages-comparison.log` | 81 / 81 逐字节一致 | 81 组实际 ABI 驱动的配网状态面板帧与基线逐字节一致 |
| 源码完整性门禁 | `python3 tools/test-source-integrity.py`（日志见 `source-integrity-tests.log`） | 18 / 18 passed | 覆盖 PocketJS 归档摘要/目标文件、ESP-IDF 根目录与递归子模块提交/变更/文件模式校验及错误注入 |
| 构建环境隔离 | `python3 tools/test-build-env.py`（日志见 `build-env-tests.log`） | passed | 验证环境隔离、IDF Python 虚拟环境与失败传播 |
| 性能基准（5 批中位数） | `.build/release-review/final-performance-comparison.json` | 已测量，无耗时阈值 | 同次宿主中查询全部 4000 个 ID 快 51.56 倍；汉字/英语 32 卡导入分配请求减少 24.03% / 23.44%；六类页面的堆峰值持平 |
| C3 交叉构建 | `CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build` | passed | 成功生成 ELF、MAP 与 BIN，后置镜像头与分区尺寸校验通过 |

### 固件尺寸与分区契约

当前 `opt-level = "z"` 候选构建产物指标：

- 原始固件 BIN 尺寸：4,599,600 字节
- 镜像实际大小：4,599,209 字节
- Bootloader 尺寸：21,280 字节
- 静态 DRAM 占用：172,671 字节
- 链接器剩余 DRAM：148,625 字节，不能视为运行时可用 heap
- Flash 布局：DIO / 40MHz / 16MB，`factory` 起始偏移 `0x10000`，容量 `0xff0000`（至 16MiB 边界）
- `factory` 分区剩余空间：12,112,080 字节（充足）
- 项目清单文件（`Cargo.toml`）默认配置仍为 `opt-level = "s"`，`z` 仅用于候选验证。

### 仓库历史与敏感信息核查（限定范围）

在代码库历史对象上执行只读启发式扫描：

- 历史共计 4 次提交、223 个对象路径：生成目录、编译二进制扩展名以及 `.env`、私钥证书等敏感文件名均为 0 命中；
- 对 270 个独立 blob 进行私钥块（`BEGIN PRIVATE KEY` 等）及主流云平台令牌模式（GitHub / AWS / OpenAI）的启发式扫描，结果为 0 命中。
- **说明**：该项检查属于限定范围的自动化模式匹配，不代表形式化的安全审计。

### 独立复审结论

模型与学习状态机、渲染与布局、源码完整性门禁分别完成了只读独立复审。在所审范围内未发现须修复的 P1/P2 问题；复审结论不能替代真机验证。

### 真机验证边界与未决项

- **全量软件验证的范围**：上述回归在宿主与交叉编译检查中完成；随后经用户授权部署当前候选，启动观察见下节；
- **4000 张 SD 课程真机读取尚未验证**：此前的课程候选仅执行了 25 秒 app-only 启动观察（验证了基础 SD 挂载与旧档恢复），未安装配套的 4000 张课程正文，完整课程取卡与连续学习仍待真机验收；
- **BLE 与 iPhone 交互**：乐鑫官方 iOS 客户端的 6 位数字 PoP 配网、空中广播关闭与 300 秒超时在当前 UI 下仍待真机复核；
- **底层硬件指标**：C3 动态运行 heap 与最大连续块、各任务实际运行栈高水位、ADC 物理键位手感与墨水屏局刷残影，必须以实机测试为准。

### 发布审查候选部署与启动观察（2026-10-29）

用户明确授权提交与烧录后，代码提交为 `116bcfd`。同一台原版 X4 / ESP32-C3 rev0.4、16MB 的设备身份、USB ROM、安全标志为 0 及 DIO / 40MHz / 16MB bootloader 与分区均重新核验通过。烧录前保存完整 16777216 B 备份并通过设备独立摘要校验，SHA256 为 `89f7cf380b95ca17c8a5707e887a5e06da39f06b3d2daec5184965f05173b38a`；另提取旧应用并核对上一轮部署摘要，保留恢复说明。

仅将 4599600 B 普通 z 应用写入 `0x10000`，SHA256 为 `ce58abf65cdc0d43778a7256985b941cd54e0adebcf45a854e34127e4808c120`。写入和独立 verify 均成功；启动前回读前 64KiB，bootloader、分区表、NVS 与 PHY 和备份逐字节一致。未写 eFuse，未整片擦除。私有备份、候选镜像、恢复说明与原始日志位于 `.build/release-review-device-20261029T071948Z/`，不纳入 Git。

25 秒启动观察匹配 ELF 前缀 `88e56fc79`，确认 USB 供电、SD 挂载、原存档恢复（`import=0 restore=0 status=4`）及首屏完成；一次全刷尝试，未见保存错误、显示错误、崩溃或异常重启。首屏 `display-end` 的 free / largest 为 93644 / 81920 B，累计最低空闲 66792 B，主任务未用栈水位 27792 B；随后 `save-end` 累计最低空闲为 57792 B。这些只是启动阶段采样，不能视为完整运行内存验收。

主机没有挂载 SD 卷，本次未安装配套 4000 张课程正文，也未验证完整课程连续学习、实际答题保存、掉电恢复或官方 iPhone 配网。完整部署范围与待验项见[部署记录](../memos/release-deployment.md)。
