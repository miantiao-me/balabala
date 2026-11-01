# BLE 配网

家长设置中的“蓝牙配网”通过 ESP32-C3 C 平台、乐鑫 `network_provisioning`、NimBLE 和官方 Security 1 提交 Wi-Fi 凭据。设备名为 `PROV_` 加六位大写十六进制后缀；PoP（配网码）为每会话随机生成的 6 位 ASCII 数字，保留前导 0。Rust 页面和 SD 存储不接触 Wi-Fi 密码。

当前部署与未验项目见[最新依赖升级验收](acceptance.md#最新依赖升级验收)。旧 16 位 PoP 候选的官方 iPhone 成功及 phase2 自动生命周期证据见[分阶段配网验证](acceptance.md#当前分阶段配网验证)；这些结果不能替代当前 6 位 PoP 的手机交互、失败 / 退出 / 超时、空中广播关闭及完整内存验收。

## 官方 iOS 操作路径

手动验收须使用关闭 autotest 的普通候选；phase2 会自动退出配网。旧候选的部署和手机结果保留在上述验收记录，不能扩展为当前全部场景通过。

1. 在首页按住正面第三键“返回”2 秒进入家长设置，再选第四项“蓝牙配网”并确认。若从其他页面按住返回到达首页，须松开后重新长按。实体键序仍待核验，请按屏幕变化核对；等待设备名、六格配网码出现且“连手机”反白，才可连接。“准备中”不表示入口就绪。
2. 在 iPhone 打开乐鑫官方 **ESP BLE Provisioning** 客户端并允许蓝牙访问，选择 BLE 配网的手动设备搜索入口。
3. 搜索 `PROV_` 前缀，在设备列表中选择与屏幕完整名称一致的一项。
4. 使用 Security 1（官方 SDK 的 `.secure`）连接，在 PoP 提示页输入设备当前显示的 6 位数字（保留前导 0）；每次重新进入配网都会生成新的 PoP。不要选择无 PoP / Security 0。
5. 选择目标 Wi-Fi，输入其密码并提交。设备取得 IP 后显示成功，继续等待客户端读取成功状态；官方查询完成或成功后约 30 秒 autostop 会请求关闭 BLE，随后还有异步清理。
6. 连接失败时退出配网页，正常清理结束后重新进入可重试。连接成功前超过 300 秒会请求停止；返回、回桌面或关机也会请求关闭入口。返回、回桌面和关机须等待明确资源释放。若底层清理失败且资源可能驻留，保留固定“清理失败”面板与按键响应，撤销关机、锁止重开，暂停完整页面渲染和自动保存，不保证立即继续学习。资源已释放但旧配置恢复失败可显示可退出错误页，仍锁止重开。底层资源保留供主线程人工诊断，产品按键不会自动重试清理。

PoP 和 Wi-Fi 密码只在官方客户端本地输入，不发送给协作者，不写入验收日志；反馈仅需设备/手机状态、是否成功返回及脱敏错误现象。重新搜索未见设备只是客户端观察，不能单独证明空中广播完全关闭。

| 验收层次 | 用户操作与应观察结果 |
| --- | --- |
| 首次成功 | 按上述步骤提交正确凭据，核对手机和设备成功、身份擦除；客户端查询完成或约30秒关闭后，退出配网并确认可回到菜单/桌面 |
| 错误凭据 | 另一次会话输入错误密码，核对失败提示和退出；不记录密码，恢复旧配置及释放结果需结合后续证据判断 |
| 主动取消 | 等待手机阶段按确认取消，等待清理后回家长设置；返回或短按电源的退出目标为桌面，物理映射单独核验 |
| 超时 | 不提交凭据，从准备阶段起等待原300秒截止及清理，检查超时提示；不以 phase2 短场景代替 |
| 重开 | 正常清理后重新进入，核对新身份、可搜索连接与再次成功；若出现清理失败则记录状态，不反复按键要求自动清理 |

上述手动列表与 PoP 路径依据官方 iOS 源码提交 [`78e58f010f8539f800bce063a26d3e4c4ac596b9`](https://github.com/espressif/esp-idf-provisioning-ios/tree/78e58f010f8539f800bce063a26d3e4c4ac596b9)：`BLELandingViewController.swift` 搜索/选中设备并转到 PoP 页，`ConnectViewController.swift` 提交 PoP，`ESPDevice.swift` 的 `.secure` 分支创建 `ESPSecurity1`。App Store 当前版本的入口文案可能不同；源码存在不能代表官方 iPhone 端到端通过。没有开发自有手机端，也没有以浏览器或 AP 配网代替 BLE。

## 固定来源和可复现依赖

[components.lock.json](../tools/components.lock.json) 固定完整 ZIP URL、版本、来源提交和 SHA256；[fetch-components.py](../tools/fetch-components.py) 在 setup/build 入口下载、校验归档并逐文件核对派生树，拒绝摘要不符或不安全路径。禁用 IDF Component Manager 的浮动解析，在 CMake 中显式连接这两个本地组件；下载与解压仅进入已忽略的 `.tools/`，不提交上游源码。

| 组件 | 固定版本 / 来源提交 | 完整 ZIP SHA256 |
| --- | --- | --- |
| network_provisioning | 1.4.0 / `c492b27913079a61f05fa738903f33709f925e6c` | `6e81719e22a1485c3d617e8512a1eff356c333ab3ea0eae3222b2a88d9fd12ad` |
| json_generator | 2.0.0 / `225a1c974215bdf6eeed990c5f9ffe45f883b171` | `4117d5b567242b7deb95a6fab051ff5375f0953d0af19bb990d4a2d67f29ee2b` |

`network_provisioning` 原始 manifest 依赖 IDF >=5.1 与 `json_generator ^2.0.0`；项目当前固定 IDF 6.1 和 json_generator 2.0.0。原包摘要不变，本地生命周期补丁可重放。版本锁见 [versions.sh](../tools/versions.sh)，归属与 Apache-2.0 原文见[第三方说明](../licenses/README.md)。

[manager 补丁](../patches/network-provisioning-1.4.0-stop-lifecycle.patch) 的摘要、目标 `src/manager.c` 与公开 `include/network_provisioning/manager.h` 应用前后摘要随组件锁固定；下载脚本在临时解压树校验原始输入、应用补丁、校验输出，再逐文件核对实际派生树，可重复执行。原始 ZIP 不修改。补丁先设置 STOPPING，再在 manager 锁外注销 Wi-Fi/IP handler 与排空 BLE host；在途事件看到 STOPPING 不再改变连接或配置，重入 deinit 返回 INVALID_STATE。三个 Wi-Fi/IP/私有 STOP 订阅及失败回滚均使用独立 instance 句柄，成功注册后由 ctx 持有，回调参数为 NULL，成功注销后才清句柄。有效句柄注销异常经 ESP_ERROR_CHECK 进入普通 fatal 路径，不伪造 END、不释放仍可能被引用的 ctx，也不以无限 STOPPING 掩盖错误。当前 sdkconfig 禁用 coredump、选择 panic print/reboot；本补丁不新增 dump，也不保证复位。

只有资源清理成功才设 IDLE 并同步通知 END，避免默认事件循环 mutex 与 manager mutex 反锁。三个注册失败出口先注销已注册实例，再在 manager 锁外调用并检查 `prov_stop()`，确认在途 BLE endpoint 请求排空后才释放 handler 表、protocomm 和自有 PoP。STARTING 保持到错误收尾结束，防止锁外排空期间 deinit 抢走 ctx；同一错误尾部停止并清空 timer。singleton mutex 保留用于保护已在等待的调用者，不随会话增长。`free_sec_params()` 仅释放 Security1 自有副本，Security0/2 不释放借用数据；早期启动失败的残留也由持有清理所有权的 deinit 幂等收尾。

所有需要排空 transport 的正常 stop、启动回滚、SESSION_ONLY 和 blocking deinit 都检查停止返回值。停止失败时保留 pc/session_pc、endpoint 表、扫描结果与 PoP，停留 STOPPING，记录 `cleanup_error`，不发 END、不自动重试。只读 `network_prov_mgr_get_cleanup_error()` 无 ctx 时返回 ESP_OK；显式 deinit 对已记录故障仅重试一次，非 OK 表示资源仍被持有。成功启动且尚未 END 的服务由 `end_pending` 保证恰好一次 END；已经 END 的会话和未成功启动/纯 SESSION_ONLY 均不补发 END。资源实际释放后，即使 END/DEINIT 观察副本队列已满，deinit 仍返回 ESP_OK。STARTING、cleanup_running 或其他 deinit 持有清理权时拒绝抢占；自身失败出口复位 `s_deinitializing`。没有扩张已通过的 instance 回收算法。

[IDF 补丁锁](../patches/esp-idf-6.1.json) 与[应用脚本](../tools/apply-idf-patches.py) 固定 IDF 提交及各目标文件应用前后摘要；[观察补丁](../patches/esp-idf-6.1-observer-notifications.patch) 仅将 Security1 五处、NimBLE connect/disconnect 两处观察通知的等待改为 0。这些事件本项目无订阅者，满队列可丢观察副本，协议状态、错误返回与会话创建/关闭逻辑不变，避免默认循环停止 BLE 等 host、host 又等默认循环队列腾空的反锁。manager 的 SET_WIFI_STA_CONFIG / CRED_RECV 异步观察副本同样不等待；同步回调保留，配置回调仍可修改配置。所有修改只作用于本项目 `.tools/` 派生副本。

[事件实例补丁](../patches/esp-idf-6.1-instance-removal.patch) 为当前回调线程的 instance 注销增加独立 deferred 标记：只逻辑失效，不分配、不投递清理消息，重复注销同一仍存活实例幂等。所有 handler/profiling 访问结束后、tick 到期返回前，在 loop 锁内按实例身份删除；每删一个即从树根重扫，避免空祖先释放后的游标失效。legacy 退休节点保留原路径，外部线程仍直接删除。没有新增退休队列或链表。

## 生命周期和状态传递

[纯 C 控制器](../firmware/main/balabala_network.c) 沿用单一生命周期，按 dirty 保存 → 空身份固定准备面板 AOT/真实 ack → prepare → 备份旧 Wi-Fi 配置到 RAM → Wi-Fi start → 生成身份 → 真实身份准备面板 AOT/真实 ack → manager init → Security 1 start 推进。无 dirty 不强制写 SD；保存失败保留学习状态/dirty，可退出错误页且不启动无线。每个显示门禁均在 UI/View drop 后重新采输入，复核同 generation、精确 revision、页面阶段、无待处理请求/取消/关机及原300秒截止；每阶段只提交一次。几何、九状态表与擦除契约见[分阶段配网](staged-provisioning.md)。配置为 BLE / FULL / scheme `NETWORK_PROV_EVENT_HANDLER_NONE`，app callback 仅发布无凭据终态；不使用 keep BLE，也不调用不可逆的 BT memory release。PoP 只在 `esp_wifi_start()` 成功后通过 `esp_fill_random()` 取得 uint32 随机值，拒绝不完整的末尾区间后对 1000000 取模，无偏生成6位ASCII数字，清零原17字节缓冲后写入，保留前导0与NUL终止；不调用会与按键 ADC 冲突的 `bootloader_random_enable()`。

[IDF 适配](../firmware/main/balabala_network_idf.c) 的默认事件观察回调不参与权威状态；RECV / SUCCESS / FAIL / END 由官方 manager 同步 app callback 发布到固定32bit状态通道，绑定本次 generation，不依赖默认队列投递。回调只持短临界区，不读取凭据、不调用 Rust 或 manager、不等待队列。RECV 风暴、默认队列满或 END 观察副本丢弃不能吞掉权威状态。

[广告观察补丁](../patches/esp-idf-6.1-advertising-observer.patch) 在固定 NimBLE transport 每会话配置中捕获不可变 callback/context；仅实际 `ble_gap_adv_start` 成功发布本代 ADVERTISING，地址/字段/scan response/start 失败发布 ADVERTISING_FAILED。NETWORK_PROV_START 不代表广播成功。回调不读凭据、不调用 Rust、生命周期或显示；旧 generation 被固定通道拒绝。广告失败发生于已创建 manager，沿原 stop→END→deinit→restore/release 路径处理；停止错误仍保留所有权。

私有 cleanup STOP 通过周期 timer 非阻塞投递；满队列失败会在下一周期重试，维持原 cleanup_delay（默认 1000ms）。实际开始 cleanup 后才 stop/delete timer。`esp_timer_stop/delete` 不等待已经拷贝 callback/arg 的执行，所以 timer arg 与 STOP payload 使用单调 uint32 token，不指向可释放的 ctx；所有入口先验 token、状态与 cleanup_error，拒绝旧会话、重复 STOP、已清理状态及已记录停止故障，token 耗尽拒绝再启动。END 在 BLE/protocomm/PoP/scan 释放、三个订阅逻辑失效、timer 停删、锁内 IDLE 后锁外同步发出；END 观察副本也在订阅失效后投递。事件实例节点由当前派发尾部物理回收，不阻塞 END。之后旧私有 STOP 栈仅取放永久 mutex，不再访问旧 ctx。同一 loop 的同步 END 回调可以 deinit/init/start，新订阅不会被旧节点回收；不支持在回调中删除正在运行的 loop。平台主任务仍在 END 后依既有顺序清理默认 loop。

每次会话独占默认事件循环；END 后主任务依次 deinit manager、按结果恢复旧配置、停止/释放 Wi-Fi、注销 handler、销毁 STA netif、删除默认事件循环并反初始化自有 NVS 引用。STA 用 `esp_netif_new`、`esp_netif_attach_wifi_station`、`esp_wifi_set_default_wifi_sta_handlers` 三步检查返回，避免便利创建函数内的 assert/ESP_ERROR_CHECK；attach 失败也可能已登记资源，仍走对应清理，清理失败会锁止重开。旧队列被删除且新会话换 generation；清理完成前拒绝并行 start。manager 尚未创建时取消直接释放平台资源，不等待 END；每个 release 步骤成功才清所有权标志，失败保留依赖，无自动重试。`esp_netif_init()` 只执行一次，IDF 尚未实现其 deinit，基础 lwIP 状态/任务可能驻留。

`balabala_runtime_service()` 仅采样输入并收集请求；[runtime_step](../firmware/main/balabala_runtime.c) 的安全点处理网络生命周期，再更新 Rust 状态并渲染。显示 service 回调不启动/关闭网络，不重入渲染。沿用 NETWORK START/CANCEL 请求位，扩展准备/关闭中/清理失败状态；`balabala_app_network_info()` 仅传本代设备名与 PoP，无 Wi-Fi 密码 ABI。准备帧以后，只在原48000 B framebuffer 无分配更新9个小标记及身份擦除区域，经原 begin_render/display_complete 实际全刷；不再 mount/View/font/draw，不增加整屏缓存。返回/Home/关机/Front4 在 ready 之前去重并合并退出意图，明确 released 且 !active 后才离页；清理失败保留面板和按键，不得先转 Home 再以 overlay 假 ack。逻辑擦除先于刷新，显示失败不能声称物理屏已擦除。重复平台状态和身份不提升 revision，没有逐帧队列。

300 秒为连接成功前的配网期限，从本次准备开始计时，包含两个显示门禁且推进阶段不重置；同一安全点先处理已收到的成功事件，再判断到期。299 秒或截止点已收到 GOT_IP 可进入成功读取窗口；进入超时停止态后，迟到成功不能推翻终态。成功不重设/延长期限，而是交给官方查询 stop 或成功后 30 秒 autostop；异步 cleanup 另需时间，不能把入口寿命描述为绝对 300/330 秒。成功等待期间主动退出/取消/关机可提前请求 stop，已确认成功则保留新配置。正常关机等待网络完成清理后才执行原有保存、显示休眠与 MCU deep sleep。查询到清理故障或 deinit 返回非 OK 时，控制器保留 active/manager/saved/success，立即清除平台/模型的身份显示副本，禁止 restore/release；普通 step、取消、关机和迟到 END 都不重试。runtime 撤销本次 shutdown，保留固定清理失败面板与输入，禁止完整 AOT、自动保存、显示休眠或 MCU sleep。平台 release 失败同样保持未释放状态；restore 失败但全部资源已释放可进入完整错误页并锁止重开。

同一安全点尚未 stopping 时先消费 SUCCESS，再处理 CANCEL / SHUTDOWN / 超时，已提交成功时不恢复旧配置；SUCCESS+CANCEL、SUCCESS+SHUTDOWN 及同批 END 均保留新配置。SUCCESS 与 FAIL 在同批无论发布先后都以成功提交为准，前一安全点已确认成功也拒绝后续 FAIL。若 FAIL / 取消 / 超时已在前一安全点触发 stopping，则拒绝迟到 SUCCESS。正面确认键 Front4 在成功页退出也发送关闭请求；取消/超时页仍允许 5/6→4 呈现随后的恢复/释放错误，离页和迟到成功不能改写页面。

错误密码/AP 失败、取消、超时和启动失败会在清理后恢复 RAM 中备份的旧 `wifi_config_t`；恢复前先停止连接，避免在连接态调用 set_config。备份失败不打开配网，也不使用未保存的数据恢复；恢复或可检查的清理 API 失败会报告 FAILED 并锁止本次开机的重开。正常释放后擦除 RAM 中的旧配置、平台身份信息等临时敏感缓冲；随机字节/MAC 使用后清零。停止故障会保留仍被 transport 使用的安全参数及待恢复旧配置，不能提前清除其所有权。平台/模型身份显示副本与 View 的 PoP 行缓冲按终态/退出清除。

查询在活动 step 和 finish 的 deinit 前执行，同一安全点先消费已收到的 SUCCESS，再检查 cleanup_error。内部 `balabala_network_retry_cleanup()` 仅供主线程手动诊断调用，不绑定产品按键、不由普通调度调用。每次只显式 deinit 一次；成功后按已提交 success 决定是否恢复旧配置，再释放平台资源；本次开机的 fault 锁止保留，重复调用不重复释放。人工清理成功不重放已经撤销的 shutdown，新的关机按键请求才继续保存/休眠。

## 安全和设备限制

- Security 1 以非空 PoP 建立官方加密会话；6位数字只有100万种组合，较原16位十六进制PoP显著缩小猜测空间，每会话重新随机生成、沿用原300秒配网窗口且不新增尝试次数限制。PoP 会显示在设备上，持有本次 PoP 的人可尝试配网。未验证实际射频环境下的熵、认证交互或攻击抵抗能力。
- 上游 manager 内部持有凭据并通过 Wi-Fi FLASH storage 保存到 NVS；它在验证前就可能写入新配置。项目以 RAM 备份补偿失败/取消/超时，但断电期间不保证旧凭据回滚，没有持久备份设计。
- 当前未启用 NVS 加密，不能称凭据静态加密安全。NVS init 错误直接失败，不自动 erase 整个 NVS。
- 项目不把真实 SSID、密码或 PoP写入日志、SD、示例。上游 provisioning 编译时设 `LOG_LOCAL_LEVEL=0`，另关闭 Wi-Fi 日志标签，避免其 debug 凭据输出。PoP 在页面终态/退出时擦除模型字段和自有文本缓冲；未声称能法证擦除第三方 UI/协议库内部的所有历史分配。
- 本地补丁处理 transport 停止函数返回的错误；底层若永不返回，本方案没有新增任务或超时强制 deinit，仍可能阻塞。上游内部更低层的未传播错误、实际射频关闭/重开与内存回收仍需真机验证，不能从 END 或宿主测试推导通过。
- 无网络业务，正常退出配网后停止 Wi-Fi 等自有资源；停止故障时保留依赖与固定面板，暂停学习渲染及自动保存；基础 lwIP 可能驻留。没有下调主栈、内容或字体限额；无线运行 heap、连续块、栈高水位和学习页共存仍未证明。

## Wi-Fi IRAM 配置及历史候选

默认和独立自动测试构建均关闭 `CONFIG_ESP_WIFI_RX_IRAM_OPT`、`CONFIG_ESP_WIFI_IRAM_OPT`。`sdkconfig.defaults` 提供新构建的输入；`tools/build.sh` 在现有配置迁移入口同时移除这两项及 `CONFIG_ESP32_WIFI_*` 旧兼容别名的缓存值，再明确写入关闭状态，避免旧 `.build/sdkconfig` 继续启用优化。构建契约检查有效配置中的正式键及兼容别名，开启任一项都会拒绝验收。

本项目同一 R3 phase2 源码和负载的历史离线 A/B 比较中，仅关闭 RX 优化可使实际 ELF `_heap_start` 前移 11776 B；再关闭通用 Wi-Fi 优化可前移 19456 B，多出 7680 B，实际 BIN 3134704 B，原 `0x300000` 分区余 11024 B。当时选择收益较大的 B 进入独立验收，主栈、无线 activity/连接数、RX 缓冲、BLE / FULL / Security 1、内容和字体限额及 phase2 诊断保持。原 A/B 证据保存在 `.build/wifi-iram-comparison-20261027T050214Z/`。随后方案 B 真机在 HCI ACL 整池 7200 B 预分配失败；没有完成预热或成功生命周期，不能把 B 视为已通过设备内存验收。

关闭 IRAM 加速会把相关 Wi-Fi 热点移到 Flash，预期降低 RX 吞吐并增加取指开销，实际吞吐/时延代价尚未测量。19456 B 是静态布局空间，不代表运行 `free` 或 `largest` 的同等增长，也不能证明 controller 后续 host、Security 1 握手或 BLE 活跃时绘屏已具备足够余量。方案 B 当时的设备失败仍保留；后续 runtime mempool、分阶段面板与 z 候选的结果以[当前验收](acceptance.md#当前分阶段配网验证)为准，不能继续把 B 的失败或待 Review 当作当前 z 状态。官方 iOS 端到端还需要实际 iPhone、官方客户端及可用无线环境。

## NimBLE 按需内存配置及历史候选

以下离线尺寸、预算及当时待 Review 状态属于 `.build/ble-runtime-memory-20261027T092234Z/` 历史候选，不代表当前分阶段面板或 z 镜像。当前配置沿用方案 B 两项 IRAM 优化关闭，并保持当时新增的四项有效配置：`CONFIG_BT_NIMBLE_MEMPOOL_RUNTIME_ALLOC=y`、`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`、`CONFIG_BT_CTRL_BLE_MAX_ACT=2`、`CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6`。构建入口同时清除旧连接数/RX 别名及派生 activity 缓存；构建契约核对正式键、兼容别名与保持项。`BLOCK_REUSED=n` 使归还的块立即释放，`STATIC_TO_DYNAMIC=y` 保持；ACL 为 24×255 payload、MSYS 为 12×256 / 24×320、ATT MTU 256、prepare entries 64、RX BA 窗口 6 与主栈 65536 均保持。FULL endpoint、Security 1 与非空 PoP、初始化/回收/诊断逻辑、字体/内容限额及DIO/40MHz不变。当前Flash容量和布局已迁移为16MB，见[16MB扩容验收](acceptance.md#16mb-flash-扩容验收)；下文历史尺寸仍对应各自冻结候选。

固定 IDF 6.0.3 的 `ble_buf_alloc()` 与 `os_msys_buf_alloc()` 在 runtime 模式跳过整池预分配；上下文仍需分配。`os_memblock_get()` 按使用逐块申请，失败归还容量计数；`os_memblock_put_from_cb()` 逐块释放。这直接移除导致 B 启动失败的 7200 B 整笔请求，但分配失败、碎片和时延风险移至 BLE 运行期，不代表 OOM 已修复。单连接/两个 activity 限制并发，六个静态 RX 降低接收突发余量；实际无线性能与官方 iPhone 协议代价未测。

预算比较对象是“假设能完成初始化的 B”，不是已测运行 heap：

| 项目 | 预算变化 | 口径 |
| --- | ---: | --- |
| MSYS 与 HCI 六池 | 移除预分配本金 20948 B | 3072 + 7680 + 2160 + 576 + 260 + 7200；运行按需块另计 |
| connection / L2CAP / prepare / GATT procedure 四池 | 移除预分配本金 1752 B | B 的 564 + 180 + 768 + 240；运行按需块另计 |
| controller activity 6→2 | 估算 3312 B | C3 Kconfig 每 activity 828 B |
| Wi-Fi static RX 10→6 | 估算约 6400 B | Kconfig 每 buffer 约 1.6 KB |
| 合计 | 预算约 32412 B | 不是运行 free/largest 增量，不含 allocator 元数据/结构变化/实际活跃块 |

runtime 已移除 host 静态池，不再叠加单连接原静态池的 496 B；原 B 实际没有成功分配 ACL 7200 B，也不能把这部分记成实测释放收益。后续仍须预算 host 栈 4096 B 及 TCB、GATT 注册与运行块、Security 1 会话 236 B 及未测密码学峰值、五个 endpoint 最多 2560 B 响应内容及节点，以及 UI 绘屏与无线共存。历史父母菜单采样差 28452 B 不是 UI 峰值上界；尚无证据证明运行预算闭合。

最低层宿主测试 [test-nimble-mempool.c](../tools/test-nimble-mempool.c) 由 [脚本](../tools/test-nimble-mempool.sh) 直接编译完整固定 IDF `os_mempool.c`、原始 mempool 结构与配置映射，仅替换 OS 临界区和 heap 边界，不复制分配算法。覆盖 runtime/nonreuse、首次与部分占用后的 OOM 恢复计数、容量耗尽不再申请、归还立即释放、clear/reinit、扩展池回调、40 轮 ACL/MSYS 及 40 轮注册表替换，所有块先归还再 clear/deinit。宿主指针宽度/allocator 和单线程临界区替身不代表 C3 的真实并发、RTOS、碎片或密码学负载。

测试发现固定上游已注册最后一个池重新初始化时，内部 unregister 把注册表初始化标志清除，随后重新插入未恢复标志；下一次访问会丢失该注册项。最小 [固定补丁](../patches/esp-idf-6.0.3-mempool-reinit.patch) 在两处插入前重新调用既有初始化函数，保持其余池、身份语义及所有权规则；同对象、不同对象同名、多池替换和显式注销后重建均覆盖。补丁摘要与重放入口见 [补丁说明](../patches/README.md)。这是离线新发现的注册表缺陷，不能解释为历史设备 7200 B OOM 的根因；没有新增初始化步骤或失败清理策略。

离线证据冻结于 `.build/ble-runtime-memory-20261027T092234Z/`，含原失败回归、源码差异、固定依赖、双镜像/配置/ELF/map、测试日志、内存账本与 manifest。完整宿主测试、phase1/phase2 `test-autotest`、默认和独立 phase2 C3 构建通过；两份有效配置对 B 均只变化上述四键、连接数/RX 旧别名和 activity 派生键共七项。实际 BIN 与 ELF 静态账本如下（IRAM 跨度包含对齐空隙，RTC 仅列分配节）：

| 项目 | 默认 | 独立 phase2 |
| --- | ---: | ---: |
| 实际 BIN / 原分区剩余 | 3127264 / 18464 B | 3134400 / 11328 B |
| `_heap_start` | `0x3fca9ff0` | `0x3fcaa220` |
| 相对 B heap 起点 | 后移 512 B | 不变 |
| IRAM `.text` / 对齐后跨度 | 81120 / 81408 B | 81366 / 81408 B |
| DRAM `.data` / `.bss` | 14553 / 76040 B | 14737 / 76408 B |
| RTC text / noinit / slow / reserved | 20 / 0 / 36 / 24 B | 20 / 64 / 36 / 24 B |

runtime 分支增加了代码，默认因 IRAM 对齐使静态可用空间少 512 B；phase2 对齐跨度不变。上述链接数据不能当成运行 heap 或按需分配的实测收益。两份冻结 ELF 的 `ble_buf_alloc()` 不再申请 HCI 整池，`os_msys_buf_alloc()` 仅保留 88 B 上下文，实际 `os_memblock_get()` / `os_memblock_put_from_cb()` 含 malloc / free 路径；编译命令、预处理单元与反汇编一同保留。

该历史离线阶段尚待独立 Review 与设备验证，后续已在授权原设备上完成指定 z 候选的自动生命周期并采集 heap/主栈样本。当前完整预算、空中广播关闭及官方 iPhone 完整 Security 1/长请求仍未验收；当前状态见[验收文档](acceptance.md#当前分阶段配网验证)。

## 早期 BLE 实现证据（历史）

transport 两项 P1 的独立复审 `f8d9e485-7759-4969-933c-26da1d6f5602` 通过后，本轮在项目根目录执行以下完整验证，均退出 0。当前 `.build/sdkconfig` 启用内存诊断；主栈、内容和字体限额保持。精确 argv、时间、退出码见 `.build/ble-final-{env,host,c3}.json`，完整输出为同名前缀 `.log`。

```sh
python3 tools/test-build-env.py
BALABALA_COMPARE_PAGES="$PWD/.build/ble-baseline/pages" sh tools/build.sh test -- --nocapture
sh tools/build.sh build
```

| 验证范围 | 本轮通过数量 | 证据边界 |
| --- | --- | --- |
| 环境隔离与失败传播 | 3 | 实际 setup、外部 IDF Python 环境隔离、export 失败及缺 shasum 入口拒绝 |
| 全部 Rust | 33，0 过滤 | 学习/存档/导入/渲染、设备名 11 字符、PoP 16 位/字形及窄关机故障 ABI |
| C 输入/存储 | 19 | 含真实 Rust ABI 与文件故障注入 |
| C runtime | 17 | 真实 ABI；清理错误撤销关机、继续学习、不重放旧关机 |
| 配网控制器 | 16 | SUCCESS 优先、查询/deinit 门禁、人工重试与所有权/回滚 |
| IDF 适配 | 5 | 15 启动/8 清理故障点与查询错误不释放；真实 API 形状、驱动替身 |
| manager 函数级 | 8 | 按派生 SHA 原样提取 20 个生产函数，作为补充回归 |
| IDF 观察通知 | 1 | 原样提取 4 函数，验证 7 处 wait=0；密码学 API 为 mock |
| 完整事件循环/protocomm | 14 | 完整固定 esp_event/default_event_loop/protocomm，原样提取 21 个 manager 函数 |

C 使用 ASan/UBSan，Rust 本身未插桩。完整事件测试启用 profiling、按值 FIFO 容量 8、recursive mutex，真实 barrier 最长 3 秒、进程限时 30 秒。原 7 组 instance 验收保持，transport 7 组验证三注册 OOM 时真实 endpoint priv_data/barrier 在途访问、stop 错误保留所有权、启动回滚、SESSION_ONLY、人工两次清理、END 三种计数、满队列 deinit 返回真相、清理权早退和同批 SUCCESS。本轮记录 2665 次分配、10 次注入失败、8 次观察丢弃、25 次重开、queue_waits=0；每组回到唯一永久 mutex 基线，测试进程末 live=0。真实 protocomm endpoint 存储、请求路由与安全参数复制执行；Security1 密码学、provisioning decoder、无线/RTOS/timer 边界仍为替身，不能据此宣称实际 BLE 或密码学通过。

冻结 View 输入的原始 34 张 PBM 本轮全部逐字节一致；当前模型的同名 34 张输出与原基准为 33/34 一致，`network.pbm` 因配网页接通而预期变化。另 6 张配网页为 `ble-connecting`、`ble-success`、`ble-status-{1,4,5,6}`，覆盖字形、16 位 PoP、设备名和终态隐藏。当前 40 张 PBM 与 View 从本轮宿主运行冻结到 `.build/ble-final-pages/`，逐页摘要与两类比较见 `.build/ble-final-page-comparison.json`；历史 PNG 预览不作为本次证据。

### 早期 C3 产物和后续交接（历史）

固定工具链为 IDF 6.0.3 / `76f5dedd9950a3012fee8fb7d5586df21fc67802`、Rust 1.99.1、Bun 1.3.14、RISC-V GCC 15.2.0。首轮构建通过后，符号检查发现内部人工入口被链接 GC；仅在既有 CMake 追加符号保留，再次执行同一 C3 构建退出 0。最终 ELF 包含 80 B 的 `balabala_network_retry_cleanup()`，没有自动调用或产品按钮。首轮日志/尺寸在 `.build/ble-final-initial-c3/`，不混作以下最终结果。

| 资源 | 最终构建 |
| --- | ---: |
| BIN | 3099536 B |
| 3MiB factory 剩余 | 46192 B |
| Flash Code / Data | 1298794 / 1685904 B |
| 静态 DRAM / 链接表 remain | 190673 / 130623 B |
| RTC SLOW / remain | 80 / 8112 B |
| 主任务实际栈请求 | 65536 + 512 = 66048 B |
| NimBLE / BT controller / event / tcpip 栈请求 | 4096 / 4096 / 2816 / 3584 B |

资源 JSON、最终符号、离线镜像检查及可解析栈报告分别为 `.build/ble-final-resources.json`、`.build/ble-final-symbols.txt`、`.build/ble-final-image-info.txt`、`.build/ble-final-stack-report.txt`。栈宏由实际 main.c 编译参数展开，记录在 `.build/ble-final-stack-command.json`。反汇编的 `app_main` 固定 frame 256 B、已解析直调用累积 35280 B，存在环及间接调用，不是运行栈上界。framebuffer 48000 B、stripe 7680 B、内容 32768 B、字体 83807 B 和原主栈配置均未降低。Wi-Fi 内部任务栈、动态 heap/连续块/高水位、并发峰与碎片仍未测。

冻结固件为 `.build/ble-final-firmware/balabala_microts_probe.bin`，SHA256：

```text
6d02cc893b803a64df71dec467ef80525121562c9b44b2d418a3cab6b3e99577
```

同目录含 ELF/map、bootloader、分区表、sdkconfig、构建生成的 flasher_args 与 `SHA256SUMS`。可离线校验：

```sh
(cd .build/ble-final-firmware && shasum -a 256 -c SHA256SUMS)
```

该历史镜像离线解析为 ESP32-C3，checksum/hash 有效；4MB、DIO 80MHz、最低修订 v0.3、factory 0x10000 均为该阶段构建假设，未核实具体 X4，不作为可直接执行的烧录参数。本轮没有访问设备或烧录。进入硬件验证前须核实设备型号、USB 锁定、备份/恢复路径及 Flash/分区；再验证无线 heap/连续块、各任务栈、手机成功读取窗口、错误重试、取消/关机及实际广播关闭。官方 iPhone 端到端仍未通过验证。

本轮最终摘要、相对验证前快照的五文件差异与初版以来的组合差异为 `.build/ble-final-evidence.json`、`.build/ble-final-review.diff`、`.build/ble-final-combined.diff`；无 Git 元数据，组合差异说明每个文件采用的历史快照，不伪称 Git HEAD diff。transport 局部设计/证据仍在 `.build/ble-transport-design.md`、`.build/ble-transport-evidence.json`，本轮完整日志优先用于最终回归判断。剩余软件验收为主 Agent 复核保留符号改动、文档与最终证据并同步 AGENTS；本轮不改 AGENTS。底层 stop 永不返回、设备资源预算及射频行为仍属明确限制。
