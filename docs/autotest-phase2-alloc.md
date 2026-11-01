# Phase2 有界分配诊断

该诊断只在 `BALABALA_AUTOTEST_PHASE=2` 的独立测试固件启用，帮助采集真实失败分配的请求大小、能力集合和阶段。默认固件与 phase1 不注册失败回调、不编译诊断模块、不输出新增字段。它不改变 Wi-Fi/BLE 内存配置、栈、字体、初始化顺序或 Security 1 协议，不增加清理、重试或重启入口。

## 回调与同步边界

固定 IDF `76f5dedd9950a3012fee8fb7d5586df21fc67802` 的 `components/heap/heap_caps.c` 在 `heap_caps_malloc_base()` 返回空指针后同步调用注册回调；`heap_caps_base.c` 的普通分配路径通过 multi_heap 分配器尝试各 heap。非 ROM 的 `multi_heap_malloc_impl()` 返回之前会释放 heap 锁，但 C3 还可使用 ROM allocator，且分配入口可能来自任务或 ISR，不能据此保证所有回调上下文都适合扫描 heap、打印或获取其他锁。

应用通过 `heap_caps_register_failed_alloc_callback()` 注册回调，不修改 IDF wrapper，不启用 heap tracing。回调向内部 DRAM 中的固定四条记录写入 `size/caps/diag_phase/generation`，锁存首错；第五次及以后只置 `overflow=1`，不增长计数或覆盖首四条。同时仅将首条失败保存到独立的 24 字节 RTC noinit 记录。`function_name` 参数既不解引用也不保存。hook 不分配、释放、查询 heap、输出、写文件或调用网络生命周期。

同步限定为原版单核 C3，编译期检查目标和 `FREERTOS_UNICORE`。使用固定 IDF 的 `portSET_INTERRUPT_MASK_FROM_ISR()` / `portCLEAR_INTERRUPT_MASK_FROM_ISR(previous)`；这与 heap 自身使用的 FreeRTOS 临界区同属中断阈值保护，避免自旋锁和递归获取 heap 锁。`port.c` 将前一阈值原样保存和恢复；`interrupt_intc.h` 临时清除 MSTATUS.MIE 后把阈值固定设为 **4**，再恢复原 MIE 位，并非无条件开中断，也不是 `max(old, 4)`。进入时阈值若高于 4，期间会暂时降低阈值，不能宣称任意高优先级 ISR 安全。使用范围限定为允许 FreeRTOS/heap 调用的任务与普通 ISR；不得从不允许调用它们的高优先级中断分配。诊断临界区两侧另有编译器 memory barrier。固定 IDF `components/freertos/linker.lf` 将两项原语放在 `noflash_text`，应用 hook 标为 `IRAM_ATTR`，所用记录和阶段标为 `DRAM_ATTR`；冻结 ELF/反汇编用于核查最终代码与数据位置以及 hook 无额外调用。

主任务先在同样的短临界区内复制 DRAM 记录快照，退出后才查询 heap 和使用 `esp_rom_printf()` 输出固定数值字段。注册失败也使诊断失败并停止脚本推进，不能当作无 OOM 继续运行。DRAM 初始化清零只在注册 hook 前执行，不清除待消费 RTC 证据；一次测试运行中不清除 OOM 锁存。

## 首失败 RTC 证据生命周期

RTC 记录由链接器独立分配为 `RTC_NOINIT_ATTR`，依次为六个 `uint32_t`：`magic/size/caps/phase/generation/checksum`，静态断言共 24 字节，不使用硬编码地址，不改既有 40 字节自动测试 RTC 对象。`magic=0x42414601` 标识本格式及版本 1；checksum 对 magic 与四个载荷字段进行固定 XOR/轮转计算，用于识别半写与损坏，不是认证机制。

