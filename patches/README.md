# PocketJS 本地补丁

固定上游为 `8bc0525776efe779ef9592e0b91574cfbc91f60b`；[manifest.json](manifest.json) 按顺序固定补丁 SHA-256 及每个目标文件应用前后的 SHA-256，保留 [PocketJS MIT 许可](../licenses/LICENSE-pocketjs.txt)。

1. [pocketjs-direct-font-subset.patch](pocketjs-direct-font-subset.patch) 修改 `engine/core/src/text.rs` 和 `engine/core/src/lib.rs`，提供从 Flash 字档直接构造最终 Atlas 的入口。
2. [pocketjs-layout-capacity.patch](pocketjs-layout-capacity.patch) 基于第一项修改 `engine/core/src/lib.rs` 和 `engine/core/src/layout.rs`，提供初始布局容量提示与只读真实投影节点计数。
3. [pocketjs-static-font-subset.patch](pocketjs-static-font-subset.patch) 基于前两项修改 `text.rs`、`lib.rs`、`resources.rs`、`font_pages.rs` 和 `font_stream.rs`，提供只接受 `&'static [u8]` 的字体子集借用入口及统一只读 bitmap 访问器。

4. [pocketjs-mono-font-atlas.patch](pocketjs-mono-font-atlas.patch) 接在前三项之后，修改 spec / TypeScript 格式常量、core 字体解析与资源视图、drivers 视图、软件 raster 和字体烘焙，增加 DCFA v3 的 `FLAG_MONO1 = 2`；另同步 raster 集成测试中三个旧 8bit `FontView` 夹具的 `flags: 0`。补丁 SHA256 为 `2d915983d12f560ccbadafb44f3ddf06ba39d793598630e5d6e3d71490fc9984`，完整 10 个文件清单及逐文件摘要链见 manifest；同一文件的输入摘要沿用前一补丁的输出。
5. [pocketjs-dependencies.patch](pocketjs-dependencies.patch) 接在前四项之后，更新 PocketJS 上游依赖锁并适配 Taffy 0.15 布局 API，同时在 `package.json` 与 `bun.lock` 中锁定 TypeScript 6.0.3，避免 TS 7.0.2 移除旧编译器 API 导致 AOT 编译失败。补丁 SHA256 为 `9597126270d0d4f16a9683655adf6d75b65e67c334e34673411d8b7e15615372`，目标文件与逐文件摘要链见 manifest。

安装和构建入口调用 [apply-patches.py](../tools/apply-patches.py)。脚本只接受本项目 `.tools/pocketjs` 的固定派生源码，检查目录解析结果、版本标记、目标路径和所有摘要。完整原始状态或完整已应用前缀可以继续；完整最终状态直接通过；混合、漂移或摘要错误均退出失败。因此已完成任意完整前缀的派生源码可幂等升级到五项补丁，无需修改外部仓库。首次应用需要系统 `patch` 命令。下载目录与派生源码均由 `.gitignore` 排除，补丁自身是维护源码。

`Atlas::subset(bytes, chars)` 从 Flash 字档和排序去重字符集合直接构造最终 cmap / bitmap，省去应用侧的序列化子集及再次复制。输入检查覆盖 magic、版本、尺寸/密度、严格有序 cmap、glyph 索引、位图长度及所选字符的排序/唯一性/存在性；使用安全 Rust 切片和 checked 算术。`Ui::load_font_subset()` 与原 `load_font_atlas()` 共用字体注册后的 revision、布局及 raster 失效处理，完整 atlas 入口保留。

`Ui::load_font_subset_static(bytes: &'static [u8], chars: &[char])` 沿用相同校验和 `font_loaded` revision / dirty 契约，仅分配当前页 cmap；完整源 bitmap 从 Flash 借用，不复制到 heap，选中 cmap 保留源 gid，`glyph_count` 覆盖源可索引范围。原 `load_font_subset()` / `load_font_atlas()` 继续拥有复制后的 bitmap，不依赖调用者源缓冲的生命周期。owned 子集的缺字 fallback 仍是首个选中字形的 gid 0；借用子集记录它对应的源 gid，保留相同像素及度量，不改变这个既有行为。入口使用安全静态切片，没有延长临时输入生命周期。

`Atlas::bitmap()` 统一供 `glyph_rows`、RenderResources / FontView 和 FontPages 读取。8bit 字档的 streaming configure 先从该访问器复制基字形，再切换为可写 owned bitmap；commit / compact 不写借用源。未启用 streaming 的 detach 保留 Flash 借用。

