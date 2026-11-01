# 发布前代码审查与最终验证

本备忘记录 balabala 本轮发布前审查的实现、取舍、证据和待验项。审查基线为 `3912a6b`；目标是原版 XTEINK X4 / ESP32-C3。代码已提交为 `116bcfd`，随后完成应用分区烧录与启动观察，见[部署记录](release-deployment.md)。项目尚未发布。

## 审查过程

先完成完整宿主基线测试并冻结可重放页面，再分别审查模型、渲染与平台构建。方案经独立顾问核对后分阶段实现；每阶段核对行为测试与页面输出，最后运行源码门禁、性能基准和 C3 构建。写手整理 README、架构与验收文档、第三方许可，主审再逐项核对源码与原始日志。

模型、渲染、源码完整性三个范围均完成只读独立复审，未发现须修复的 P1/P2 问题。这是限定范围的审查结论，不能证明不存在其他缺陷或替代真机验收。

## 发现与修复

| 问题 | 修复与验证 |
| --- | --- |
| 配网页按 Home / Shutdown 后可能同步返回首页，松开事件未清除自身 held 位，后续正面键持续被拒绝 | 只清除当前键的位，保留其他按住键的门禁；覆盖资源释放前后、两个电源意图以及其他旧键仍按住的组合 |
| 示范卡已掌握进度迁入完整课程后，未重新计算解锁单元，可能提前显示课程完成 | attach 成功后、load 完整读卡并提交状态后重算四科解锁；数学首单元覆盖 25 / 26 / 32 张已掌握及再次保存恢复，26 张对应 32 张的 80% 向上取整 |
| 首题正面向上翻、单项菜单导航和默认首页重复 Home 触发无效显示请求 | 只在对应无效操作处跳过 revision 更新；首题背面翻正、Done 返回、关机意图与父设置清理仍保留 |
| 课程 ID 查询反复线性扫描 4000 张目录 | 复用已有 FNV-1a 有序键表进行二分查询，最后核对科目与完整 ID；遍历全部 4000 张并覆盖未知、越界科目及真实哈希碰撞 |
| 折行分词为每个临时单元分配 String | 改为原文 UTF-8 字节范围，保留字形验证、标点禁则、空格与英文分词语义；新增边界测试并重放冻结页面 |
| 配网矩形填充逐像素写入，完整帧重复绘制状态面板 | 复用 overlay 按字节填充；完整帧仅补充必要的身份区域擦除，缓存帧继续更新状态与身份；81 组 ABI 帧保持一致 |
| 上游派生源码可能在锁定提交之外发生本地漂移 | 在补丁应用前验证 PocketJS 归档及派生成员、ESP-IDF 根目录与递归子模块；在 IDF Python 脚本执行前检查；18 个隔离用例覆盖放行与拒绝路径 |
| 根许可缺失，部分固件链接组件缺少分发文本 | 原创代码采用 Apache-2.0；补充 lwIP、NimBLE、FreeRTOS、TLSF、FatFS、protobuf-c、wpa_supplicant、工具链和 Rust 底层库的许可与归属 |

## 简化与架构边界

- [model/learning.rs](../native/src/model/learning.rs) 收纳内置轮次调度、选项重排、答题入口和复习计数；[model.rs](../native/src/model.rs) 保留状态机与按键入口。[model/course.rs](../native/src/model/course.rs) 继续负责课程正文、进度标记与单元解锁。外部 API 与存档格式保持。
- 内容计费复用单卡计费函数，并补计 Vec 空余容量；课程构建复用已有 `CourseUnit` 类型声明。
- 未新增通用 NPM 运行时依赖：MicroTS strict AOT 不提供完整 JavaScript 环境，本轮需求可由现有 serde、编译器类型和标准库完成。
- 页面模板与模型逻辑继续分离。现有 `App.vue` 和硬件主入口按职责保留；未仅为缩短行数拆出额外层级，也未删除仍用于兼容验证的旧模板。
- 保留双槽保护、BLE 所有权、显示确认、下载模式和关机门禁。课程复习轮是否推进普通轮游标缺少产品依据，本轮未更改该规则。

## 渲染实现中的纠正

ABI 测试表明，完整帧完成后仍需擦除终态身份区域：PoP 圆角与身份矩形相交，直接删除整个 `update_panel` 调用会留下像素。最终实现只去掉重复的 `panel_status` 绘制，共享 `overlay::panel_identity` 擦除逻辑，保留缓存、generation、失败路径及显示 ack 语义。

普通渲染保持 1029 张冻结 PBM 一致；配网另外比较 81 组真实 ABI 帧。冻结目录中原有 138 张缺少 View 输入的历史 PBM 已移至 `.build/release-review/baseline-orphan-pages/` 备份，未计入可重放集合。

少数旧测试借无效 Home / 上翻制造刷新，已改为真实导航或显式启动渲染来验证 busy 门禁；未放宽全局显示确认辅助函数，单次评分、重访和下一题可评分的断言仍保留。

## 验证命令与结果

以下命令均在仓库根目录执行；冻结目录须事先存在。日志、图片、固件和 JSON 都位于已忽略的 `.build/`，不纳入 Git。

```sh
BALABALA_COMPARE_PAGES="$PWD/.build/release-review/baseline-pages" \
  sh tools/build.sh test -- --nocapture
python3 tools/test-source-integrity.py
python3 tools/test-build-env.py
CARGO_NET_OFFLINE=true \
  BALABALA_PERFORMANCE_OUT="$PWD/.build/release-review/final-performance.json" \
  sh tools/build.sh test --release tests::performance_render_import_and_save \
  -- --ignored --exact --nocapture
CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build
```

