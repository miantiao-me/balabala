# 第二轮内存归因与容量提示

本轮沿用 MicroTS strict AOT、Rust 模型、固定 PocketJS 和原渲染器。完整输入、业务限额、65536 B 主栈、48000 B 单帧缓冲与 7680 B stripe 均保持。第一轮结果见[第一轮记录](memory-round1.md)，本轮证据为 `.build/memory-round2-evidence.json`。**宿主降峰与 C3 链接已验证，C3 运行内存可行性仍未证明，平台资源验收继续阻塞。**

## 测量边界

[test-native-render.rs](../tools/test-native-render.rs) 在原线程独立请求计量上增加固定大小的 TLS 阶段记录，不在 allocator 回调中分配记录容器。保留完整生命周期 peak / 最大单次请求，realloc 保守同时计旧块与新块。每个阶段记录该区间的 peak、最大请求，以及阶段结束时全部已跟踪对象的 live；live 包含前面阶段留下的对象，不能把阶段 peak 相加。计量表本身和 TLS 不计入请求字节。

完整渲染场景从 `Model::new()` 前开始，包含内容、模型状态、场景内输入副本、View、Ui、Atlas、布局与绘制临时对象，结束于这些对象析构。fixture 原始数据、48000 B framebuffer 和 7680 B stripe 在计量外。Model 结构位于宿主栈；该场景不含实际 C3 App Box、C FILE、FatFS、SPI、IDF 任务/中断、TCB、allocator 元数据、对齐和碎片。真实 ABI 保存场景包含 App Box 和既有 32768 B 输出缓冲；恢复场景包含两个输入副本。各场景不能直接相加，也不能把 64 位宿主请求峰当作 C3 上界。

阶段端点来自真实生成 App 的调用路径：`model-core` 包含计量开始至 Ui 构造；`fonts` 截止最终字体注册；`tree-mount` 截止生成 `AppApp::new()`；`frame-update-layout` 截止 `App.frame()` 的 props 更新及 tick 布局；`draw` 截止 `core.draw()`；`raster` 截止所有 stripe 渲染；`cleanup` 截止对象释放。无渲染的恢复/保存/拒绝样本没有中间 checkpoint，其单个 `cleanup` 项覆盖整个操作。大于等于 8192 B 的重分配另记旧/新请求及重叠 live，固定记录区溢出会使测试失败。

## 归因与有界修改

相同内置英语正确反馈，原完整峰 140049 B 出现在 draw 阶段的 DrawList 4096→8192 B 重分配。此前布局阶段，Taffy SlotMap 从 17 槽扩为 34 槽，每槽 872 B，请求 14824→29648 B，旧新合计 44472 B，该阶段完整 live 峰 138153 B。Taffy 扩容不是最终瞬时峰的触发点，但扩容后驻留使后续绘制峰更高。

[固定补丁](../patches/pocketjs-layout-capacity.patch) 增加 `Ui::new_with_layout_capacity(nodes)` 和只读 `layout_node_count()`，复用 Taffy 原有容量及正常扩容机制。[build-native.ts](../tools/build-native.ts) 从实际 AOT AST 生成 `.build/generated/projection.rs` / `projection.json`：计入 core root、实际 View、非空 Text 和 success 条件下的鼓励图案；SlotMap 自加 sentinel，应用不重复增加。当前满文本正确反馈为 21 个投影节点，典型正确反馈为 18 个。生成器遇到不支持的元素、文本或条件结构以及 static/formula/region 投影会失败，避免猜测容量。

仅 `view.success` 的正确反馈使用提示，其他页面继续默认容量 16。当前模板最多为 core root 1 + 普通 View 3 + 非空 Text 10 + 鼓励 View 7；生成表达式按实际文本和条件求值，并非一律预留 21。414 个实际视图均核对计数等于真实 `TaffyTree::total_node_count()`；额外检查错误/正确反馈、空行/满文本及提示不足为 1 的真实生成 App，节点数和 DrawList 均一致。本轮未修改 DrawList、字体格式、raster 或 UI 语义。

## 同输入结果

内置正确反馈各阶段如下，单位为宿主请求 B。最大请求指阶段内单次分配大小，不是连续空闲块。

| 阶段结束 | 原 peak | 当前 peak | 原 live | 当前 live | 原最大请求 | 当前最大请求 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| model-core | 47423 | 49407 | 47423 | 49407 | 14824 | 16568 |
| fonts | 98239 | 100223 | 98223 | 100207 | 36960 | 36960 |
| tree-mount | 105559 | 107543 | 104023 | 106007 | 3200 | 3200 |
| frame-update-layout | 138153 | 112587 | 125263 | 110383 | 29648 | 6400 |
| draw | 140049 | 125169 | 136781 | 121901 | 8192 | 8192 |
| raster | 136781 | 121901 | 136781 | 121901 | 0 | 0 |
| cleanup | 136781 | 121901 | 0 | 0 | 0 | 0 |

提前按节点数预留使初期驻留增加 1984 B，避免后续过量扩容。完整峰 **140049→125169 B，下降 14880 B / 10.6%**；raster 结束时驻留 **136781→121901 B**。生命周期结束后 live 均为 0。全程最大请求仍为最终字体 bitmap 的 36960 B；当前 draw 阶段仍存在 4096→8192 B 重分配。