mono 字档保持 DCFA VERSION 3、16 字节头及 8 字节 cmap；头部 byte 13 的 bit 1 标记 mono。每字形逐行保存 `ceil(coverage_w / 8)` 字节，高位在左、行尾未用位为 0；原 8bit 覆盖度 `>=128` 为 1，读取为 255，否则为 0。mono 只支持 density=1，烘焙和解析均拒绝其他密度；mono 不支持 streaming，非 detach 配置显式失败，不改变 revision 或 Flash 借用。未标记 mono 的旧 8bit v2/v3 字档保持原路径。`bakeSlot` 末尾新增可选 `mono=false` 参数，默认输出不变；本项目只为 5 / 14 号槽显式开启。

在 mono 字库阶段，将固定归档的全部补丁目标恢复为原始源码，再执行 `python3 tools/apply-patches.py`，当时四项补丁顺序重放及 before/patch/after 摘要全部通过。新增测试覆盖非整字节宽度、所有截断点、owned/static 子集、密度与 streaming 拒绝，以及 0/127/128/255 覆盖度经真实 RGB565 / 黑白转换的正反色等价。46 张冻结输入 PBM 逐字节一致；当前字体 Flash 从 1470432 B 减到 190062 B。精确命令、镜像尺寸和本轮未验证项见[mono 验收记录](../docs/acceptance.md#mono-字库第-1-阶段验证2026-10-28)。

`Ui::new_with_layout_capacity(nodes)` 提示主布局树的初始容量，`layout_node_count()` 返回真实主投影节点数。旧构造入口仍默认 16，hint 不足时正常扩容；SlotMap 自加 sentinel，调用方只计真实节点。应用仅对正确反馈使用从当前 AOT AST 得出的精确节点数，其他页面保持默认。[build-native.ts](../tools/build-native.ts) 生成可核验的 `projection.rs` / `projection.json`，对不支持的投影结构直接构建失败。

真实渲染和格式/边界回归位于 [test-native-render.rs](../tools/test-native-render.rs)。第一轮固定 33 个输入 PBM、第二轮固定 34 个输入 PBM 均与各自补丁前逐字节一致；第二轮还验证 414 个实际视图的节点计数、空行/满文本与 hint 不足的真实生成 App。测量边界、收益与未闭合项见[第一轮](../docs/memory-round1.md)和[第二轮内存记录](../docs/memory-round2.md)。

Flash 字体回归见 [test-font-flash.rs](../tools/test-font-flash.rs) 与隔离测试 allocator [test-font-arena.rs](../tools/test-font-arena.rs)：45 份刷机模式冻结 View 的 owned / borrowed / 历史 PBM 像素一致，另覆盖边界、替换、streaming 和有界完整生命周期；43424 B 总量 / 15872 B 最大区域的子进程实验通过显式解压的历史 8bit fixture 继续准确触发旧 16800 B 请求失败，mono owned 路径最大请求 2100 B、borrowed 路径最大请求 40 B 均成功。该合成实验不等同 C3 allocator 重放。测量口径、原 282 字形独立基准及未验证项见[字体 OOM 记录](../docs/font-flash-oom.md)。

BLE 组件与 ESP-IDF 采用独立的固定补丁锁，原始包/提交均不改写：[组件锁](../tools/components.lock.json) 与 [IDF 锁](esp-idf-6.1.json)。[manager 生命周期补丁](network-provisioning-1.4.0-stop-lifecycle.patch)维护 STOPPING 锁外排空、三个 instance 订阅及回滚、同步 END、失败启动自有 PoP 释放；当前还固定公开 cleanup_error 查询、end_pending、注册失败先排空再释放以及停止错误保留所有权/显式 deinit 单次重试，观察副本投递失败不改变释放成功返回。公开 manager.h 与实现一同校验，已通过的 instance 算法保持。[IDF 观察补丁](esp-idf-6.1-observer-notifications.patch)固定七处 wait=0；[IDF 事件实例补丁](esp-idf-6.1-instance-removal.patch)在回调内注销时先逻辑失效，在派发与 profiling 结束后按实例身份回收；跨任务等待事件互斥锁后完成删除，保留原 legacy 退休路径，不新增回收队列。完整输入/输出与补丁 SHA256 由 [fetch-components.py](../tools/fetch-components.py) 和 [apply-idf-patches.py](../tools/apply-idf-patches.py)校验；仅操作本项目被忽略的派生树。行为与未验证边界见 [BLE 配网](../docs/ble-provisioning.md)。

[IDF mempool 重初始化补丁](esp-idf-6.1-mempool-reinit.patch)只修改固定 `components/bt/porting/mem/os_mempool.c` 的两处注册表插入：已注册最后一个池重新初始化时，内部 unregister 会清除初始化标志，插入前再调用既有 `os_mempool_list_ensure_init()` 保持标志与链表一致。保留身份比较、块所有权和分配算法，不增加失败清理入口。补丁前真实源码回归可重现注册项丢失；[最低层宿主测试](../tools/test-nimble-mempool.c)直接编译补丁后的完整固定源码，在 runtime/nonreuse 下检查 OOM 恢复、立即释放、clear/reinit、最后/多池/同名替换及连续周期。该补丁和目标源码前后摘要纳入 IDF 锁的 `mempoolReinit`，同一重放入口按顺序校验；该离线注册表缺陷与设备曾失败的 ACL 7200 B 整池申请是两个发现。另有 [IDF 广告观察补丁](esp-idf-6.1-advertising-observer.patch) 用于捕获精确广告状态。

## 源码完整性门禁与子模块校验

[source_integrity.py](../tools/source_integrity.py) 校验固定上游源码与合法补丁状态，避免本地派生副本的意外修改进入构建。

### 1. PocketJS 归档与派生完整性

- **归档摘要与版本**：核验下载归档 `pocketjs.tar.gz` 的 SHA-256 与 `versions.sh` 严格匹配；解压后的版本文件 `.balabala-version` 必须与固定 Commit 一致。
- **逐文件校验**：逐一提取归档内所有文件的原始摘要，对照派生树中的文件内容；未受补丁影响的文件必须与归档完全一致，已受补丁影响的文件必须匹配 [manifest.json](manifest.json) 中某个完整补丁前缀的摘要，随后按固定顺序补齐补丁。
- **无未受管文件**：除根目录 `.balabala-version` 与 `node_modules` 外，派生目录内禁止存在归档未登记的额外文件；归档已有成员始终逐项核验。

### 2. ESP-IDF 根目录与递归子模块门禁

- **Commit 校验**：核验 `.tools/esp-idf` 当前 Git HEAD 必须等于 `versions.sh` 声明的 `fff9895c82d744c7237be8847347bdd1b07c6643`。
- **工作区变更与补丁锁**：执行 `git diff --cached` 与 `git diff`（带 `--ignore-submodules=none` 参数），要求除 [补丁锁](esp-idf-6.1.json) 声明的目标文件外，工作区必须严格干净；目标文件修改前后的 SHA-256 必须与锁文件严格吻合。
- **无额外源码**：通过 `git ls-files --others --exclude-standard` 拒绝 `.tools/esp-idf` 中不受 Git 忽略的未跟踪文件。
- **递归子模块锁**：递归遍历所有子模块（`git submodule status --recursive`），要求状态标识符必须为空格 `' '`（表示每一个子模块均精确停留在其锁定的固定 Commit，无漂移、无前向新增）；遍历每个子模块工作区，要求没有任何已修改或未跟踪文件。
- **文件类型**：补丁目标必须是普通文件，不能以软链接替代。

以上校验不覆盖 PocketJS 根目录新增的 `node_modules` 内容，以及 ESP-IDF 中被 Git 忽略的文件。依赖安装仍使用项目锁文件；此门禁不承诺固定整个依赖目录的所有文件。

### 3. 执行时机与失败防御

- **执行前置门禁**：在 [tools/env.sh](../tools/env.sh) 中，`python3 "$root/tools/apply-idf-patches.py" || return 1` **严格在执行任何 ESP-IDF Python 脚本（如 `idf_tools.py export`）之前运行**。如果补丁或源码完整性未通过，立即中断并退出，防止未受管的代码被 Python 解释器加载执行。
- **安装前置校验**：在 [tools/setup.sh](../tools/setup.sh) 中先核验 PocketJS 归档摘要再解压。已有 ESP-IDF 缓存会在 `submodule update` 前先检查，避免更新操作掩盖漂移；首次克隆完成初始化后检查。两种情况均在执行 IDF 安装脚本前应用并核验补丁。

### 4. 完整性门禁自动化回归测试

[tools/test-source-integrity.py](../tools/test-source-integrity.py) 提供 19 项自动化测试，在隔离目录与 Git 夹具中验证正常重放、幂等执行及故障拒绝：

- PocketJS 归档哈希篡改、派生源码被篡改、未打补丁即调用、包含未受管额外文件等；
- ESP-IDF HEAD Commit 偏离、存在未跟踪文件、补丁目标文件哈希不匹配；
- 递归子模块 Commit 偏离、子模块工作区存在未提交修改、目标文件被软链接替换；
- 补丁目标包含合法补丁时的幂等放行；
- 运行命令为：`python3 tools/test-source-integrity.py`，19 项测试均必须退出 0。