| 检查 | 结果 |
| --- | --- |
| Rust 完整宿主测试 | 105 passed / 0 failed / 1 ignored；忽略项为显式运行的性能基准 |
| C 输入与存储 / runtime / 网络控制器 | 22 / 50 / 17 项通过 |
| IDF 适配 / patched manager / protocomm / 事件集成 | 8 / 8 / 1 / 14 项通过；上述 C 测试合计 120 项 |
| 广告与 NimBLE mempool 回归 | 通过；mempool 2561 次分配、2561 次释放；C 链 ASan/UBSan 无报错 |
| 同输入冻结 PBM | 1028 个 View 加 1 张保存错误页，共 1029 / 1029 逐字节一致 |
| 配网实际 ABI 帧 | 81 / 81 逐字节一致 |
| 源码完整性 | 18 / 18 通过 |
| 构建环境隔离 | 全部通过，包含实际安装入口与失败传播检查 |
| C3 普通候选 | 构建及配置、镜像头、分区、符号与尺寸门禁通过；autotest OFF |

证据汇总为 `.build/release-review/render-source-acceptance.json`；其余原始证据包括 `source-integrity-tests.log`、`build-env-tests.log`、`panel-pages-comparison.log`、`final-performance.json`、`final-performance-comparison.json` 与 `final-c3-build.log`，均在同一目录。

源码门禁的范围不包含 PocketJS 根目录新增的 `node_modules` 内容及 ESP-IDF 中被 Git 忽略的文件，不能据此宣称整个依赖目录均已固定。详细规则见[补丁说明](../patches/README.md#源码完整性门禁与子模块校验)。

## 性能结果

使用相同宿主、相同内容及 `opt-level=s` release 配置。每项预热后测五批，取各批平均操作耗时的中位数；分配计量单独执行。

| 操作 | 原值 | 新值 | 解释 |
| --- | ---: | ---: | --- |
| 依次查询全部 4000 个课程 ID | 6,460,833 ns | 125,305 ns | 同一次基准中的原线性算法与索引算法，宿主查询快 51.56 倍 |
| 汉字 32 卡导入 | 812,055 ns | 770,152 ns | 耗时减少 5.16%；分配请求 11,523 → 8,754，减少 24.03% |
| 英语 32 卡导入 | 1,019,541 ns | 957,055 ns | 耗时减少 6.13%；分配请求 12,623 → 9,664，减少 23.44% |
| 32KiB CRC32 | 104,374 ns | 103,754 ns | 基本持平 |
| 当前状态导出 | 4,147 ns | 4,135 ns | 基本持平 |

导入宿主堆峰值仍为 12,816 / 12,851 B。六类页面的堆峰值与分配请求数不变；页面耗时变化为 -25.88% 至 +2.5%，有涨有跌，样本不足以证明各页面稳定提速或确定变化原因。

分配请求数不等于同时存活对象数；宿主堆峰值不等于 C3 heap。这些数据不代表设备翻页加速比。无效操作去重由 revision / dirty 回归验证，本轮未测量设备刷新次数。详情见[性能文档](../docs/performance.md)。

## 固件指标

| 指标 | 本轮 z 候选 |
| --- | ---: |
| BIN / 实际镜像大小 | 4,599,600 / 4,599,209 字节 |
| Bootloader | 21,280 字节 |
| 静态 DRAM / 链接剩余 DRAM | 172,671 / 148,625 字节 |
| factory 剩余空间 | 12,112,080 字节 |

Flash 为 DIO / 40MHz / 16MB；factory 起点 `0x10000`、容量 `0xff0000`、终点 `0x1000000`。默认清单仍为 `opt-level=s`，`z` 仅作本次候选的进程级覆盖。链接剩余 DRAM 不能视为运行时可用 heap。

## 文档、许可与历史核查

[README](../README.md) 提供能力、前置依赖、构建与 SD 安装入口；[架构](../docs/architecture.md)、[平台](../docs/platform.md)、[性能](../docs/performance.md)、[验收](../docs/acceptance.md)及[补丁说明](../patches/README.md)分别记录实现边界。历史 4MB / 3MiB、16 位 PoP 与旧镜像的真机数据保留原候选口径。

原创代码采用根目录 [Apache-2.0](../LICENSE)。第三方字体、课程数据、链接组件及工具链运行时继续使用各自许可，固定本地来源与文本见[第三方归属](../licenses/README.md)。10 份完整许可或 NOTICE 与固定本地原文逐字节一致；文件头提取的声明另行核对，Rust 内置 libm 的独立来源声明一并保留。

文档收尾校验覆盖 26 个 Markdown 文件、400 个本地链接与锚点，0 错误；`git diff --check` 通过。源码验证完成后的修改仅涉及文档与许可。

限定范围的 Git 历史核查覆盖 4 次提交、223 个对象路径，生成目录、常见编译产物及 `.env` / 私钥等敏感文件名均为 0 命中；270 个独立 blob 对私钥块和 GitHub / AWS / OpenAI 令牌模式的启发式扫描为 0 命中。这不能替代完整凭据审计。

## 待真机验收

软件审查完成后，当前候选已在用户授权下完成 app-only 烧录和 25 秒启动观察，确认基础 SD 挂载、原存档恢复与首屏显示，见[部署记录](release-deployment.md)。本次未安装配套的 4000 张课程正文。

尚待验证完整课程真实取卡与连续学习、缺失或损坏正文及掉电恢复、动态 heap 与最大连续块、全部任务栈高水位、实体按键、局刷残影与耗时、休眠唤醒及电流，以及当前 UI 下官方 iPhone 的 6 位数字 PoP 配网、广播关闭和超时流程。NVS 未加密；BLE stop 永不返回仍是已记录限制。
