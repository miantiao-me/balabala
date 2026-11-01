# Flash 字体借用与启动 OOM 证据

本记录区分已定位的旧包启动失败、修复后的宿主结果与待完成的真机验收。修复使用 MicroTS strict AOT、原 24px / 48px 字体和当前页字形筛选。83807 B 逻辑字体导入门禁、32768 B 内容门禁、65536 B 主栈、48000 B 单帧缓冲、7680 B stripe、DIO / 40MHz、4MB 镜像和原分区保持。

## 失败与修复边界

历史包 `.build/download-final/firmware/balabala_microts_probe.elf` 的 SHA256 为 `4736042cb377e16c4b3fe93fc63b309d7f19898f8e5af950fafd858129c11515`，与 `.build/device-boot.log` 的 ELF 标识匹配。该包写入及 verify 成功，真实启动在首页大字 bitmap 请求16800 B时失败；日志记录 free43424 B、largest15872 B。首页普通35字形 bitmap29400 B、大字5字形 bitmap16800 B，共46200 B。调用链为 `render → Ui::load_font_subset → Fonts::load_subset → Atlas::subset → Vec::with_capacity`。完整源字体虽已由 `include_bytes!` 放在 Flash，旧入口仍复制当前页 bitmap。

[固定第三补丁](../patches/pocketjs-static-font-subset.patch)新增 `Ui::load_font_subset_static(bytes: &'static [u8], chars: &[char])` / `Atlas::subset_static()`。[应用](../native/src/fonts.rs)只将生成的静态字档交给该入口，临时或可释放缓冲继续使用 owned 入口。Flash Atlas 仅拥有当前页 cmap，借用完整源 bitmap；cmap 保留源 gid，`glyph_count` 为源可索引范围。没有 `unsafe` 生命周期延长。

统一 `Atlas::bitmap()` 供 `glyph_rows`、core RenderResources、FontPages、FontView 和 raster 使用。streaming configure 通过该访问器读取基字形，随后切换到 owned 可写 bitmap；commit / compact 沿用写路径，未启用 streaming 的 detach 不解除借用。成功注册共用既有 `font_loaded`，字体 revision、布局 dirty 与 raster revision 随替换更新，拒绝输入不改变 revision。

owned 子集的既有缺字 fallback 是排序后的首个选中字形（子集 gid 0），未必是 U+FFFD；新入口保存其对应源 gid，使像素、advance 和 xoff 行为相同。该轮保留这个语义，未借机修改旧注释所称 tofu 行为。应用对真实缺字的导入拒绝仍生效。

## 字体逻辑规模和实际 heap

逻辑门禁仍按原 owned 构造估价，包括字形 coverage、cmap 和字符计划容量；`FontStats.logical_construction_budget` 保留这个兼容口径。`normal` / `large` 是逻辑序列化大小，`resident_cmap_bytes` 仅是 cmap 元素字节。它们不能代替实际 allocator 峰值，借用的 bitmap 不计作实际 heap 复制。

下表为同一 View、同一字档的64位宿主请求字节，使用线程隔离计量，realloc 保守同时计旧块与新块。字体区间从已有空 Ui 上开始加载到加载完成，含字符计划及最终 cmap；不含原 Ui 构造。bitmap 实际复制均为0 B。原 owned 字体由保留的 owned 入口重新测量。

| View | 逻辑门禁 B | owned 字体峰 B | Flash 字体峰 B | owned 最大请求 B | Flash 最大请求 B | cmap 驻留 B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 首页35+5字形 | 46552 | 46552 | 728 | 29400 | 448 | 320 |
| 近内容/字体双限额 | 76328 | 76328 | 1424 | 68880 | 768 | 672 |
| 内置最大字形规模页 | 60072 | 60072 | 728 | 36120 | 384 | 400 |
| 鼓励页 | 50800 | 50800 | 736 | 36960 | 384 | 384 |
| 下载提示 | 48136 | 48136 | 256 | 36960 | 104 | 192 |

近双限额样本实际导入内容32217/32768 B，当前页逻辑字体76328/83807 B，完整卡组最大77176 B；没有缩小其内容或字形支持。首页逻辑序列化为29696 B / 16856 B，46200 B coverage 借用 Flash。独立22000个学习状态和414个实际视图的检查保留：内置最大逻辑页60072 B；414视图的实际字体请求峰744 B，原冻结报告为58376 B。

原口径的完整 mount / draw / render 区间（不含 Model / View、framebuffer / stripe）最大请求峰从历史97484 B降至48054 B，当前最大案例为 english-03 正确反馈。相同 english-01 正确反馈的 Model / View / render 生命周期从历史126849 B降至76449 B；该口径仍不含 framebuffer / stripe、C3 App Box、平台与 allocator 元数据，不能和下面有界测试的绝对值混用。

## 碎片限制和完整生命周期

