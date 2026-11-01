# 依赖升级记录

本轮基于 `be2a970`，更新工具链、上游框架、实际构建依赖及课程读音源。架构仍为原版 X4 / ESP32-C3、MicroTS strict AOT、Rust 模型与 C 平台 ABI；Flash 保持 DIO / 40MHz / 16MB。

## 版本与保留项

| 依赖 | 升级前 | 当前 |
| --- | --- | --- |
| ESP-IDF | 6.0.3 | 6.1 |
| PocketJS | `a9f186e2` | `8bc05257` |
| Bun | 1.3.14 | 1.4.4 |
| TypeScript | 5.9.3 | 6.0.3 |
| Taffy | 0.11.0 | 0.15.1 |
| heapless | 0.8.0 | 0.9.3 |
| serde / serde_core / serde_derive | 1.0.228 | 1.0.229 |
| serde_json | 1.0.150 | 1.0.154 |
| syn | 2.0.118 | 3.0.9 |
| json_generator | 2.0.0 | 2.1.0 |
| Unihan | 16.0.0 | 18.0.0 |

PocketJS 的构建依赖同时更新为 Babel core 8.0.9 / preset-typescript 8.0.7、opentype.js 2.0.0、Vue 及三个编译器 3.6.0-rc.13，相关 Volar、Vue JSX Vapor、Solid 和 Octane 依赖一起更新并锁定。Vue 延续原有 3.6 RC 系列，保持运行时与编译器版本一致。其余实际 Rust 包版本及归属见[许可表](../licenses/README.md)。

Rust 1.99.1、resvg-js 2.6.2、network_provisioning 1.4.0、NGSL 1.2 和当前字体经上游核对后保留；实际固定值见 [versions.sh](../tools/versions.sh)、[组件锁](../tools/components.lock.json)和[来源锁](../content/curriculum/sources.lock.json)。另有两项兼容限制：

- TypeScript 7.0.2 的真实 strict AOT 尝试在 `aot-frontend.ts:48` 失败：`TypeError: undefined is not an object (evaluating 'ts.ScriptTarget.Latest')`。当前 MicroTS 使用的 JavaScript Compiler API 在该版本不可用，因此采用已通过真实 AOT 的 6.0.3。
- hash32 保留 0.3.1；最新 heapless 0.9.3 仍声明 `hash32 = "0.3.0"`，无法直接采用 hash32 1.0.0。

## 可重放补丁与安装来源

ESP-IDF 固定提交为 `fff9895c82d744c7237be8847347bdd1b07c6643`，28 个递归子模块按 Git 锁核验。四项补丁见 [IDF 锁](../patches/esp-idf-6.1.json)：观察通知、事件实例注销、mempool 重初始化和广告状态观察。实例注销适配 6.1 的递归事件锁：同派发回调内先逻辑失效，派发与 profiling 完成后按身份回收；跨任务等待事件锁后完成删除，保留 legacy 路径及资源所有权契约。

PocketJS 固定提交为 `8bc0525776efe779ef9592e0b91574cfbc91f60b`，归档 SHA256 为 `8db2b451cca869f8be810db8b0184b68000c68c32169bd3939ede4b774d4ddf1`。原四项字体、布局容量补丁重放后，新增[依赖补丁](../patches/pocketjs-dependencies.patch)；Taffy 0.15 的测量回调适配保留已知尺寸、padding、min/max 和 BorderBox 计算。SDK 与 PocketJS 移植均经过独立只读复审，指定范围未发现需修复的 P1/P2；该结论不替代后续行为与硬件验收。

完整上游 `Cargo.lock` 作为维护补丁的一部分重放；构建复制并裁剪得到 32 个包（21 个 registry、11 个 path），SHA256 为 `2b7d9233f21fd64cd71d8161a52051de1ac58135d38b574ca5e5a75149798c4b`。生成锁留在忽略目录。已移除的 arrayvec 不再保留当前发行许可，version_check 仍作为构建依赖保留。

NPM 锁中的显式下载地址统一为 `registry.npmjs.org`，版本与 integrity 保持锁定。空 `node_modules` 与独立空缓存下执行以下命令成功，安装 142 个生产依赖包：

```sh
BUN_INSTALL_CACHE_DIR="$PWD/.build/dependency-upgrade/official-bun-cache" \
  .tools/bun/bun install --cwd .tools/pocketjs \
  --registry https://registry.npmjs.org --frozen-lockfile --production --ignore-scripts
```

升级前已按旧锁核验并保留旧 SDK、PocketJS、Bun 和数据缓存。首次 C3 构建遇到 IDF 6.0 / 6.1 Python 配置缓存冲突，保留旧 `.build/firmware` 后从新目录构建通过。初次 ABI 比较误用了 `.build/pages`，改按测试实际输出目录比对通过；两次初始日志均保留，没有改动测试、基准或资源限额。

## 课程与许可

