# balabala

面向原版 XTEINK X4 / ESP32-C3 的小学一至三年级离线卡片学习固件，覆盖汉字、拼音、英语、数学。应用使用 MicroTS strict AOT 页面、Rust 模型和平台 C ABI，BLE 配网面向乐鑫官方 iOS 客户端。

## 常用命令

- 所有命令在仓库根目录执行；安装前置依赖及固定工具链说明见 [README.md](README.md)。`native/Cargo.toml` 由脚本复制到 `.build/native/` 后使用，不要直接在 `native/` 执行 Cargo；通过脚本加载项目内工具链。
- 安装：`sh tools/setup.sh`；完整宿主测试：`sh tools/build.sh test`；默认 s 的 C3 构建与尺寸检查：`sh tools/build.sh build`。
- 当前设备候选使用 `CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build`；z 仅作进程级覆盖，默认清单仍为 s，不因候选构建改全局默认。
- 筛选 Rust 测试：`sh tools/build.sh test network_`；精确单测：`sh tools/build.sh test tests::read_flip_skip_and_back_do_not_score -- --exact`。参数传给 Cargo，两者仍运行完整 C 平台测试。
- 诊断宿主测试：`sh tools/build.sh test-autotest`；phase2 C3 诊断构建：`CARGO_PROFILE_RELEASE_OPT_LEVEL=z BALABALA_AUTOTEST_PHASE=2 sh tools/build.sh build-autotest`，默认输出到 `.build/autotest-phase2/firmware/`，可用 `BALABALA_AUTOTEST_OUT` 隔离证据目录。
- 诊断固件会自动驱动流程，不用于普通交付或官方 iPhone 手动验收；普通 `build` 强制关闭 autotest，构建后自动检查配置、镜像头、符号和分区尺寸。
- 修改安装或环境隔离逻辑后运行 `python3 tools/test-build-env.py`；该检查会实际调用安装入口。
- 尚无独立 lint、format 或 TypeScript typecheck 命令；不要将 MicroTS AOT 编译称作完整 TypeScript 类型检查。
- 原始固件产物为 `.build/firmware/balabala_microts_probe.{elf,map,bin}`，宿主页图为 `.build/pages/*.pbm`。不得直接套用参考仓库的构建或烧录命令。

## 开发边界

