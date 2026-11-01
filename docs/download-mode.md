# 刷机模式

家长设置的第五项提供普通 USB ROM 下载入口，第四项仍为蓝牙配网。进入后先显示中文二次确认，默认选中“取消，继续学习”；按键须释放并完成确认页显示后，才能选择“进入刷机模式”。下载准备不写入存档，正常启动始终回桌面。

准备期间保存进度并关闭 BLE；侧面电源短按或正面返回可取消，电源长按优先走原有关机流程。确认进入后的最终画面为白底黑字，只有居中的两行英文：

```text
Download Mode
Keep USB connected
```

提示沿用 48px / 24px 字体及 MicroTS AOT；无页眉、状态栏、按键提示或进度。ROM 接管后应用不再采样按键；退出刷机模式须按正常启动条件复位，不能假定任意 CPU reset 都能退出强制下载状态。

## 进入条件与顺序

[模型](../native/src/model.rs)只提交一次下载请求；[运行时](../firmware/main/balabala_runtime.c)在主线程串行完成以下步骤：

1. [C3 适配](../firmware/main/main.c)只读检查 eFuse。依次拒绝 `DIS_FORCE_DOWNLOAD`、`DIS_DOWNLOAD_MODE`、`DIS_USB_SERIAL_JTAG`、`DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE`，以及 `ENABLE_SECURITY_DOWNLOAD`。安全下载受限时显示“安全下载受限，无法使用普通USB下载”，不承诺支持其专用下载流程，不写 eFuse。
2. 执行现有双槽保存，要求返回成功且模型 dirty 已清除；无 SD、写入/落盘/读回/确认失败均阻止下载。即使模型原已 clean，也必须通过存储可用性检查。
3. 发一次 `NETWORK_CANCEL`，后续正常 step 等待 END、manager deinit 与依赖释放。放行条件为 `!active && !manager && !cleanup_error && !fault`；仅 `!active` 不够，恢复旧配置或释放失败同样阻止进入，不调用人工 `retry_cleanup`。
4. 通过 `balabala_app_download_ready()` 取得英文提示专属 revision，完整刷新并成功 `display_complete()`。旧帧 ack、渲染失败、SPI/BUSY 错误都不能放行。显示仍是初次加两次、共三次有界尝试；耗尽保留既有 pending/fault，不伪造确认。
5. `display_sleep()` 成功后再次服务输入，复核取消、关机、dirty、显示状态和网络清理条件，然后设置 `RTC_CNTL_OPTION1_REG` 的 `RTC_CNTL_FORCE_DOWNLOAD_BOOT` 并调用公共 `esp_restart()`。准备后的普通学习按下不改变进度，BUSY 和显示休眠期间仍接收取消、关机及松开。

任何前置失败都不会设置强制位。下载入口意外返回显示失败，并锁止本次开机的后续提交。持续显示屏障故障时继续服务电源，但不保证新的错误画面可见。网络底层 stop 永不返回仍是既有限制。

新增 ABI、请求及错误枚举见 [balabala_probe.h](../firmware/main/balabala_probe.h)。状态位 8 表示下载准备/提示事务，16 表示提示页；请求位 8 为下载、16 为取消下载。提示返回的 u64 revision 是提交依据，不能只检查当前是否存在画面。

## 固定来源与构建配置