hook 在既有中断阈值临界区内判断首错锁存，使用与 DRAM 首条相同的 phase/generation 快照。先写 magic=0，依次写四个载荷与 checksum，最后提交 magic。volatile 32 位对齐读写配合三处 `fence rw,rw` 和编译器 memory clobber，分别保证失效先于载荷、完整载荷先于提交、提交先于退出；没有分配、查询、输出、锁或等待的新调用。后续失败即使 RTC 已消费也不能在同次启动再次保存。checkpoint 已从主任务传入网络代次，hook 不读取网络状态。

每次 phase2 启动，`app_main` 在诊断 init、注册 hook 和自动测试 start/event110 入口之前读取 RTC，复制到正常任务栈并清除 magic，再按公共 `esp_reset_reason()` 的实际值决定是否输出。仅完整版本与 checksum 均有效，且 reason 为 `SW/PANIC/INT_WDT/TASK_WDT/WDT` 时输出一条 `alloc_rtc=1`；包含 panic hint 转换后的 `INT_WDT`，不只接受 `SW`。POWERON、USB、BROWNOUT、DEEPSLEEP 及所有其他原因的残留直接消费丢弃，不能被当成本轮故障。损坏记录也被消费且不输出。消费发生在任何输出前，因此输出后再次复位不会重放该条；有效记录只允许排出一次。

有效来源是**前一次 phase2 启动期间本 hook 记录的首条失败**，不表示当前启动或当前自动测试已开始。generation 是该次启动内最近一次 checkpoint 的网络启动代次，初值 0；它既不是启动编号，也不是持久会话标识，必须连同上一启动日志与 reset reason 解读。phase 初值同为 0，之后保留最近边界，不能据此断言失败时网络仍 active。未运行本启动早期消费路径的其他固件不构成可接受的采集链。若启动尚未到达消费点便再次复位，记录可能保留更久；自然复位到达本路径是后续采集的前提。

固定 IDF 的 [memory-types 文档](../.tools/esp-idf/docs/en/api-guides/memory-types.rst) 说明 C3 RTC FAST 可作为 DRAM 使用；[C3 memory_layout.c](../.tools/esp-idf/components/heap/port/esp32c3/memory_layout.c) 将 RTCRAM 标为 `MALLOC_CAP_INTERNAL`，该能力在 [esp_heap_caps.h](../.tools/esp-idf/components/heap/include/esp_heap_caps.h) 明确定义为 flash/cache 关闭仍可访问。`.rtc_noinit` 是 NOLOAD，启动代码只清 `_rtc_bss_start.._rtc_bss_end`；RTC 记录按实际链接地址从 RTCRAM heap 保留范围扣除。最终 ELF/反汇编仍需核对 hook 全部指令、RTC/DRAM 位置和调用目标。以上为固定 SDK 与离线构建证据，真实 cache 关闭、跨自然复位保留仍待真机验证。

恢复证据不改变原自动测试门禁：warm reset 仍由既有 start 返回拒绝并进入 event110，不能开启新的 BLE、恢复或清理。POWERON/USB 是否开始新测试仍由原策略独立决定。

## 字段和阶段

串口保留既有 `autotest phase=2 ...` 输出，新增下列固定字段：

| 字段 | 含义 |
| --- | --- |
| `alloc_rtc=1 magic size caps diag_phase generation checksum reset_reason same_instant=0` | 前一次启动首失败的完整 RTC 数值记录与当前实际复位原因；仅通过校验和来源门禁后输出一次，没有失败瞬间 heap 样本 |
| `alloc_hook=1 result capacity same_instant=0` | 回调注册真实返回码、固定容量 4 |
| `alloc_fail=1 index size caps diag_phase generation same_instant=0` | 第 `index` 条失败，`size/caps/diag_phase/generation` 来自回调；没有失败瞬间 heap 样本 |
| `alloc_post=1 index caps free largest same_instant=0` | 主任务排出该记录时按相同 caps 后采样；对应 `alloc_fail` 的 index |
| `alloc_overflow=1 capacity` | 超出记录容量；只输出一次，不统计无限失败次数 |
| `alloc_sample=1 diag_phase generation result caps free largest same_instant=0` | 调用边界的主任务采样；`result` 为该边界真实返回码，调用前为 0 |
| 既有 `event=20/23/24/21/25/26` | generation/start、start 返回码、stop、END、deinit 返回码/计数、release 返回码/计数；完整 64 位值仍以高低 32 位输出 |
| `event=113` | OOM 锁存或 hook 注册失败使脚本停止；`a_lo` 是 OOM 锁存，`b_lo` 是注册返回码的 uint32 表示 |

