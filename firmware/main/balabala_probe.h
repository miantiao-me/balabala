#ifndef BALABALA_PROBE_H
#define BALABALA_PROBE_H
#include <stddef.h>
#include <stdint.h>
#define BALABALA_WIDTH 480
#define BALABALA_HEIGHT 800
#define BALABALA_FRAMEBUFFER_BYTES 48000
#define BALABALA_STRIPE_PIXELS (480 * 8)
#define BALABALA_MAX_DECK_BYTES 65536
#define BALABALA_MAX_SAVE_BYTES 32768

typedef struct BalabalaApp BalabalaApp;
typedef enum {
    BALABALA_FRONT1 = 0, BALABALA_FRONT2, BALABALA_FRONT3, BALABALA_FRONT4,
    BALABALA_UP, BALABALA_DOWN, BALABALA_HOME, BALABALA_SHUTDOWN, BALABALA_PARENT
} BalabalaKey;
enum { BALABALA_NEEDS_REFRESH=1, BALABALA_DISPLAY_PENDING=2, BALABALA_SAVE_DIRTY=4,
       BALABALA_DOWNLOAD_ACTIVE=8, BALABALA_DOWNLOAD_PROMPT=16, BALABALA_NETWORK_OWNED=32 };
enum { BALABALA_REQUEST_SHUTDOWN=1, BALABALA_REQUEST_NETWORK_START=2, BALABALA_REQUEST_NETWORK_CANCEL=4,
       BALABALA_REQUEST_DOWNLOAD=8, BALABALA_REQUEST_DOWNLOAD_CANCEL=16 };
enum { BALABALA_NETWORK_WAITING=1, BALABALA_NETWORK_CONNECTING=2, BALABALA_NETWORK_SUCCESS=3, BALABALA_NETWORK_FAILED=4,
       BALABALA_NETWORK_CANCELLED=5, BALABALA_NETWORK_TIMEOUT=6, BALABALA_NETWORK_PREPARING=7,
       BALABALA_NETWORK_CLOSING=8, BALABALA_NETWORK_CLEANUP_FAILED=9 };
/* 所有调用由同一平台任务串行执行；对象和所有字节指针由调用方保证生命周期。
 * 输出缓冲有效、可写、互不重叠且不覆盖app；stripe按uint16_t对齐。
 * 480x800逻辑竖屏，48000字节，1白/0黑、MSB左、行优先。物理屏旋转交平台。
 * OOM/panic终止并由平台记录；禁止跨ABI unwind或并发访问同一app。 */
BalabalaApp *balabala_app_create(void);
void balabala_app_destroy(BalabalaApp *app);
/* pressed仅0/1。侧/正面键必须上报按下和松开（busy时也上报松开）。
 * 平台去抖并在电源松开时判短按；长按仅发Shutdown，不能先发Home。 */
int32_t balabala_app_key(BalabalaApp *app, uint32_t key, uint32_t pressed);
enum { BALABALA_SAVE_AVAILABLE=0, BALABALA_SAVE_UNAVAILABLE=1, BALABALA_SAVE_FAILED=2 };
/* 只更新会话内存储提示，不改变学习错误页/存档dirty；相同状态不触发刷新。
 * AVAILABLE仅代表平台存储可用；保存成功仍必须由save_ack确认。 */
int32_t balabala_app_storage_status(BalabalaApp *app, uint32_t status);
/* 仅卡组读取失败使用（过大/IO/分配失败），缺卡和无SD不调用。
 * 进入可退出错误页并提升revision；已有Error页/其具体文案保持不变。
 * 后续load仍恢复有效状态并保留该错误页；pending帧仍须按原协议处理。
 * 0已处理/已有错误，-1空对象。 */
int32_t balabala_app_storage_error(BalabalaApp *app);
uint32_t balabala_app_status(const BalabalaApp *app);
/* 0已写帧并返回revision；1没有待绘页或显示尚未完成；-1参数错；-2资源错。
 * 只有物理显示完成才能ack该revision；此后如仍有NEEDS_REFRESH，再渲染最新状态。
 * 渲染期间导航合并到最新状态，无逐帧队列；Home/Shutdown仍立即处理。 */
int32_t balabala_app_render(BalabalaApp *app, uint8_t *framebuffer, size_t bytes,
                            uint16_t *stripe, size_t pixels, uint64_t *revision);
/* render成功后、物理显示前查询；1允许局刷，0要求全刷/没有pending帧。 */
uint32_t balabala_app_refresh_partial(const BalabalaApp *app);
/* 家长选择的连续局刷上限：8/16/32/64/128；NULL返回默认8，不取消关键页全刷。 */
uint32_t balabala_app_clear_interval(const BalabalaApp *app);
int32_t balabala_app_display_complete(BalabalaApp *app, uint64_t revision);
/* 平台确认无线完整释放后调用；仅clean且没有pending帧时准备易失休眠页。
 * 返回待全刷revision，0拒绝。保存失败不得用休眠页遮盖错误。 */
uint64_t balabala_app_sleep_ready(BalabalaApp *app);
/* 接电取消自动休眠：仅休眠页回首页并撤销其在途帧，返回1；其他页0，NULL -1。 */
int32_t balabala_app_sleep_cancel(BalabalaApp *app);
/* 下载准备与提示均为易失状态，不属于存档。ready仅在准备中且clean时切提示页，
 * 返回专属revision，0拒绝；必须全刷并ack精确revision后才可休眠/进入ROM。
 * failed仅撤销仍有效的下载事务；不ack/放弃任何pending帧。 */
