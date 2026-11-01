# 第一轮内存降峰与回归记录

本轮在固定 MicroTS strict AOT / Rust 模型上修复启动错误被成功导入掩盖的问题，并减少字体、双槽恢复、保存与输入拒绝的临时分配。原有双槽 C ABI、存档格式、65536 B 主栈、48000 B 帧缓冲、7680 B stripe、32768 B 活动内容与 83807 B 字体门禁保持不变。**宿主与 C3 构建通过，运行内存可行性仍未证明，平台资源验收继续阻塞。**

## 实现与数据生命周期

- [模型](../native/src/model.rs) 成功导入合法科目时仍更新内容/进度并提升 revision，同时保留既有 Error 页。完整启动回归覆盖先坏后好、有效 restore、错误退出和原轮次恢复；restore 没有因保留错误而跳过状态恢复。
- [入口](../firmware/main/main.c) 在 SD 挂载失败后重新配置 SD CS12 为输出高。配置或置高失败会记录错误并禁止后续 EPD 使用该总线，释放总线失败也记录，避免依赖 IDF cleanup 后已变成输入的 CS。
- [固定 PocketJS 本地补丁](../patches/README.md) 从 Flash 字档和排序去重字符集合直接构造最终 Atlas cmap / bitmap；不再序列化子集后复制。字体注册后的 revision、布局、raster 失效与原入口共用。应用按 View 的 title、正文、提示、按键字符串迭代收集字符，不再构造 normal_text 整串。构造公式按当前字符 Vec capacity、已驻留及新最终 cmap / bitmap 计量，与实际请求峰值核对。
- 恢复先对两个输入分别检查长度、版本、header、CRC，再按 generation 排序，同代槽0优先。一次只 decode 一个 Snapshot，语义失败释放后尝试另一个；候选通过前不替换模型。C 仍同时保留两个最多32768 B文件缓冲。
- 保存 `export_into()` 保留固定 serde_json 的 `to_vec()` payload，直接写调用方输出切片中的24 B header和payload，移除单独完整 record Vec。容量和全部序列化检查成功后才安装 pending_save，写入、校验及 ack 时序不变；没有假设 no_std 提供 to_writer。
- 已有 serde_json 1.0.150 仅增加 `raw_value` 特性，锁定版本及 Cargo.lock 不变。已知字符串先借用 RawValue，按最大合法 UTF-8 字节数 n 检查原始跨度不超过 `6*n+2`，再解码并检查长度、字符/ID格式，在拥有型 String 分配前计费。合法 Unicode 转义不被当成超长原文误拒。
- 卡组 envelope 先得到有界头字段和借用 cards；根据其科目及另外三科占用计算候选余额。卡 Vec 槽按实际 capacity、字符串按拥有容量逐项累计，确定超限即停止。cards最多40、progress最多160、round ID/answer最多10，visitor在额外元素处直接拒绝，不构造第41张/161条/11项。原子导入失败保留旧内容和进度。

已知字段转义解码仍可能使用 serde scratch；借用 raw 跨度是前置约束，并不等于零临时分配。候选 Vec 扩容还可能短暂重叠旧新缓冲。字段名、未知字段错误格式化及其转义 scratch 没有形成完整上界，下方专门记录一个未获改善的反例；没有实现通用 JSON 解析器。

## 同输入测量

优化前证据保存在 `.build/memory-round1-baseline/`，新增最大单块计量后、实施优化前生成 `scenarios.json`。当前对应结果为 `.build/memory-round1-scenarios.json`。相同 case 名使用相同输入和生命周期；下面单位均为字节。

| 场景 | 前峰值 | 后峰值 | 前最大单块 | 后最大单块 |
| --- | ---: | ---: | ---: | ---: |
| 内置英语正确反馈，含 Model/View/render | 140049 | 140049 | 37328 | 36960 |
| 配网页状态1–4，含 Model/View/render | 117153 | 110017 | 34784 | 34440 |
| 配网页状态5–6，含 Model/View/render | 117150 | 106631 | 34784 | 34440 |
| 近内容上限、固定较多字形题面，含 Model/View/render | 118499 | 107783 | 28000 | 27720 |
| 近内容上限、两份32768 B、各160条进度恢复 | 144657 | 128577 | 32768 | 32768 |
| 高进度/轮次存档，经真实 C ABI 保存 | 100545 | 97815 | 32768 | 32768 |
| 超长已知字段拒绝，输入59625 B | 155611 | 88767 | 59625 | 59625 |
| 转义未知字段名拒绝，输入约60KiB | 134599 | 134599 | 60006 | 60006 |

字体阶段下降不等于完整渲染同比下降：内置正确反馈的总体峰值由其它 UI/布局阶段决定，仍为140049 B。保存场景虽然消除了完整 record 副本，生命周期总峰只减少2730 B，不能直接把 record 长度当总峰收益。恢复同场景减少16080 B；转义未知字段名仍保持134599 B，属于后续解析资源风险。

原第二阶段独立压力场景也按相同函数重跑，结果在 `.build/platform2-memory-scenarios.txt`，优化前副本在 baseline 目录：