阶段采样不调用 start、stop、finish 或 retry_cleanup。每个阶段都采样 `INTERNAL|8BIT` (`2052` / `0x804`)、`INTERNAL|DMA` (`2056` / `0x808`)、`RETENTION` (`16384` / `0x4000`) 的 free/largest：

| diag_phase | 采样位置 | 下一边界前失败记录的阶段含义 |
| --- | --- | --- |
| 0 | 未进入本轮网络启动，无边界采样 | 开机或网络启动之外 |
| 1 | 真实 prepare 前 | prepare 执行中 |
| 2 | prepare 返回后 | backup 或 radio_start 执行中，或 prepare/backup 早期失败后的自然回收期间 |
| 3 | radio_start 返回后 | identity 或 manager_init 执行中，或 radio_start/identity/manager_init 早期失败后的自然回收期间 |
| 4 | manager_start 前 | manager_start 执行中，包含 controller 初始化 |
| 5 | manager_start 返回后 | 返回后的正常处理或失败自然回收中 |
| 6 | 失败分支的 finish 自然返回后 | 失败回收已返回；不代表清理成功，仍须看 cleanup_error、fault 和返回码 |

prepare、radio_start 或 manager_start 返回错误时，先采样该返回边界，再按原有流程进入自然失败回收；phase6 的 result 保留触发失败的原始返回码。阶段 2、3 是区间标记，不能把其中所有 OOM 都归到该阶段名称指向的单个函数。

`same_instant=0` 明确表示 heap 查询与失败分配不在同一瞬间。各 caps 依次查询，同一行的 free/largest 也为先后两次查询；串口输出和其他任务可能改变间隔。**不得用后采样的 free 与 size 或 largest 与 size 直接证明失败瞬间是总量不足或碎片问题。** 该证据用于确定真实请求和收窄时段；没有采到失败瞬间的 free/largest 就保留未知结论。

## OOM 后行为和后续采集协议

OOM 单调锁存后，脚本在 tick、每次 emit 前及最终 Home 采样返回后检查，停止新增注入和下一轮；最终采样内即使返回原来的 free/largest，也必须在 cycle 推进或 `event=8` 成功输出前报告 `event=113`。不自动 retry_cleanup、不重启。已经进入的真实 start/finish 保持原有返回与所有权语义，允许自然返回并排出记录；诊断停止不触发新的取消或清理。主任务及既有网络事件处理仍可运行，因此自然 END 或原本的超时行为不被诊断替换。清理错误仍保留资源和 fault，不将其解释为成功释放。

OOM 或注册失败使脚本停止后，任务继续只读采集网络观测：首次输出完整的既有 `event=20/23/24/21/25/26` 快照，之后仅在 generation、start/stop/END/deinit/release 计数或三个返回码发生变化时输出完整快照；逐项比较字段，不比较结构体 padding。缓存属于当前 phase2 脚本实例，初始化重置。无变化时不重复输出，不恢复测试推进；迟到的自然 END 和 runtime 回收仍可被观察，本轮失败结论保持。

后续真机采集必须先通过独立代码 review，再按设备审核流程决定是否烧录。本实现任务不访问设备、不烧录，也不改采集器。未来采集器应遵守：