最初实现依据 ESP-IDF `v6.0.3` / `76f5dedd9950a3012fee8fb7d5586df21fc67802` 的 C3 eFuse 描述符、`soc/rtc_cntl_reg.h` 和公共 `esp_restart()`；当前固定版本见 [versions.sh](../tools/versions.sh)，升级验证见[最新验收](acceptance.md#最新依赖升级验收)。入口不假设专用下载 API，不写 eFuse；C3 TRM v1.4 §7.2 与寄存器来源见[第三方说明](../licenses/README.md)。

源码 [sdkconfig.defaults](../firmware/sdkconfig.defaults) 固定 DIO / 40MHz / 16MB，不据此断言 80MHz 不受支持。当前 [分区表](../firmware/partitions.csv) 保留NVS/PHY与factory偏移 `0x10000`，factory大小为 `0xFF0000`，覆盖到物理16MiB末尾；配套配置、镜像头及设备验证见[16MB扩容验收](acceptance.md#16mb-flash-扩容验收)。下文4MB/3MiB尺寸为早期冻结候选。软件入口不代替设备 eFuse、USB 接口及正常启动恢复路径验证。

48px UI 字形由[明确字符源](../native/font-sources.json)保留旧 UI 全量覆盖并加入新 hero，卡组 front 仍由既有内容源加入；24px 继续扫描全部 Rust 文案。原有导入字形覆盖、83807 B 字体构造门禁、32768 B 内容门禁、48000 B 单帧、7680 B stripe 和 65536 B 主栈均保持。

## 软件验证与证据（历史）

以下测试数量、尺寸及部署状态属于下载入口和 Flash 字体修复的早期候选。最新回归、构建与部署见[最新验收](acceptance.md#最新依赖升级验收)，存档修复历史见[存档保护验证](acceptance.md#当前存档保护验证)。真实 ROM 切换、USB 枚举、屏幕保持和正常复位恢复仍待验证。

针对用例覆盖默认取消、释放门禁、每事务单次请求、不持久化、准确提示/13 种错误字形，以及实际条件分支的投影计数。runtime 使用真实 Rust ABI/显示序列与 ASan/UBSan，覆盖预检拒绝、无 SD/保存失败/dirty 未清、等待清理期间取消、restore/release/stop 故障、BUSY/休眠期间 Home/返回/Shutdown、旧 revision/失败 ack、重复请求、三次耗尽、sleep 失败及入口返回锁止。

完整命令为 `BALABALA_COMPARE_PAGES="$PWD/.build/ble-baseline/pages" sh tools/build.sh test -- --nocapture` 和 `sh tools/build.sh build`。本次 37 项 Rust、19 项 C 输入/存储、25 项 runtime、16 项网络、5 项 IDF、8 项 manager、1 项通知和14组事件集成通过。原34张冻结 View 输入 PBM 逐字节一致；当前模型旧34页有32页一致，`network.pbm` 的既有BLE身份信息与 `settings.pbm` 的第五项为预期变化。新增下载提示只有两条墨迹带，y=346–384 和431–454，整体中心400；每行水平中心偏差不超过4px，48/24px字体构造48136 B。

字体档从旧1437408 B增至1461104 B；避免把仅24px使用的新错误文案冗余烘焙到48px，较首次失败构建省74096 B。刷机模式原冻结包 BIN 为3128480 B（0x2fbca0），3MiB factory 余17248 B（0x4360，0.5483%，约0.55%）。该包已烧录且 verify 通过，但匹配 ELF 的启动日志记录首页字体16800 B连续分配失败，不能将软件检查或写入校验视为真实启动通过。

字体 Flash 借用修复的当前 BIN 为3128544 B（0x2fbce0），较上述历史包增加64 B，factory 余17184 B（0x4320，0.5463%，约0.55%）。ELF 静态 DRAM 为90596 B（`.dram0.data` 14553 + `.noinit` 3 + `.dram0.bss` 76040），IRAM `.text` 100144 B，另有208 B段末对齐；历史包与当前值相同。IDF size 表将共享 D/IRAM 的代码和数据汇总为190737 B、remain130559 B，该表不能作为运行 heap。栈配置及原内容/逻辑字体门禁保持，修复后的宿主/构建结果与风险见[字体 OOM 记录](font-flash-oom.md)。

原证据 `.build/download-final/` 包含历史固件/bootloader/分区表、sdkconfig/镜像头、PBM/View、字符差异、源码差异及 SHA256；该目录和 `.build/ble-final-*` 均保留。修复证据独立冻结于 `.build/font-flash-fix/`，完整宿主测试44项 Rust及上述全部C测试链通过，45份相同 View 的 owned / borrowed / 原 PBM 像素一致。修复轮没有访问设备或烧录，设备进入ROM的既有状态未更改；新固件未获得真机启动、动态 heap / 栈、eFuse、USB枚举、刷机模式进入/退出、屏幕保持与SD落盘验收。独立 Review 和主 Agent 上机验证仍待完成。