| 原压力场景 | 前峰值 | 后峰值 |
| --- | ---: | ---: |
| 17186 B包、替换后活动内容32369 B导入 | 69024 | 69137 |
| 65536 B有效JSON / 40张，容量拒绝 | 134422 | 115953 |
| 近上限后两份最大槽恢复 | 143217 | 127857 |
| 近上限错误反馈完整渲染 | 116001 | 111069 |

导入小样本增加113 B，来自新的受限解析临时对象，不能声称所有路径都降低。第二张表的恢复 fixture 与第一张表独立，不混用数字。新增拒绝场景最大单块为65536 B；CRC正确、语义坏的新槽单候选回退场景后峰113821 B、最大单块32768 B，没有伪造优化前测量。

## 字体、页面与边界组合

414个既有视图的字体构造峰由82110 B降至58376 B；完整内置学习组合的最大值由83807 B降至60072 B，实际分配与共享预算公式一致。83807 B仍是内容导入门禁，没有降低限额。旧日志保留的 normal/large 大小含16 B等效档案头；名为 peak-atlas-copy 的旧字段当前表示最终常驻payload，不表示仍有序列化副本。414个样本的 render-only 峰仍是110684 B，Model/View已在计量外存在；该数字不能和上表含模型的峰值相加。

固定旧输入的33张PBM逐字节一致。另新增 `.build/pages/near-content-and-font-limits.pbm`：活动内容32217/32768 B，当前题面字体76328 B、整卡组最大77176/83807 B；该组合当前通过全部导入校验，完整 Model/View/render 峰为 **152415 B，最大单块68880 B**。这是新的边界样本，不冒充优化前允许域内的样本，也不是全域最大值。它说明较丰富字形与接近内容上限同时存在时，仍有显著连续块要求。

字体 Flash 档案共1397792 B：24px 568字形 / 481680 B，48px 272字形 / 916112 B。源扫描包含新增Rust字面量，因此比前阶段档案增大；字档仍位于Flash，运行时仅为当前页构造最终Atlas。

计量使用64位宿主的分配请求字节，realloc保守同时计旧新，`largestRequest`是最大单次请求，不是实测最大连续空闲块。场景从计量器内创建 Model，包含其动态内容、状态、View及相应临时对象；输入用同尺寸Rust Vec模拟C文件缓冲。排除 Model 栈本体、实际C3 App Box、静态帧/stripe、C FILE、FatFS、SPI/DMA、IDF任务/TCB、中断、栈和分配器元数据/对齐/碎片，也未计BLE。这些样本不能转换成C3可运行结论。

## 最终验证与 C3 产物

项目根目录执行：

```sh
BALABALA_COMPARE_PAGES="$PWD/.build/memory-round1-baseline/pages" sh tools/build.sh test -- --nocapture
sh tools/build.sh build
```

29项Rust、19项C输入/存储、11项C运行时均通过，生成34张PBM。C测试使用ASan/UBSan并连接真实Rust静态库，Rust本身未插桩。新增覆盖：完整启动前坏后好+restore、同代槽0优先、新槽CRC有效而语义坏回退、全部候选失败不改模型、export容量失败不安装pending、直接Atlas格式/索引/排序边界、最大字符与UTF-8字段的全转义、三行显示约束继续生效、超数量集合、失败导入保旧内容。

C3实际编译/链接/尺寸检查通过，最终BIN **2141952 B（0x20af00）**，比前阶段增加40416 B；3MiB构建分区余1003776 B。Flash Code569016 B、Flash Data1513084 B、总image2133925 B。静态DRAM仍为112585 B（bss60576 / IRAM text45328 / data6681），size表remain208711 B；这不是运行heap上界。32个应用/平台公开函数保留，唯一framebuffer48000 B、stripe7680 B可从最终ELF符号核对。

最终固定序言：app_main256 B（前阶段240），产品render28080 B不变，App render入口176 B、可解析直调用和34960 B，app_main直调用和35264 B；runtime_step32 B / 35008 B，显示refresh144 B / 256 B，storage import320 B / 2544 B，restore320 B / 2832 B，save576 B / 1472 B。递归、间接调用及后续sp调整没有闭合，报告不是栈上界证明。本轮不优化UI构造栈。配置主栈仍65536 B，最终启动反汇编依旧向xTaskCreatePinnedToCore传递0x10200，即实际动态请求66048 B，另有TCB/元数据，不包含在静态Used中。

最终ELF原始内存region/reserved表与前阶段相同：D/IRAM候选280384 B，RTC候选8112 B，总288496 B，尚未扣启动动态分配及heap元数据；其中14576 B启动栈在app_main前回收，不能再次相加。运行heap、最大连续块、任务/中断栈高水位、SD与显示并存及未来BLE预算均须实机补齐。4MB Flash/3MiB分区未核实，没有烧录或操作外部参考仓库。

完整源文件与产物摘要在 `.build/memory-round1-evidence.json`；日志为 `.build/memory-round1-host-test.log`、`.build/memory-round1-c3-build.log`，栈/符号/size/原始region及PBM摘要位于同一忽略目录。固定依赖与本地补丁许可见 [第三方归属](../licenses/README.md)。