[test-font-flash.rs](../tools/test-font-flash.rs)在隔离子进程中使用[test-font-arena.rs](../tools/test-font-arena.rs)的有界 allocator。区域 `[15872, 15872, 11680]` 总43424 B、最大15872 B：同一大字5字形的 owned 路径以 `memory allocation of 16800 bytes failed` 终止；Flash 路径峰40 B / 最大请求40 B，析构后恢复43424 B。这个样本只验证分配形状能暴露旧请求失败，并非对真实 C3 碎片状态的等价重放，也不声称整个页面能在该容量运行。

另用4个65536 B区域、总262144 B，包住完整 Model / 内容 / View、生成 App / Ui / Atlas、48000 B framebuffer 和7680 B stripe。近双限额场景包含真实 import及输入副本释放；每个场景独立运行三轮，每轮结束 live为0，区域合并和全部容量恢复，三轮峰值一致：

| 场景 | 完整生命周期峰 B | 最大请求 B | 每轮释放后容量 B |
| --- | ---: | ---: | ---: |
| 首页 | 114843 | 48000 | 262144 |
| 近内容/字体双限额 | 133183 | 48000 | 262144 |
| 内置最大字形规模页 | 115253 | 48000 | 262144 |
| 鼓励页 | 131782 | 48000 | 262144 |
| 下载提示 | 110889 | 48000 | 262144 |

该合成 allocator 使用64位宿主对象布局，fixture原始数据和计量TLS在边界外，Model结构在栈上；不含C3 App Box、RTOS、BLE / Wi-Fi、FatFS / FILE / SPI、allocator 元数据及真实对齐损耗，不能当作C3内存上界或可行性证明。

## 像素和兼容性验证

45份 `.build/download-final/pages/*.view.json` 输入分别通过 owned / borrowed 渲染并与原PBM像素逐字节比较，全部相等；gid和DrawList字节不作为等价要求。另覆盖首页、近双限额、内置最大字形规模、鼓励与下载提示。原282个48px字形的独立保留断言来自[历史fixture](../tools/fixtures/legacy-large-glyphs.json)：原`.build/ble-final-firmware/balabala_microts_probe.elf`中独立提取的282字形cmap及原archive SHA256，避免只用当前字符源自证。

回归还覆盖空/乱序/重复/缺字子集、截断、非法源gid、源cmap顺序和slot、非0源gid、v3 density、fallback替换、owned源缓冲释放、font revision / dirty、FontView / FontPages资源边界，以及stream configure / commit / detach和重新owned加载。

根目录执行：

```sh
BALABALA_COMPARE_PAGES="$PWD/.build/download-final/pages" sh tools/build.sh test font_flash -- --nocapture
BALABALA_COMPARE_PAGES="$PWD/.build/download-final/pages" sh tools/build.sh test -- --nocapture
sh tools/build.sh build
```

最终完整宿主44项Rust测试，以及19项C输入/存储、25项runtime、16项网络、5项IDF、8项manager、1项通知和14组事件集成通过。C链包含既有ASan / UBSan；Rust未插桩。固定上游三项补丁顺序回放、每文件前后摘要及最终派生源码摘要另行核对；构建入口的幂等校验通过。生成PBM由完整字节比较检查，页面布局未改变。

## C3产物、栈与未验证项

当前BIN3128544 B（0x2fbce0），factory3145728 B余17184 B（0x4320，0.5463%，约0.55%）；历史download-final为3128480 B、余17248 B（0.5483%），当前增加64 B。实际sdkconfig及bootloader / app镜像头均为DIO / 40MHz / 4MB，分区表保持。

ELF分段静态DRAM为14553 B `.dram0.data` +3 B `.noinit` +76040 B `.dram0.bss` =90596 B；IRAM `.text`100144 B，段末另有208 B对齐。两项与历史包相同。IDF size表将共享D/IRAM计为190737 B / remain130559 B，其remain不代表运行可用heap。

主任务配置65536 B、IDF另加512 B，启动申请66048 B保持。最终反汇编固定帧：app_main256 B、内部render29120 B（历史28160 B，增加960 B）、ABI render176 B；runtime_step可解析直调用保守和36064 B，包含未必实际到达的分支。递归、间接调用、中断和后续sp调整未闭合，这些数值不是栈上界。旧OOM时高水位36180 B只描述失败前历史，不能证明新包后续动态峰。

独立冻结目录为`.build/font-flash-fix/`，包含firmware、实际sdkconfig/镜像头、源码差异、补丁回放、同输入PBM、实际heap报告、ELF段及栈反汇编、命令日志和SHA256；原`.build/download-final/`与`.build/device-boot.log`按完整摘要校验未变。根目录无Git元数据，本轮未建立仓库或stage任何文件。

修复轮未访问设备、未烧录或触发真实下载入口。新包真机启动、动态heap / 连续块、主任务与BLE栈高水位、无线共存、屏幕刷新及刷机模式完整流程待主Agent在独立Review后验证；已写入历史包的verify成功不能替代这些验收。