- 完整课程当前4000张/135单元，人工源和固定来源锁见`content/curriculum/`；所有字段中的汉字限GB2312一二级汉字，英语卡所有英文词限固定NGSL原形白名单。正文生成到`.build/sdcard/`，须与同次构建固件配套；不可按码位补齐题量。
- SD课程仅按轮加载最多10张正文；存档v2保留32768字节双槽上限，读取v1只把旧“认识”迁移为一次正确。新设置、稳定ID及退役规则见[docs/curriculum.md](docs/curriculum.md)。
- 目标是原版 X4 / ESP32-C3；不要混用 X4 Pro、X4 Classic、S3 的板级配置、Xtensa 工具链、PSRAM 或双核绑定。
- `../giligili` 仅为外部参考，不修改它。MicroTS 不是完整 TypeScript / 浏览器 Vue，不支持默认假设的动态 JS 安装。
- 固定版本及本地补丁由 `tools/` 和 `patches/` 中锁文件校验；修改上游行为须更新可重放补丁及摘要，不只修改 `.tools/` 派生源码。补丁说明见 [patches/README.md](patches/README.md)。
- 墨水屏按状态变化刷新；连按不得重复评分或形成无界刷新队列。帧缓冲、字体、任务栈及 BLE/Wi-Fi 共存均须纳入运行内存预算；链接表剩余 DRAM 不等于可用 heap。
- 侧面电源短按回桌面、长按关机且不先触发短按；侧面上下翻页；正面四键在答题页为 A/B/C/D，非答题页为左/右/返回/确认。答对显示静态黑白鼓励图案，由孩子主动翻题；完整交互见 [docs/product.md](docs/product.md)。
- 平台接线、共享 SPI、存储与关机契约见 [docs/platform.md](docs/platform.md)；内容格式、限额及应用 ABI 见 [docs/learning-app.md](docs/learning-app.md)。不得仅为构建通过下调栈或内容/字体限额。
- 启动 SD 未挂载时禁止平台保存；执行恢复时根目录不可用、任一槽 IO/内存错误或最高有效档含未成功导入科目的未知卡片 ID 时，模型必须持续保护原档并拒绝导出。重置进度、重新初始化 C 存储服务及迟到 ack 不能解锁；仅成功恢复可解释的有效档解除。确认 CRC/语义/大小无效的槽仍可降级，见 [双槽存档](docs/learning-app.md#双槽存档)。
- 刷机模式只在家长第五项二次确认后进入，默认取消；最终提示仅白底居中英文 `Download Mode` / `Keep USB connected`，不持久化准备状态。严格先只读 eFuse 预检、保存成功清 dirty、一次 CANCEL 等待完整网络清理、精确提示 revision 全刷 ack、display_sleep，再复核取消/关机；仅 C3 FORCE_DOWNLOAD_BOOT + 公共 restart，不写 eFuse、不自动 retry_cleanup，见 [docs/download-mode.md](docs/download-mode.md)。
- Flash 默认固定 DIO / 40MHz / 16MB，构建后核验 sdkconfig、bootloader/app 镜像头及实际生成的分区表；NVS/PHY与factory起始地址保持，factory为`0x10000`/`0xFF0000`并覆盖到16MiB末尾。构建入口必须覆盖旧容量缓存；不把旧固件40MHz配置解释为80MHz不受支持。
- BLE 不得用 AP 配网替代。沿用官方 Security 1 与每会话随机6位数字 PoP（保留前导0）；客户端步骤、安全与持久化限制见 [docs/ble-provisioning.md](docs/ble-provisioning.md)，不得宣称 NVS 已加密。
- 配网两次准备帧全刷确认并释放 View/UI 后才启动无线；活跃期只更新固定面板，不重建完整 AOT 或自动保存。退出等明确资源释放后才恢复，不能用 !active 推定安全；门禁见 [docs/staged-provisioning.md](docs/staged-provisioning.md)。
- 配网成功/失败/结束通过同步桥写入有界状态通道；回调不得执行生命周期、Rust UI 或阻塞等待。停止失败须保留资源，不发成功 END、不自动重试或释放仍使用的依赖；人工清理入口不绑定普通取消、关机或自动 step。

## 验证要求

- 实现改动运行相关行为测试及 C3 构建；最终交付运行完整宿主测试。报告精确命令、结果和未验证项，测试替身不等于真实无线/RTOS。
- 状态机至少覆盖翻面/跳过不评分、四选一正确判断、每题每轮单次评分、鼓励页不自动跳题、电源长短按互斥、退出恢复及缺失/损坏卡组。
- 渲染改动检查生成 PBM；用 `BALABALA_COMPARE_PAGES` 指定已核实存在的冻结页目录，重放相同 View 输入比较。基准与允许变化见 [docs/acceptance.md](docs/acceptance.md)，不能把预期页面变化当成全部逐字节一致。
- BLE 生命周期改动保留注册失败回滚、满队列、在途请求、同步重开、停止错误保留所有权、END 单次和关机失败可响应的测试；不能只用不保存 endpoint 私有指针的替身验证释放安全。
- 板级、显示、按键、存储及 BLE 改动须记录相应真机结果；宿主与构建通过不能替代动态 heap、连续块、栈高水位、广播关闭或官方 iPhone 端到端验证。验收状态以 [docs/acceptance.md](docs/acceptance.md) 为准。
- 文档修改检查相对链接与本地路径，区分本项目实测、外部参考和待验证设计。

## 注意事项

- 烧录前核实具体设备、Flash/分区、USB 锁定、完整备份和恢复路径；不得直接套用上游地址或解锁步骤。锁定机型可能失去恢复能力。
- 引入代码、字体和素材须核验许可证、固定版本并保留归属，见 [licenses/README.md](licenses/README.md)。
- 不在代码、示例卡组或日志中保存真实 Wi-Fi 密码；PoP 也不得写入常规诊断日志。底层 BLE stop 永不返回仍是已记录限制，不把处理返回错误描述成解决全部停止故障。