1. 全程只使用一个串口连接。识别首个 OOM 后立即停止测试推进，保持失败结论。
2. 保持同一连接，进入最长 30 秒的只读尾部采集，收集自然 start 返回、回收、END/deinit/release 及诊断记录。
3. 不自动 retry_cleanup、不复位、不重启、不关闭再重开串口，不开启下一轮。自然回收成功也不把本轮 OOM 结论改为通过。
4. 30 秒窗口内底层仍未返回时，保留“窗口内未返回”的事实；若设备自然复位，继续在同一窗口读取启动早期 `alloc_rtc=1` 及 event110，不主动制造复位。hook 的 RTC 首条不依赖 controller 返回，DRAM 记录仍依赖应用安全点。没有可信记录不能解释为没有 OOM，也不能推断死锁或清理结果；hook 内不输出串口。

本 RTC 方案的成功标准是：start 从不返回并发生自然复位后，仍在启动早期取得校验通过、来源可信的真实 `size/caps`。原安全点的 DRAM 输出可以补充信息，但不替代这项跨复位验证。只有 OOM/watchdog/event110 而没有可信记录，仍为观测失败。RTC 没有保存失败瞬间 free/largest，仍不能用边界或后采样推断碎片与总量不足。

宿主替身只能验证容量、锁存、脚本门禁和既有自然返回/保留所有权行为，不能替代真实 ISR/RTOS/cache、无线 heap、栈高水位或官方 iOS 端到端验证。

## R3 构建成本与默认隔离

以下为 R3 与 R2 冻结 ELF 的实际比较；大小以 ELF 段、符号及最终 BIN 为准，不代表运行时可用 heap 或栈高水位：

| 项目 | R2 phase2 | R3 phase2 | 增量 |
| --- | ---: | ---: | ---: |
| BIN 字节数 | 3135232 | 3135680 | 448 |
| 距 `0x300000` 上限 | 10496 | 10048 | -448 |
| `.flash.text` | 1308870 | 1309092 | 222 |
| `.flash.rodata` | 1710580 | 1710708 | 128 |
| `.iram0.text` | 100290 | 100390 | 100 |
| `.iram0.text` + 尾部对齐 | 100352 | 100864 | 512 |
| `.dram0.data` | 14737 | 14737 | 0 |
| `.dram0.bss` | 76408 | 76408 | 0 |
| ELF `_heap_start` | `0x3fcaec20` | `0x3fcaee20` | 后移 512 字节 |
| `.rtc_noinit` | 40 | 64 | 24 |
| RTC 静态保留末端 `_rtc_force_slow_end` | `0x50000064` | `0x5000007c` | 后移 24 字节 |
| IDF size 所报 DRAM 使用 / 剩余 | 191435 / 129861 | 191535 / 129761 | 使用 +100 |

hook 代码增加 100 字节后跨越 IRAM 的 512 字节对齐边界，因此共享 SRAM 中 IRAM 保留增加 512 字节，普通 DRAM heap 起点也后移 512 字节，不能只看 `.dram0.data/.bss` 未增便称无 heap 成本。普通链接区域从实际 `_heap_start=0x3fcaee20` 到静态区域上界 `0x3fcce710` 的尾部范围为 129264 字节（R2 为 129776）；这只是链接布局范围，还未计动态分配和多个 heap 的能力划分，不能当作可用 heap。C3 的 IRAM 与 DRAM 共享物理 SRAM，IDF size 的剩余值未逐项计入全部对齐，不能将不同口径或两种视图相加。RTC 可用 heap 区域另减少 24 字节。

40 字节 `autotest_rtc` 仍位于 `[0x50000018,0x50000040)`，24 字节 `alloc_rtc` 由链接器放置于 `[0x50000040,0x50000058)`，SDK 的 36 字节 `.rtc.force_slow` 移至 `[0x50000058,0x5000007c)`，互不覆盖。上述地址仅为本次 ELF 核验结果，没有写入实现。