| 同输入场景 | 原完整 peak | 当前完整 peak | 当前最大请求 |
| --- | ---: | ---: | ---: |
| 内置正确反馈 | 140049 | 125169 | 36960 |
| 网络状态 1–4，各状态 | 110017 | 110017 | 34440 |
| 网络状态 5–6，各状态 | 106631 | 106631 | 34440 |
| 近内容上限与高字形 | 107783 | 107783 | 27720 |
| 同时接近内容/字体限额 | 152415 | 152415 | 68880 |
| 近内容上限 + 两个最大槽恢复 | 128577 | 128577 | 32768 |
| 最大进度/轮次的真实 ABI 保存 | 97815 | 97815 | 32768 |
| 最大包逐项预算拒绝 | 115953 | 115953 | 65536 |
| 新槽语义坏而回退 | 113821 | 113821 | 32768 |
| 超大已知字符串拒绝 | 88767 | 88767 | 59625 |
| 转义未知字段拒绝 | 134599 | 134599 | 60006 |

近双限额样本仍为内容 32217/32768 B，当前页字体 76328 B、完整卡组最大 77176/83807 B。**68880 B 是 82×24×35 的最终字体 bitmap，属于驻留对象，不能再称临时副本。** 这个非 success 样本没有收益，本轮不继续扩展优化。旧独立 render-only 口径（Model/View 已在计量外）最大峰 110684→95804 B；完整 22000 学习组合的对应最大 render 请求峰为 90299 B。不同 fixture/起止不能相互替换。

原始阶段及全场景记录：`.build/memory-round2-stage-before.json`、`memory-round2-scenarios.json`、`memory-round2-comparison.json`。优化前 34 张 PBM（33 份 View 输入及 1 张真实 ABI 错误页）固定在 `.build/memory-round2-baseline/pages/`；相同输入全部逐字节一致，包含第一轮新增的近双限额页。

## C3 诊断入口

[main.c](../firmware/main/main.c) 的 `balabala_memory_checkpoint(stage)` 使用固定阶段标签，不打印卡组或凭据。`CONFIG_BALABALA_MEMORY_DIAGNOSTICS` 在 [Kconfig.projbuild](../firmware/main/Kconfig.projbuild) 默认关闭；调试构建开启时，在 create、SD、import、restore、input/runtime 准备、渲染起点、字体/建树/布局/绘制/raster 结束、display/save 返回处采样 `INTERNAL|8BIT` free / minimum / largest block 与主任务 stack watermark。该 IDF 的水位单位为 byte。

Rust allocator 仅在分配失败时打印请求 size / align、最近阶段、上述 heap/stack 数值，然后 abort；与阶段开关无关。正常每次 alloc 不记录日志、不扫描 heap。IDF largest-block 查询会遍历 pool，stack watermark 会扫描填充值，因此阶段诊断仅供调试，存在耗时；阶段结束 free/largest 与最低 free 不能还原每个瞬时连续块峰。本轮实际 C3 构建启用了该开关，但没有设备运行或 OOM 注入，未产生真机样本。

## 构建与资源边界

项目根目录实际执行 `sh tools/build.sh test -- --nocapture`：30 Rust、19 C 输入/存储、11 C 显示/运行时均通过；C 使用 ASan/UBSan 连接真实 Rust ABI，Rust 未插桩。冻结 34 个相同输入 PBM 全部一致。补丁从固定上游 tar 依次回放并核对前后 SHA，幂等调用两次均通过；自动应用只写项目内 `.tools/pocketjs`。

诊断开启的 `sh tools/build.sh build` 完成 C3 编译、ELF 链接和分区检查。BIN **2142832 B / 0x20b270**，比第一轮增加 880 B，3MiB 分区余 1002896 B；Flash Code 569898 B、Flash Data 1513340 B，静态 DRAM **112593 B**（增加 8 B），size 表 remain 208703 B。32 个应用/平台公开函数和新增私有诊断桥符号均保留，framebuffer 48000 B / stripe 7680 B 可核对符号。

最终 ELF 固定帧：app_main 256 B、内部 render 28080 B、App render 入口 176 B；可解析直调用和分别为 app_main 35264 B、App render 34960 B，runtime_step 35008 B。主栈配置仍 65536 B，IDF 另加 512 B，启动反汇编仍向 `xTaskCreatePinnedToCore` 传入 0x10200，即动态请求 66048 B，TCB 另计。递归、间接调用和中断未闭合，固定帧分析不是栈上界证明。

最终 ELF 原始区域扣除静态 reserved 后：D/IRAM 候选 280368 B，RTC 候选 8112 B，总 288480 B；均未扣 allocator 元数据、启动动态分配与主栈。14576 B 启动栈已在 app_main 前回收，已包含在候选值中，不能再加。size remain 是链接报告口径，不能当作运行 free/largest block。

最终日志/产物为 `.build/memory-round2-{host-test,c3-build}.log`、`memory-round2-{stack,c-stack,symbols,disassembly}.txt`、`memory-round2-heap-regions.json`。未知字段 scratch、Vec 扩容、近限额最终 bitmap、无线并存预算、真机连续块/碎片和栈高水位仍未闭合；4MB Flash 与 3MiB 分区仍为构建假设。本轮没有 BLE、烧录、栈或内容限额下调、UI 构造栈优化及外部仓库修改。