enum { BALABALA_DOWNLOAD_UNSUPPORTED=1, BALABALA_DOWNLOAD_FORCE_DISABLED=2,
       BALABALA_DOWNLOAD_DISABLED=3, BALABALA_DOWNLOAD_USB_DISABLED=4,
       BALABALA_DOWNLOAD_USB_BOOT_DISABLED=5, BALABALA_DOWNLOAD_SECURITY=6,
       BALABALA_DOWNLOAD_SAVE_FAILED=7, BALABALA_DOWNLOAD_NO_STORAGE=8,
       BALABALA_DOWNLOAD_NETWORK_FAILED=9, BALABALA_DOWNLOAD_DISPLAY_FAILED=10,
       BALABALA_DOWNLOAD_SLEEP_FAILED=11, BALABALA_DOWNLOAD_RETURNED=12,
       BALABALA_DOWNLOAD_DIRTY=13 };
uint64_t balabala_app_download_ready(BalabalaApp *app);
int32_t balabala_app_download_failed(BalabalaApp *app, uint32_t reason);
/* SD读取由平台完成。≤65536字节完整UTF-8 JSON；全量验证后替换对应学科。
 * 返回0成功，-1参数/尺寸错，-2卡组错误且显示可退出错误页；旧卡组保留。
 * 长度0允许NULL，表示缺卡；其他非空指针必须有声明长度。 */
int32_t balabala_app_import_deck(BalabalaApp *app, const uint8_t *bytes, size_t length);
/* 课程目录固化于Flash，正文位于SD。info返回匹配包的总字节数和payload CRC。
 * 平台完整流式核验BALC头及CRC后attach；reader/context串行借用至app销毁。
 * reader精确读取offset/length，0成功；读取失败不会用旧正文替代。 */
typedef int32_t (*BalabalaCourseRead)(void *context, uint32_t offset, uint8_t *out, size_t length);
int32_t balabala_app_curriculum_info(uint32_t *bytes, uint32_t *crc);
int32_t balabala_app_curriculum_attach(BalabalaApp *app, BalabalaCourseRead reader, void *context,
                                      uint32_t bytes, uint32_t crc);
/* 启动先导入平台现存卡组，再load两槽；每槽≤32768字节，NULL+0表示无槽。
 * 0恢复最高有效且可解释generation（保留先前卡组错误页）；1无可恢复状态并锁住保存，
 * 若无既有错误则显示可退出存档错误页。 */
int32_t balabala_app_load(BalabalaApp *app, const uint8_t *a, size_t alen,
                        const uint8_t *b, size_t blen);
/* 仅内存保护：清除待ack事务并锁住保存，不改变页面/revision；NULL返回-1。
 * 保护跨mutation/重置持续，只有成功load有效且可解释状态才能解除。 */
int32_t balabala_app_save_protect(BalabalaApp *app);
/* out容量至少32768。正数为记录字节数，slot为应覆盖的非活动槽(0/1)。
 * 平台只写该槽，落盘并读回核验后ack；失败ack(0)，不得覆盖另一个有效槽。
 * 同时仅一笔事务；-3为保存受保护、上笔待ack且状态已变化或编码失败。失败不清dirty。 */
int32_t balabala_app_save_export(BalabalaApp *app, uint8_t *out, size_t capacity,
                               uint32_t *slot, uint64_t *generation);
int32_t balabala_app_save_ack(BalabalaApp *app, uint64_t generation, uint32_t success);
/* 读取后清空请求位；SHUTDOWN须在完成存档后执行。配网不传密码入模型。
 * 平台以乐鑫官方Security1协议接入。离开后拒绝迟到状态。 */
uint32_t balabala_app_platform_requests(BalabalaApp *app);
/* phase 1空身份首帧，2真实身份帧，0其余；flags: bit0活动、bit1确认释放、bit2锁止重开。
 * phase返回模型revision；ready要求真实ack、同代同阶段精确revision且无退出请求。 */
uint64_t balabala_app_network_phase(BalabalaApp *app, uint32_t generation, uint32_t status,
                                   uint32_t phase, uint32_t flags);
uint32_t balabala_app_network_ready(const BalabalaApp *app, uint32_t generation, uint32_t phase, uint64_t revision);
int32_t balabala_app_network_save_failed(BalabalaApp *app);
/* 清理失败撤销本次关机意图，保持固定面板。 */
int32_t balabala_app_network_shutdown_blocked(BalabalaApp *app);
int32_t balabala_app_network_status(BalabalaApp *app, uint32_t status);
/* 仅PROV_+6位大写hex设备名和6位ASCII十进制PoP（允许前导0）；长度0+0清除会话显示。
 * 不接受Wi-Fi凭据，不持久化；相同值不触发刷新。 */
int32_t balabala_app_network_info(BalabalaApp *app, const uint8_t *name, size_t name_len,
                                  const uint8_t *pop, size_t pop_len);
/* 兼容最初探针调用：当前输出桌面，缓冲及返回值同render。 */
int32_t balabala_probe_render(uint8_t *framebuffer, size_t bytes,
                            uint16_t *stripe, size_t pixels);
#endif
