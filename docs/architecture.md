# 代码结构与修改入口

应用仍使用 MicroTS strict AOT、Rust 模型和 C 平台运行时。页面、模型和硬件通过固定数据与 C ABI 连接；构建及测试入口见[README](../README.md)。

## 应用模块

| 需要修改的行为 | 入口 | 职责 |
| --- | --- | --- |
| 学习、评分、导航、显示门禁 | [model.rs](../native/src/model.rs) | 保存状态机与按键入口；维护按键松开状态清除与边界检查 |
| 学习流程、选卡、出题与统计 | [model/learning.rs](../native/src/model/learning.rs) | 内置轮次调度、选项重排、评分入口与复习计数（私有子模块） |
| 卡组类型、导入校验、常驻计费 | [model/content.rs](../native/src/model/content.rs) | 完整验证成功后替换活动卡组 |
| 有界 JSON 解析 | [model/decode.rs](../native/src/model/decode.rs) | 在构造字符串、集合前限制输入和容量；卡组及存档共用 |
| SD课程、累计掌握与单元解锁 | [model/course.rs](../native/src/model/course.rs) | 按轮读取正文、调度课程卡片、累计次数与解锁；FNV32 二分查询索引 |
| 双槽记录、恢复、导出及确认 | [model/save.rs](../native/src/model/save.rs) | 集中维护保护锁、generation、pending save 和 dirty |
| 配网身份、退出意图、资源释放确认 | [model/network.rs](../native/src/model/network.rs) | 等待明确释放后再离开面板，退出时清除身份数据 |
| 状态到页面数据的转换 | [model/view.rs](../native/src/model/view.rs) | 生成学习页、菜单、提示及整页字体预算 |
| View 数据、props 与敏感字段清除 | [view.rs](../native/src/view.rs) | 页面数据接口和生命周期 |
| AOT 挂载、条带栅格化、固定面板更新 | [render.rs](../native/src/render.rs) | 输出一份帧缓冲；配网活跃时沿用面板去重更新路径 |
| 静态线条、素材叠加与面板绘制 | [overlay.rs](../native/src/overlay.rs) | 按字节叠加线条与素材；共享矩形填充和配网身份区域擦除 |
| C 入口及指针、长度检查 | [abi.rs](../native/src/abi.rs) | 参数检查、渲染缓存、revision 确认及帧完成后的身份擦除 |
| ESP32 分配与终止处理 | [allocator.rs](../native/src/allocator.rs) | 仅在嵌入式目标编译 |

[lib.rs](../native/src/lib.rs)负责模块装配、生成数据和应用根对象。外部仍通过原有 C ABI 或 `model` 的公开入口访问；有界解析、记录解码和页面组装辅助函数不需要成为新的公开 API。`course::index_for_id` 复用已有的 FNV32 有序索引，二分定位后再次核对科目与完整 ID，避免未知 ID 的哈希碰撞误匹配。配网完整帧已包含状态绘制，ABI 只补充 `overlay::panel_identity` 所需的终态身份擦除；缓存帧继续更新状态和身份区域。

## 页面与构建

[App.vue](../native/App.vue)声明文字与容器，[ui-layout.ts](../tools/ui-layout.ts)提供共用几何，[overlay.rs](../native/src/overlay.rs)负责静态线条和 Flash 素材叠加。调整学习行为应修改模型；调整字号适配应修改[fit.rs](../native/src/fit.rs)，导入与 View 必须使用同一排版规则。

[build-native.ts](../tools/build-native.ts)串行编排以下步骤：

1. 校验配网布局并生成共用坐标，编译 strict AOT。
2. [native-projection.ts](../tools/native-projection.ts)使用编译器 `AotProgram`、`AotNode` 和 `PROP` 定义，生成节点容量及当前分支的字体集合。
3. [native-content.ts](../tools/native-content.ts)读取内置示范；[curriculum.ts](../tools/curriculum.ts)校验固定来源并按人工主题生成课程，[course-pack.ts](../tools/course-pack.ts)输出SD正文与固件目录。
4. [native-fonts.ts](../tools/native-fonts.ts)收集示范、完整课程和运行时文案，烘焙字号及素材，输出资源记录。

固件构建仍由[build.sh](../tools/build.sh)加载固定工具链；[configure-firmware.py](../tools/configure-firmware.py)统一处理普通/诊断配置和旧缓存覆盖。不在 `native/` 直接运行 Cargo，也不把 AOT 编译视为完整 TypeScript 类型检查。

## 测试定位

[test-native-render.rs](../tools/test-native-render.rs)是宿主测试装配入口，保留公共操作和 PBM 输出。行为文件按用途划分：

- 导入与容量：[test-content-import.rs](../tools/test-content-import.rs)；评分和导航：[test-learning-model.rs](../tools/test-learning-model.rs)。
- 课程累计、解锁、SD正文及存档迁移：[test-curriculum.rs](../tools/test-curriculum.rs)。
- 双槽恢复：[test-save.rs](../tools/test-save.rs)；配网应用桥：[test-network-app.rs](../tools/test-network-app.rs)；刷机事务：[test-download.rs](../tools/test-download.rs)。
- 渲染与冻结比较：[test-render-core.rs](../tools/test-render-core.rs)、[test-render-layout.rs](../tools/test-render-layout.rs)；位图与圆角裁剪：[test-overlay.rs](../tools/test-overlay.rs)；内存生命周期：[test-native-memory.rs](../tools/test-native-memory.rs)。
- CPU耗时与分配基准：[test-performance.rs](../tools/test-performance.rs)，默认忽略，显式运行方法见[性能测量](performance.md)。
- 共用夹具：[test-fixtures.rs](../tools/test-fixtures.rs)；分配计量：[test-heap-meter.rs](../tools/test-heap-meter.rs)。计量区间内不新增夹具准备工作。

现有 `tests::...` 测试名与构建筛选方式保留。C 输入、存储、runtime、BLE 适配和事件集成仍由[test-platform.sh](../tools/test-platform.sh)覆盖；Rust 替身和 C ASan/UBSan 不能替代真机无线、SD 掉电或运行内存验收。

## 修改时需要保留的边界

- 存档字段与记录格式不因文件拆分而变化；保护锁仅由成功恢复有效且可解释的档案解除。
- 正面评分要求当前 revision 已完成显示且旧键已释放；配网活跃时不能恢复完整 AOT 渲染。
- 刷机和休眠继续等待保存、网络清理及精确显示确认，平台步骤的先后顺序不能仅按表面重复删除。
- 内部网络事件使用 `BALABALA_NET_EVENT_*` 位标志，C ABI 状态仍为 `BALABALA_NETWORK_*` 枚举，两者不能互换。
- 保留现有 `serde`、`serde_json::RawValue`、编译器类型和 Flash 借用接口；新增依赖前先确认具体收益与容量成本。

硬件主入口的静态对象、共享 SPI 与回调保持在[main.c](../firmware/main/main.c)；运行时、存储、输入、显示和网络本身已按职责分文件，无需为行数再增加跨文件全局状态。
