# 原版 X4 平台运行时

[固件入口](../firmware/main/main.c) 已串行接通 SD 挂载、卡组导入/双槽恢复、输入采样、SSD1677 全刷/普通页面局刷、存档节流、BLE 配网和关机。实际控制策略位于可宿主执行的 [balabala_runtime.c](../firmware/main/balabala_runtime.c)，通过 [应用 C ABI](../firmware/main/balabala_probe.h) 操作 Rust 模型。[配网文档](ble-provisioning.md) 提供官方 iOS 路径、固定组件和安全边界。最终传输转换相对旧输出旋转180度后，已部署修正版并由用户确认屏幕方向正确；phase2 z 的预热＋3轮自动生命周期通过，证据与报告更正见[当前验收](acceptance.md#当前分阶段配网验证)。旧16位PoP普通 z 候选已独立 Review、app-only 部署校验，用户随后确认官方 iPhone 配网成功；6位PoP及后续存档保护修复均通过独立 Review；当前修复候选已app-only部署、独立Flash校验和20秒启动观察，证据在 `.build/storage-protection-device-20261028T014614Z/`；SD挂载和首屏刷新通过，6位手机交互待确认。真实按键、SD可靠性、睡眠/唤醒、其余无线场景、完整运行内存和性能不由此推导通过。

## 输入服务

纯状态机位于 [balabala_input.h](../firmware/main/balabala_input.h) / [balabala_input.c](../firmware/main/balabala_input.c)。`balabala_input_init()` 接收可选映射配置，`balabala_input_sample()` 接收单调毫秒时间、两路原始 ADC 及各自有效标记、电源 GPIO 电平，并同步回调按下/松开事件。`balabala_input_to_app()` 可直接把事件转交 `balabala_app_key()`；刷新繁忙时也应继续采样和传递松开。

引脚和 ADC 区间参考固定上游 [InputManager.cpp](https://raw.githubusercontent.com/open-x4-epaper/community-sdk/7d86603ad27709a9a766bb5ad893cfc39e60777e/libs/hardware/InputManager/src/InputManager.cpp) / [InputManager.h](https://raw.githubusercontent.com/open-x4-epaper/community-sdk/7d86603ad27709a9a766bb5ad893cfc39e60777e/libs/hardware/InputManager/include/InputManager.h)，许可归属见 [第三方说明](../licenses/README.md)。以下 ADC1/ADC2 指两组按键网络；在 C3 上 GPIO1、GPIO2 实际都属于硬件 `ADC_UNIT_1`，通道由 IDF 的 GPIO 映射查询取得。

| 网络 | GPIO | 12bit raw 区间 | 临时逻辑映射 |
| --- | ---: | --- | --- |
| ADC1 | 1 | 3100 < raw ≤ 3900 | Front1 |
| ADC1 | 1 | 2090 < raw ≤ 3100 | Front2 |
| ADC1 | 1 | 750 < raw ≤ 2090 | Front3 |
| ADC1 | 1 | 0 ≤ raw ≤ 750 | Front4 |
| ADC2 | 2 | 1120 < raw ≤ 3900 | Up |
| ADC2 | 2 | 0 ≤ raw ≤ 1120 | Down |
| 两组 ADC | 各自 | 3900 < raw ≤ 4095 | 无键 |
| 电源 | 3 | 0 按下，1 松开 | Home / Shutdown |

**上述顺序只是 ADC 分段的默认配置，物理左右和上下尚未核验。** 不依据上游 Left/Right 名字认定机身方向；实机确认后通过 `BalabalaInputConfig` 集中修正。六个普通键须唯一对应 Front1–4、Up、Down。

每组 ADC 独立做 30ms 去抖，换键先释放原键再按下新键。无效标记或 raw 超出 0–4095 时，该路保留已确认状态并取消尚未确认的候选，重新取得有效采样后重新去抖；读错不会伪造按下或松开，另一组和电源继续处理。时间必须单调，服务不自行等待时间。

电源也做30ms去抖；从确认按下时起计1500ms，达到阈值只发一次Shutdown，松开不再发Home。短按仅在确认松开时发Home；松开候选期间暂停长按，并按开始松开的时刻计算持续时间，避免1499ms松开因释放去抖变成长按。Front3单独计2000ms，最多发一次Parent脉冲；模型只在首页开始且已就绪的返回按压期间接受，其他页面长按不会穿透进入家长区，须松开重按。两个阈值独立，Home/Shutdown/Parent均以按下/松开脉冲传入模型；输入故障和释放边界由宿主时序测试覆盖。

[IDF 适配](../firmware/main/balabala_input_idf.h) 的 init 配置 12bit、12dB attenuation 和电源输入上拉；poll 用 `esp_timer_get_time()` 限为最多每 5ms 各读一路一次，不 delay、不等待按键释放、不创建轮询任务。`adc_oneshot_read()` 本身同步完成一次转换，错误作为该路无效样本返回；不能从 ISR 调用。这是供周期调用的接口，ADC 标定、阈值容差、噪声、真实键序和实际采样耗时仍须实机验证。

## 存储服务

[balabala_storage.h](../firmware/main/balabala_storage.h) / [balabala_storage.c](../firmware/main/balabala_storage.c) 使用 C POSIX 文件操作，`balabala_storage_init()` 复制调用方给定的挂载根目录。服务不挂载 SD、不探测 SPI 引脚、不创建根目录；不存在或不是目录时报告 `UNAVAILABLE`，应用仍可使用内置内容。IO 回调可逐项替换，用于实际临时目录上的故障注入；未设置的回调使用默认 POSIX 实现，close 回调即使报错也必须关闭流。

存储可用时调用顺序为 create → `balabala_storage_import_decks()` → `balabala_storage_restore()`，随后在合适的时机调用 `balabala_storage_save()`。真实入口在 SD 未挂载时跳过导入/恢复且调用 `balabala_app_save_protect(app)` 设置模型会话保护，C 的 `sd_ready` 另阻断全部平台保存。所有调用必须与模型、输入及显示操作串行；存储服务自身不包含后台任务；节流和重试由下述运行时处理。

| 数据 | 根目录下固定路径 | 单文件上限 |
| --- | --- | ---: |
| 卡组 | `/balabala/decks/hanzi.json` | 65536 B |
| 卡组 | `/balabala/decks/pinyin.json` | 65536 B |
| 卡组 | `/balabala/decks/english.json` | 65536 B |
| 卡组 | `/balabala/decks/math.json` | 65536 B |
| 存档槽 0 | `/balabala/saves/slot0.bin` | 32768 B |
| 存档槽 1 | `/balabala/saves/slot1.bin` | 32768 B |

四个卡组文件依次读取，每次先检查普通文件和长度，再分配实际文件长度的输入缓冲，完整读取并确认 EOF，关闭后提交 `balabala_app_import_deck()`，返回后立即释放。科目和内容校验仍由包中的 subject 及 Rust ABI 负责，C 不另写 JSON 解析器。缺失保留当前/内置卡组；损坏、未知字形、超长、字体或内容容量超限时保留旧内容和稳定 ID 进度。逐文件结果保存在 report 中。入口调用 `balabala_storage_show_import_error()`，仅把 TOO_LARGE / IO_ERROR / NO_MEMORY 映射到 `balabala_app_storage_error()` 的可退出“卡组无法读取，请检查存储卡”页；CONTENT_REJECTED 保留 Rust 具体错误，ABSENT 与未挂载不报损坏。后续合法科目导入仍完成替换并保留已有错误页；restore 仅在有效存档的 ID 可由当前卡组或已成功导入的科目解释时恢复，同时保留错误页；不能解释时保护原文件，修复卡组后重启恢复。

两个存档槽各自检查长度和 IO 状态，坏槽不会阻止另一个槽被读取。只有两次 open 都明确为 ENOENT 时，才报告 `FIRST_BOOT` 并跳过 ABI load；空文件、权限错误、短读、超长均不算首次启动。C 的 `FILE_READ` 只表示完整读取，CRC、版本、语义及最高有效 generation 仍由 Rust 双槽加载判断。通过 IO 检查的两个输入缓冲须同时保留至该 ABI 返回，随即释放。任一槽 IO_ERROR / NO_MEMORY 时，未读槽的 generation 未知，不能用部分可读槽恢复并解锁保存；平台提交失败 load，报告 `DAMAGED` 并保护原档。确认 CRC/语义/长度无效的槽仍可降级到另一有效槽。没有有效存档或最高有效档的 ID 不能解释时，也报告 `DAMAGED`，模型提供可退出错误页。root 不可用时静默保护，不新增错误页。

保护存在时，`balabala_app_save_export()` 返回 -3，不写文件且不清 dirty。保护位属于模型，不因 C 服务重新 init、后续 mutation 或重置解除；它会清除 pending，迟到 ack 不能清 dirty，只有成功 load 可解除。

保存时仅在 dirty 状态分配 32768 B 导出缓冲，按 ABI 返回的非活动槽及 generation 写入，顺序为 write → fflush → fsync → close → 以 256 B 小块读回逐字节比较并确认 EOF/close → ack(1)。任一步失败均 ack(0)，保留 dirty，并保持另一有效槽的文件不变。下一次调用可以重试。目录不存在时只在已提供的根目录下创建 `balabala/saves`；服务不初始化挂载介质。

`report.status` 提供 READY、UNAVAILABLE、FIRST_BOOT、RESTORED、DAMAGED、SAVED、SAVE_FAILED、CLEAN；`decks[]` / `slots[]` 提供未检查、读取成功、不存在、超长、IO 错误、内存不足或内容拒绝。`last_errno` 和 `app_result` 分别记录本次服务的文件错误与应用 ABI 结果。返回 0 表示成功/无需处理，1 表示可恢复问题，-1 表示参数错误。独立文件结果应与总体结果一起判断，例如 RESTORED 仍可能包含另一个超长或 CRC 无效的槽；任何 IO/内存失败槽会阻止恢复。`balabala_app_storage_status()` 的 AVAILABLE / UNAVAILABLE / FAILED 分别用于正常提示、“存储不可用”、“保存失败”；状态相同不提升 revision，提示不修改存档 dirty，也不覆盖已有错误页。AVAILABLE 只代表存储服务可用，成功保存仍由双槽 ack 证明。

## SPI、SD 与显示

实现前直接读取固定上游源码：[community-sdk EInkDisplay.cpp](https://github.com/open-x4-epaper/community-sdk/blob/7d86603ad27709a9a766bb5ad893cfc39e60777e/libs/display/EInkDisplay/src/EInkDisplay.cpp)、[SDCardManager.cpp](https://github.com/open-x4-epaper/community-sdk/blob/7d86603ad27709a9a766bb5ad893cfc39e60777e/libs/hardware/SDCardManager/src/SDCardManager.cpp) 和 [X4Term HalGPIO.h](https://github.com/penk/X4Term/blob/bd2f028bd57aebb8d0580dfb9b608c869bb521a3/lib/hal/HalGPIO.h)。community-sdk 的 MIT 归属见 [许可](../licenses/README.md)；X4Term 固定树未发现许可文件，只使用其引脚事实，没有移植代码。

| SPI2 / 屏幕信号 | GPIO |
| --- | ---: |
| SCLK | 8 |
| MOSI | 10 |
| MISO | 7 |
| EPD CS | 21 |
| EPD DC | 4 |
| EPD RST | 5 |
| EPD BUSY | 6，HIGH 表示忙 |
| SD CS | 12 |

启动时先把两个 CS 设高，再创建共享 SPI2 总线。先使用 IDF `SDSPI_HOST_DEFAULT()` 的 20MHz 安全默认频率挂载 FAT SD 至 `/sdcard`，关闭 `format_if_mount_failed`，不自动格式化；随后添加 mode 0、10MHz 的 EPD 设备。参考源码的 40MHz 未采用，真实信号质量验证后再决定提速。无卡或挂载失败先重新配置 SD CS12 为输出高，再提示存储不可用并使用内置内容。GPIO配置/置高失败会记录并禁用共享总线的后续EPD使用；释放总线失败也记录，不假定IDF cleanup保留输出状态。本阶段不自动重挂载热插拔介质。

[balabala_display_rotate_row()](../firmware/main/balabala_display.c) 是纯函数：逻辑帧为 480×800、行优先、MSB 左、1 白 0 黑；输出每行 800 像素 / 100 字节，`out(x,y) = logical(479-y,x)`，即传输坐标的逆时针 90 度变换。旧版 `out(x,y) = logical(y,799-x)` 已由用户实机确认上下倒置；当前转换相对旧输出旋转 180 度，修正版部署后用户已确认屏幕方向正确。发送行 y 仍从 0 到 479，控制器 Gate 地址仍由 479 递减到 0，不能把传输行 0 直接视作机身顶部。四角、非对称点、逻辑/输出跨字节边界和 7×11 棋盘测试检查坐标与位序，并逐像素核验新输出等于旧传输帧的 180 度旋转、区别于单轴镜像。传输只使用一个 100 B 行数组，唯一逻辑帧仍为 48000 B，渲染 stripe 仍为 7680 B；逻辑页面、overlay 坐标、键位与帧位序保持。

[显示序列](../firmware/main/balabala_display.c) 全刷时完整 reset/init：RST 高至少 20ms、低至少 2ms、高至少 20ms；0x12 soft reset 后等待；0x18=80、0x0C=AE C7 C3 C0 40、0x01=DF 01 02、0x3C=01。全屏 area 为 0x11=01、0x44=00 00 1F 03、0x45=DF 01 00 00、0x4E=00 00、0x4F=DF 01，0x46/0x47=F7 分别等待完成。每个 RAM bank 重新设置 area，再分别通过 0x24、0x26 发送同一帧的 480 个旋转行。全刷固定 0x21=40、0x22=F7、0x20；F7 包含刷新后关闭 analog/clock。这条全刷传输序列保持逐字节一致。

符合条件的普通页面更新允许局刷，包括普通页面之间切换（不比较像素差异或面积，换整张卡也算）：area → 0x24 写 480 个旋转行 → 0x21=00 → 0x22=DF → 0x20 → BUSY 完成 → area → 0x26 写同一帧。局刷不 reset/init，避免 0x46/0x47 清掉旧帧；旧帧保存在控制器 0x26 RAM，刷新完成后再同步为下一次基准，不增加第二块 48000 B 帧缓冲。固定 community-sdk 提交的 `EInkDisplay.cpp` 第 907–935 行给出 single buffer 的 FAST 写入/同步顺序，第 1105–1144 行给出 CTRL1=00 和关屏起步、刷完关屏、无自定义 LUT 时的 `0xC0 | 0x1C | 0x03 = 0xDF`。未引入 0x32 自定义 LUT、0x1A 温度欺骗、HALF 或灰阶。

Rust 在 begin_render 记录本帧 Page，只有精确 revision 的 complete 成功才更新上次成功显示页；`balabala_app_refresh_partial()` 在 render 成功后查询，仅前后均为普通页面（排除 Network、DownloadPreparing、DownloadPrompt、Error、Sleep）时返回 1，首次无成功显示页时返回 0。runtime 固定保存该值并透传给每次尝试，配网准备帧/固定面板及刷机准备/最终提示始终要求全刷。每次刷新前平台通过 `balabala_app_clear_interval()` 读取家长选择的 8 / 16 / 32 / 64 / 128 次上限（默认 8），写入显示对象；驱动仅在允许局刷、baseline_valid 且连续局刷次数小于该上限时走 DF，否则全刷 F7。修改上限不清零累计次数，调小后已达上限则下一帧全刷。每次尝试开始即清基准有效位，整条序列（含局刷后的 0x26 同步）成功才恢复；全刷成功计数归零，局刷成功加一。display_sleep 无论结果均清有效位，因此开机、休眠后、任何 IO/超时失败后的下一次尝试都全刷。各档上限的实际残影、现有边框 0x3C=01、DF 与 FF 的取舍、残影和约 0.5s 目标耗时均待真机标定。

每个 BUSY 等待先让出 5ms 供信号拉高，然后每 5ms 服务输入并 `vTaskDelay`，最长 30000ms。FreeRTOS 配置为 200Hz，reset 的 2ms 最低脉冲向上取整至一个 5ms tick。每行 SPI 数据后也服务输入，输入回调只投递 key / 收集平台请求，不嵌套渲染、保存或 SPI。逻辑帧和 revision 在物理完成前保持不变；只有成功返回才 `display_complete(revision)`。SPI 错误或超时后保留同一帧，间隔 1000ms 重做完整 reset/init，最多三次尝试（初次 + 两次重试）。耗尽后进入明确 `display_fault` 并只记录一次终态日志，停止自动刷新；仍采样并处理 Home/Shutdown，没有帧队列或自动重启。

## 主循环、保存与关机

调用顺序为 create → SPI2 / SD mount → import → 呈现真实读取失败 → restore → input init → 首次 render / full refresh。之后 [balabala_runtime_step()](../firmware/main/balabala_runtime.c) 依次采样/收集请求，在主任务安全点推进网络生命周期并合并状态，再按待显示 revision 渲染并按刷新策略全刷或局刷，最后检查 dirty 保存。所有 App ABI 在同一主任务执行，显示等待期间的导航只合并为最新状态。ADC 初始化失败时仍采样 GPIO3，保留电源关机入口。

保存成功后的最小间隔为1000ms，失败或存储不可用后的重试间隔为5000ms；不会因dirty连续忙写。成功save_ack后才能清dirty。配网前仅dirty时保存；配网独占面板及资源可能驻留时暂停自动保存。存储与配网提示纳入页面字体预算，83807 B门禁不变；新页面按mono覆盖、旧冻结模板按原等效口径计费。

关机在网络明确released后只尝试一次保存，先等待已有pending帧完成其有界刷新重试；仅保存返回成功、模型dirty已清、存储可用、网络无故障、没有在途帧或下载事务时，`balabala_app_sleep_ready()`才切到静态休眠页。runtime记录该目标revision，强制全刷，并在刷新失败时沿用1000ms间隔、最多三次尝试；只有pending清除、目标revision匹配且模型收到精确显示确认后才display_sleep，随后等待电源释放稳定30ms再进入MCU deep sleep。保存失败时提交错误提示帧并等待同样的显示确认；既有故障或重试耗尽时保留原故障关机退路，不无限等待。不得把逻辑图像生成或失败的display尝试记录为物理休眠图已显示。

## 省电与自动休眠

普通固件通过 `CONFIG_PM_ENABLE=y` 与 `esp_pm_configure()` 启用 DFS，最高频率沿用 CPU 默认 160MHz、无锁空闲时最低 40MHz。RTOS 在任务就绪时持有 CPU 最大频率锁，SPI/无线等 IDF 驱动按需持有 APB/CPU 锁；保留 5ms ADC 采样与显示 BUSY 服务周期。构建入口同时覆盖旧 sdkconfig 的 PM 关闭缓存，后置检查要求 PM 启用且 `esp_pm_configure` 和输入活动接口已链接。主栈、单帧与 stripe 大小保持。

本轮 `light_sleep_enable=false`，不启用自动轻度睡眠。GPIO1/2 是电阻分压 ADC 按键，GPIO 电平中断不能可靠区分全部按键；延长轮询周期会改变短按响应。因此本轮通过闲时降频与长期无操作深睡降低功耗，保留原有输入时序。IDF 电源管理依据来自固定工具链的 `docs/en/api-reference/system/power_management.rst` 及 `components/esp_pm/include/esp_pm.h`，还须测量板级实际电流。

`balabala_runtime_activity()` 在物理输入进入应用门禁前更新无操作时间与按住掩码；未改变页面的按键、松开、BUSY期间输入同样有效。`balabala_input_idle()` 同时检查两路 ADC 的已确认值和去抖候选，以及电源已释放；IDF 适配还要求采样成功、ADC无错误与原始电源高电平。输入异常不推定无人操作。

`BALABALA_AUTO_SLEEP_MS=300000`。原版X4的GPIO20高电平表示USB供电，接线事实来自固定X4Term源码[HalGPIO.h](https://github.com/penk/X4Term/blob/bd2f028bd57aebb8d0580dfb9b608c869bb521a3/lib/hal/HalGPIO.h)中的`UART0_RXD=20`和[HalGPIO.cpp](https://github.com/penk/X4Term/blob/bd2f028bd57aebb8d0580dfb9b608c869bb521a3/lib/hal/HalGPIO.cpp)的`isUsbConnected()`高电平判断。普通固件将其配置为无上下拉输入，经`external_power`回调读取；检测初始化失败时禁用自动休眠，保留手动关机。接电期间持续重置计时，充满但仍接电也不自动休眠；拔线后重新计满五分钟。只有普通固件、全部输入释放、无线明确完整释放且无故障、无在途显示/刷新需求、无下载/关机事务时才累计自动休眠时间；忙态结束后重新计时。期满先保存并检查返回值及dirty，失败或缺少ack就取消自动关机、保留当前页与提示，五分钟后才再尝试；常规dirty保存仍按原有1000/5000ms间隔运行。自动关机不合成Home/Shutdown按键，不修改当前轮次；成功后沿用精确休眠revision全刷确认、最多三次刷新尝试、屏幕休眠与电源释放30ms门禁。保存、休眠帧刷新、关屏返回后和MCU深睡前均复核外部供电；若接电则撤销自动休眠及旧休眠帧确认资格、恢复可操作首页。手动长按仍可在接电或保存失败时按原策略关机。

诊断固件不提供自动休眠的 `input_idle` 回调，避免自动驱动流程被无操作计时中断；DFS保持启用。软件证据与尚未验证的省电量、ADC/无线共存和唤醒见[省电优化验收](acceptance.md#省电优化验收)。

## 刷机模式

下载入口在任何保存/分配前先要求网络明确 released 且 !active；否则只记录内部失败、保留物理帧，不触发生命周期或延后重放。资源已释放时复用既有保存、网络清理和显示序列；只有精确提示 revision 已成功全刷、显示休眠成功并再次复核取消/关机后，才设置 C3 强制下载位并重启。当前使用 DIO / 40MHz / 16MB，保留NVS/PHY与应用起始地址，factory扩至Flash末尾；布局与设备记录见[16MB扩容验收](acceptance.md#16mb-flash-扩容验收)，下载入口的软件证据、取消/失败边界和待真机项见[刷机模式](download-mode.md)。下文 BLE 最终尺寸及80MHz为该阶段历史冻结值，不代表当前下载构建配置。

## BLE 配网

[网络控制器](../firmware/main/balabala_network.c) 使用既有 START/CANCEL 请求位及 NETWORK 状态；[IDF 适配](../firmware/main/balabala_network_idf.c) 管理官方 Security 1 + 非空 PoP、NimBLE、FULL 与 scheme NONE；app 同步终态桥只发布无凭据事件。仅在 Wi-Fi start 成功后通过 `esp_fill_random()` 生成 uint32 随机值，拒绝末尾不完整区间后对1000000取模并写为6位ASCII数字PoP（含前导0），原17字节C缓冲先全部清零，尾部保持NUL，设备名为 `PROV_` 加 MAC 后三字节。新增 `balabala_app_network_info()` 仅传设备名和 PoP，不传 Wi-Fi 密码。默认事件回调不参与权威状态；RECV/SUCCESS/FAIL/END 经 manager 同步 app callback 在短临界区进入 generation 状态位通道，不读取凭据、不调用 Rust/manager、不等待队列。默认队列满或 END 观察副本丢弃也不影响权威终态。

启动顺序为 dirty 保存→空身份准备面板真实 ack→prepare/backup/Wi-Fi start/身份→真实身份准备面板真实 ack→manager init/start。每个 ack 后重新采输入并复核 generation、精确 revision、阶段、请求和原300秒截止；UI/View已释放。实际广告成功后才显示等待手机，RECV显示连接网络。两准备帧以外只无分配修改原帧的标记/擦除矩形，状态变化仍全刷并等待真实 ack。几何与状态表见[分阶段配网](staged-provisioning.md)。

`runtime_service()` 仅输入/收集，显示回调不启动或关闭网络。`runtime_step()` 串行处理 manager 生命周期；正常收到 END 后 deinit；停止错误由显式查询发现，人工诊断入口可单次重试 deinit。每会话独占并最终删除默认事件循环，结合 generation 隔离旧事件，清理未完成时不启动新会话。成功前 300 秒到期请求 stop；GOT_IP 后停止该超时，等待官方手机查询 stop 或成功后约 30 秒 autostop，再做异步清理。重复状态不提升 revision，避免无效刷新；退出、取消、回桌面和关机均请求关闭入口，含成功页 Front4；安全释放前保存退出意图并停留固定面板，不先改 Home 或假 ack。尚未 stopping 时，同批 SUCCESS 先于 CANCEL/SHUTDOWN 提交，SUCCESS/FAIL 同批以成功为准；stopping 后拒绝迟到成功。取消/超时页面允许后续清理错误转为失败提示，离页不再更新。

manager 本地补丁先 STOPPING 后锁外注销 handler/排空 BLE，私有 STOP 投递失败按原 cleanup_delay 周期重试并用单调 token 拒绝迟到事件。资源清理、私有注销/timer 停删、IDLE 后锁外同步 END，旧栈随后不再碰 ctx，允许完成后重开；保留一个 singleton mutex 保护等待者。三个订阅与回滚均改为独立 instance，成功注销后才清句柄；异常按普通 fatal 处理，不虚报 END。三注册失败先注销实例、锁外成功排空 transport 后才释放 endpoint 表/pc/PoP；停止错误保留依赖及 STOPPING，不 END、不自动重试。cleanup_error 只读查询与 end_pending 分别跟踪清理故障和一次 END；deinit 非 OK 仍有资源，观察副本投递失败不改变释放成功返回。IDF instance 在回调内仅逻辑失效，不分配/投递；派发/profiling 结束且 tick 返回前按身份重扫删除，legacy 不变。END 表示资源释放和订阅逻辑失效，物理节点由当次派发尾回收；同 loop 同步 END 可 deinit/init/start，不支持回调销毁运行中的 loop。固定 IDF 补丁将七处 host 观察通知改为不等待，协议状态/返回不变，防止 host 与默认循环满队列反锁。STA 使用底层 new/attach/handler 注册逐步检查，不经会 abort 的便利创建路径；部分 attach 失败仍回收，清理错误锁止重开。

打开入口前在平台 RAM 备份旧 Wi-Fi 配置；失败/取消/超时清理后恢复，成功保留新配置。备份失败不继续启动；可见清理/恢复错误显示失败并锁止本次开机的重开。Wi-Fi、netif、handler、默认循环和自有 NVS 引用在正常清理后释放；每一步成功才清所有权标志，失败保留后续依赖，控制器明确记录 released，不能用 !active 代替。manager 未创建时取消直接释放，不等待不存在的 END；transport 故障保留旧配置和安全依赖，禁止提前 restore/release；`esp_netif_init()` 的基础 lwIP 状态/任务可能驻留。正常收尾清零临时敏感缓冲，故障时立即清身份显示副本，不记 SSID/密码/PoP 日志，不向 SD 写凭据；NVS init 失败不自动 erase。当前 NVS 未加密，断电期间不保证旧配置回滚。固定 ZIP、许可证、官方 iOS 手动操作及详细边界见 [BLE 配网](ble-provisioning.md)。

## 关机


收到 Shutdown 时先请求关闭网络并等待明确资源释放；若 manager 或平台资源清理失败，撤销本次 shutdown，保留固定清理失败面板和输入响应，禁止完整 AOT、自动保存与 sleep，不保证继续学习。原配置 restore 失败但资源全部释放时可显示完整错误页，仍锁止配网重开。主线程内部 `balabala_network_retry_cleanup()` 只供人工诊断、不绑产品按键；清理成功仍保持开机 fault，且不重放旧 shutdown。新的关机请求在清理后才尝试一次保存，不受常规节流限制；保存失败/无卡保留 dirty，记录返回值，并在能渲染时完整显示相应提示。若当前显示已经超时或失败，不伪造完成确认。随后屏幕执行 0x21=40、0x22=03、0x20 并有界等待，最终尝试 0x10=01 deep sleep；即使显示关屏失败也记录错误并结束等待。等待 GPIO3 原始高电平稳定 30ms 后，仅一次配置 IDF 6 的 `esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL<<3, ESP_GPIO_WAKEUP_GPIO_LOW)`，再进入 `esp_deep_sleep_start()`。保持长按时每 tick 继续调度，关机保存后忽略新的普通按下、仍处理释放与电源事件。唤醒配置失败只记录并停留服务循环，不反复调用。没有使用 GPIO13，也没有宣称切断电源。

## 内存与栈

输入状态无动态分配；卡组输入逐包释放，四个示范文件的 C 缓冲峰值6841 B。保存分配32768 B既有输出缓冲，Rust保留payload序列化、直接写header/payload，不再另建完整record；读回只用256 B栈块。恢复仍同时持有最多两份32768 B输入，先检查外框/CRC并按generation排序，同代槽0优先，逐一decode/语义验证，失败释放后回退，不同时持有两个Snapshot。没有64KiB栈数组。

已知字段借用RawValue检查 `6*n+2` 原始跨度后解码，在String拥有型分配前检查长度/格式；集合visitor提前限制40卡/160进度/10轮次项，按科目剩余额度逐项计费。解析scratch、Vec扩容以及未知/转义字段名错误仍有临时分配，活动内容32768 B限额不等于全流程heap上界。

字体通过[固定本地补丁](../patches/README.md)从 Flash 构造当前页 cmap，bitmap 直接借用 Flash，去掉序列化子集与 normal_text 副本。当前 24px / 48px 使用 DCFA v3 的 `FLAG_MONO1 = 2`（头部 byte 13），density=1；每字形按行保存 `ceil(cellWidth / 8)` 字节，高位在左，覆盖度 `>=128` 为 1，软件 raster 读成 255，否则为 0。16 字节头、8 字节 cmap 及旧 8bit 加载路径保留，mono 不支持动态 streaming。实际字档合计 1470432→190062 B，节省 1280370 B Flash；5 号槽 / 14 号槽分别为 68494 / 121568 B。

第1阶段字体逻辑预算按未压缩覆盖面积计费，`16 + g × (8 + cellWidth × cellHeight)`及原83807 B门禁不变；下述60072 B及mono s / z尺寸均为当时冻结值。新UI的mono计费、四字号与当前构建见[学习应用](learning-app.md#内容包)及[UI验收](acceptance.md#ui-与-ux-重写验收)。第1阶段完整内置学习页最大逻辑预算为60072 B；逻辑预算与实际cmap分配、Flash大小分别报告。mono 普通 s / z BIN 实测为 1862336 / 1774624 B，原 3MiB factory 剩余 1283392 / 1371104 B，后置配置、镜像头及分区检查通过。相同冻结输入的 46 张 PBM 逐字节一致，本轮未访问或烧录设备，不据此推导运行 heap、连续块或栈预算闭合，见[mono 验收记录](acceptance.md#mono-字库第-1-阶段验证2026-10-28)。

第二轮仅为正确反馈按实际 AOT 投影节点数预留 Taffy 容量，避免布局过量扩容；414 个视图核对真实节点数，hint 不足仍正常扩容。单帧 48000 B、stripe 7680 B 和 65536 B 主栈配置均未改，未尝试 UI 构造栈优化。

### 当前运行内存证据

节点精简后的配网面板宿主 render 峰49877 B、最大请求22672 B，活跃 overlay 更新零分配。方向修正版 phase2 z 真机四次稳定 Home 的 INTERNAL|8BIT free/largest 均为104348/29696 B，主任务高水位未用栈29732 B，累计内部8bit最低空闲21876 B；原65536 B主栈保持。证据为 `.build/display-orientation-device-20261027T140747Z/`，须同时阅读其报告更正。该轮未见 OOM 或逐轮 Home 空闲量下降，但不覆盖全部瞬态峰值、其他任务栈、Security 1 手机握手或长期运行。当前 Wi-Fi static RX 为6，NimBLE runtime mempool/nonreuse、单连接及两个 activity 等有效配置见[BLE 配网](ble-provisioning.md#nimble-按需内存配置及历史候选)。

### 早期运行内存阻塞证据（历史）

以下保留早期 BLE 阶段的静态布局、10个static RX及未上机结论，不是当前配置或候选状态。

**该阶段实例及 transport 修复已完成独立局部复审、全量宿主/PBM 回归及 C3 交叉构建；当时运行内存可行性未证明，平台资源验收阻塞。** [第二轮内存记录](memory-round2.md)列出字体、建树、布局、绘制阶段peak/最大请求/驻留、相同输入前后对比与C3诊断；[第一轮记录](memory-round1.md)保留解析、恢复和保存的前一轮结果，不混用不同fixture数字。

当前 BLE 构建静态 DRAM 190673 B，size 表 remain 130623 B 不是运行 heap 上界。加入无线前的第二轮原始 D/IRAM 候选 280368 B、RTC 候选 8112 B 是历史链接值，不能作为本轮无线余量。主栈配置 65536 B，IDF 另加 512 B，实际动态请求 66048 B，另有 TCB/元数据，不在静态 Used 中；没有降低此配置。

本轮新增任务的源码/配置栈请求：NimBLE host 4096 B，BT controller 3584+512=4096 B，默认事件任务 2304+512=2816 B，lwIP tcpip 3072+512=3584 B。Wi-Fi 内部任务栈由固定闭源库创建，尚无运行任务列表或高水位证据，不能把这四项当作完整任务预算。Wi-Fi 仍用 10 个静态 RX、32 个动态 RX 和 32 个动态 TX 默认上限，实际分配、并发峰与碎片须实测；manager 另用定时器/底层协议资源，未创建项目自有网络任务。

内置英语正确反馈含Model/View/render生命周期宿主峰140049→125169 B，下降14880 B，raster结束驻留136781→121901 B；原瞬时峰出现在DrawList扩容，Taffy容量提示降低前序驻留，本轮未修改DrawList。同一近双限额样本内容32217/32768 B、当前页字体76328 B、完整卡组最大77176/83807 B，其完整峰仍152415 B、最大请求68880 B为最终bitmap（82×24×35），没有收益。以上为 64 位宿主请求；C FILE、FatFS、SPI/DMA、IDF 任务/中断、元数据/碎片、实际 App Box 及本轮无线动态分配均未闭合，不能据此宣布 C3 可运行。

### 设备运行诊断

`CONFIG_BALABALA_MEMORY_DIAGNOSTICS` 默认关闭，启用后在main启动关键阶段、render开始、字体/建树/布局/绘制/raster结束、display/save返回采样INTERNAL|8BIT free/minimum/largest与主任务stack watermark；该IDF水位单位为byte。Rust allocator仅在OOM打印请求size/align与最近阶段的同类采样后abort，不在每次正常分配时扫描或记录。标签固定，不包含卡组或凭据内容。

largest-block查询会遍历pool，水位也扫描填充值，阶段开关仅供调试；阶段结束样本不能捕获所有瞬时连续块峰。当前构建启用了开关，已有上述指定候选的真机采样；未做本轮真机OOM注入或全部任务高水位验收。实现和开关见[main.c](../firmware/main/main.c)、[Kconfig.projbuild](../firmware/main/Kconfig.projbuild)。

## 早期 BLE 验证与交接（历史）

以下尺寸、80MHz配置和待设备验证结论属于 `.build/ble-final-*` 历史冻结值；当前方向修正版默认 s 为3141936 B、phase2 z 为3061104 B，均为 DIO/40MHz/4MB，生产默认仍 s。当前验收与设备授权范围见[验收文档](acceptance.md)。

在项目根目录执行 `python3 tools/test-build-env.py`，3 项环境隔离/失败传播检查退出 0；随后执行 `BALABALA_COMPARE_PAGES="$PWD/.build/ble-baseline/pages" sh tools/build.sh test -- --nocapture`，33 项 Rust（无过滤）、19 项 C 输入/存储、17 项 runtime、16 项配网控制器、5 项 IDF 适配、8 项 manager 函数级、1 项 IDF 通知及 14 组完整事件循环/protocomm 集成全部通过。C 用 ASan/UBSan，Rust 本身未插桩；runtime 连接真实 Rust ABI。锁竞态使用真实 mutex/condition barrier 与 3 秒期限，Security1 密码学、无线/RTOS/timer 边界为替身。冻结 View 输入 34 张 PBM 全部逐字节一致；当前模型输出与原基准为 33/34 一致（`network.pbm` 预期变化），另 6 张配网页验证字形、设备名和 16 位 PoP。当前 40 张 PBM 与 View 均来自本次宿主运行，冻结于 `.build/ble-final-pages/`；历史 PNG 预览不作为本次证据。

`sh tools/build.sh build` 通过实际 C3 编译、ELF 链接及分区/尺寸检查。资源检查发现未被产品调用的 `balabala_network_retry_cleanup()` 被链接器 GC；在既有 CMake 保留符号后再次构建退出 0，ELF 可见该 80 B 入口，没有新增产品调用。最终 BIN 3099536 B，3MiB factory 分区余 46192 B；Flash Code 1298794 B、Flash Data 1685904 B，静态 DRAM 190673 B，比加入 BLE 前增加 78080 B。相对首版 BLE 历史值 BIN 3097152 B / DRAM 190657 B，分别增加 2384 B / 16 B。framebuffer 48000 B、stripe 7680 B、主栈配置 65536 B、内容 32768 B 和字体 83807 B 门禁保持。

固定工具链为 ESP-IDF 6.0.3 / `76f5dedd9950a3012fee8fb7d5586df21fc67802`、Rust 1.99.1、Bun 1.3.14、RISC-V GCC 15.2.0；实际配置启用 `CONFIG_BALABALA_MEMORY_DIAGNOSTICS`。最终 `.build/ble-final-firmware/balabala_microts_probe.bin` 的 SHA256 为 `6d02cc893b803a64df71dec467ef80525121562c9b44b2d418a3cab6b3e99577`。同目录保存 ELF、map、bootloader、分区表、sdkconfig 与逐文件摘要；esptool 离线解析确认 C3、checksum/hash 有效。4MB / DIO 80MHz / 最低芯片修订 v0.3 / factory 偏移 0x10000 是构建设置，尚未核对具体设备，不据此直接提供烧录操作。

精确 argv、时间与退出码位于 `.build/ble-final-{env,host,c3}.json`，对应日志为同名前缀 `.log`；资源/PBM/栈宏与可解析栈报告位于 `.build/ble-final-resources.json`、`.build/ble-final-page-comparison.json`、`.build/ble-final-stack-command.json`、`.build/ble-final-stack-report.txt`。反汇编仅给出 `app_main` 固定 frame 256 B、可解析直调用累积 35280 B（存在环与间接调用），不是栈上界或高水位证明。最终摘要和相对本次验证前快照的差异为 `.build/ble-final-evidence.json`、`.build/ble-final-review.diff`；根目录没有 Git 元数据，不伪称 Git HEAD diff。既有 transport 设计/独立复审证据仍单独保留，完整范围见 [BLE 配网](ble-provisioning.md)。

剩余软件验收为主 Agent 对最终摘要、保留诊断符号的局部 CMake 改动与文档进行验收，随后同步项目指导；本轮未修改 AGENTS.md。无线动态内存、连续块和新任务高水位、底层 stop 永不返回、实际射频关闭/重开与释放结果、官方 iPhone 端到端均未验证。4MB Flash / 3MiB 分区仍为构建假设，当前 factory 余量约 45.1KiB；运行内存和设备资源验收继续阻塞。本轮没有访问硬件、烧录或修改外部参考项目。