课程保持 **4000 张、135 单元**，稳定 ID 无增删。Unihan 18.0.0 与人工词境修订使 1399 张汉字卡的选项变化；反面及解释只有 `hz-u5566`（哗啦中的“啦”）从 `la` 改为 `lā`。“看啊”的 `a`、“钥匙”的 `shi` 和“哗啦”的 `lā` 均使用已有人工覆盖，并由新增行为测试核对实际课程包。

| 配套文件 | 字节数 | 摘要 |
| --- | ---: | --- |
| `course.bin` | 874632 | SHA256 `5d04c2f6b109081cda19ece421b0a0b40297f0e42cdee1b979f7b83bb871819b` |
| `manifest.json` | 388 | SHA256 `cdd307c8967d70f47ad3c323f5b7b20fc4738f7c8eb3be7c9e9cedf1088ac796`，CRC32 `154bbdf2` |

`assets.json` 与 `projection.json` 摘要保持基线值。新固件必须配套同次构建的 SD 课程：关机后把 `.build/sdcard/balabala/curriculum/` 完整复制到 SD 的 `/balabala/curriculum/`，保留归属文件，先备份 `/balabala/saves/`。

更新了实际 21 个 registry 包的许可与版本表，保留 24 份 Rust 许可文件（另三份为工具链运行库归属）；Taffy 原文来自包内 VCS 提交对应的官方 LICENSE。FatFS 更新为 R0.16 / ChaN 2025，Unihan 读音源更新为 18.0.0 / Unicode 2026；其余 SDK 许可原文已对照固定源码核验。逐项原文检查通过，归属见[第三方许可](../licenses/README.md)。

## 验证命令与结果

以下命令在仓库根目录执行，最终退出码均为 0：

```sh
BALABALA_COMPARE_PAGES="$PWD/.build/dependency-upgrade/baseline-pages" sh tools/build.sh test
CARGO_PROFILE_RELEASE_OPT_LEVEL=z sh tools/build.sh build
python3 tools/test-build-env.py
python3 tools/test-source-integrity.py
```

- Rust：106 passed、0 failed、1 ignored；忽略项为显式宿主性能基准。
- C：22 platform + 50 runtime / 真实 Rust ABI + 17 network + 8 IDF adapter + 8 manager + 1 protocomm + 14 event，合计 120 项通过，ASan/UBSan 无报错；另有广告及 NimBLE mempool 场景通过。
- 事件集成：2665 次分配、10 次分配故障注入、8 次队列丢弃、25 次重开；NimBLE mempool 为 2561 次分配 / 释放配对。
- 冻结页面：1029 / 1029 PBM 一致；实际 ABI 面板目录 `.build/provisioning-ui-memory-current/abi-pages` 与基线文件名集合相同，81 / 81 逐字节一致。
- 构建环境：3 组隔离、失败传播与缺少 shasum 的前置拒绝检查通过；源码完整性：19 项通过。

普通 C3 候选关闭 autotest，主任务栈 65536 字节，保持 PM 和 BLE 内存配置。`z` 仅作进程级覆盖，清单默认仍为 `s`。

| 固件指标 | 实测值 |
| --- | ---: |
| 应用 BIN | 4620160 B |
| 未填充镜像 | 4619761 B |
| Bootloader | 21152 B |
| 分区表 | 3072 B |
| factory 剩余 | 12091520 B |
| 链接 DRAM 已用 | 173649 B |
| 其中静态 `.data + .bss` | 92005 B |
| 链接 DRAM 剩余 | 147647 B |

BIN SHA256 为 `0e0a32c67116bd67dc1344ebd94ffc4e69ca5fe00fc4082269cdeb83013bd9b2`；较此前 4599600 B 候选增加 20560 B。NVS / PHY / factory 起始及分区表保持不变，factory 为 `0x10000` / `0xff0000`，覆盖到 16MiB 末尾。链接余量不是运行时可用 heap，本轮未重新测量运行性能。

## 交付状态与证据

实现、锁、补丁及文档已提交为 `974dde7`（`chore: 升级依赖并适配 ESP-IDF 6.1`）。用户随后授权烧录，原版 X4 已运行该提交对应的应用；写入与独立校验通过，25 秒启动观察确认 SD 挂载、旧档恢复与首屏全刷成功。保留 ESP-IDF 6.0.3 引导程序用于回退，完整备份、启动内存采样与新增 GPIO 告警见[依赖升级部署记录](dependency-deployment.md)。

本轮未安装新 SD 课程，仍待验证完整课程连续加载、实体按键、局刷、休眠电流、学习与无线全流程 heap / 连续块 / 各任务栈水位，以及官方 iPhone 配网、广播关闭与超时。此前旧应用的启动记录见[发布审查部署备忘](release-deployment.md)。

本地证据位于忽略目录 `.build/dependency-upgrade/`：`integration-validation.md`、`host-tests.log`、`panel-pages-comparison.json`、`c3-artifacts-final.json`、`build-env.log`、`source-integrity-final.log`、`licenses-verification.json`。完整验收见[依赖升级验收](../docs/acceptance.md#最新依赖升级验收)。