原 DRAM 记录 `observation` 仍为 80 字节，phase/generation 共 8 字节，`autotest` 对象仍为 264 字节。hook 位于 IRAM `0x403823f2`，由 84 增至 184 字节，局部栈帧由 32 变为 16 字节；三处 `fence rw,rw` 的失效、载荷和提交顺序可在反汇编复核。hook 仍只调用 `xPortSetInterruptMaskFromISR` 并尾跳 `vPortClearInterruptMaskFromISR`，没有新的外部调用。保存的阈值由原语原样恢复，原语固定将临时阈值设为 4；高优先级 ISR 限制见上文。正常任务的 RTC 消费函数局部帧为 48 字节，phase2 `app_main` 从 304 增至 320 字节；这些是函数局部静态帧，不能当作含 ROM、heap 查询及中断在内的峰值。原 65536 字节任务栈配置未改，没有真实栈高水位结果。

默认 BIN 仍为 3128544 字节，距上限 17184 字节；默认全部 `SHF_ALLOC` ELF section 的集合、地址、大小和内容 SHA256 均与 R2 相同。BIN 相对 R2 只有 app descriptor 中 ELF SHA256 的 32 字节和末尾 checksum/SHA256 的 33 字节不同。默认 `_heap_start` 仍为 `0x3fcae9f0`，没有新增诊断符号、失败回调注册符号或新增诊断串口字串；phase1 宿主二进制也没有诊断符号。默认与 phase2 的 sdkconfig 配置项均与各自 R2 相同，包括 DIO/40MHz/4MB、Wi-Fi/BLE、栈及 heap 配置。

R3 已通过以下四项离线命令；精确环境变量、原始输出和退出码见冻结 README 与日志：

- 独立输出目录、phase2 环境的 `sh tools/build.sh test-autotest`：phase1/phase2 C ASan/UBSan 与 Rust 快照。
- `sh tools/build.sh test`：44 Rust 测试、完整 C 平台/runtime/network 及 patched 组件集成测试。
- `sh tools/build.sh build`：默认 C3 构建与尺寸/镜像头合同。
- 独立输出目录、phase2 环境的 `sh tools/build.sh build-autotest`：phase2 C3 构建与尺寸/镜像头合同。

新增测试走真实 hook、RTC 校验和消费函数，覆盖首条不覆盖、各六字段损坏、magic 提交前的全部写入前缀、init 不提前擦证据、含 INT_WDT 的 warm reset、POWERON/USB 等残留丢弃、消费一次及同次启动不重写、再次启动不重放、正常无记录，并核对真实 manager_start OOM 的 RTC/DRAM phase/generation 相同。真实 `balabala_autotest_start` 在 warm reset 恢复后仍拒绝，root 为空、不调用随机数、不改原 40 字节对象；源代码和最终 `app_main` 反汇编核对 event110 拒绝循环仍先于网络初始化。原 event113、自然 END、容量/并发、注册失败和所有权保留测试继续通过。宿主不是硬件复位，不能替代真实跨复位、cache/IRAM、ISR/RTOS、动态 heap/连续块、栈高水位及 iOS 验证。

新冻结在忽略目录 `.build/autotest-phase2-alloc-r3/`，包含五文件 before/source 和统一 diff、生成输入、配置、默认/phase2 BIN/ELF/map、R2 对照镜像、SDK 依据、反汇编、成本和隔离检查、全部日志及 SHA256 manifest。补测的一次局部变量重名编译失败已修复，失败日志与最终通过日志均保留。没有修改采集器，没有设备、串口或烧录操作；独立 Review 仍待进行。

原 `.build/autotest-phase2/`、`.build/autotest-phase2-alloc/`、`.build/autotest-phase2-alloc-r2/` 的 manifest 与各 174/229/245 项文件哈希均已核验未变，不覆盖历史设备采集目录。R2 冻结旧成本统计遗漏 BSS 尾部 8 字节对齐，R3 直接读取原始 ELF `_heap_start` 符号；原更正记录仍在 `.build/autotest-phase2-alloc-r2-device-20261027T041029Z/cost-correction.json`。当前工作区没有 `.git`，使用修改前快照作 diff；未执行暂存，无法通过 Git index 实测。
