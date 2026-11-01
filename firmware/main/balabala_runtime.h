#ifndef BALABALA_RUNTIME_H
#define BALABALA_RUNTIME_H
#include <stdbool.h>
#include "balabala_probe.h"
#include "balabala_network.h"
#define BALABALA_DISPLAY_MAX_ATTEMPTS 3
#define BALABALA_DISPLAY_RETRY_MS 1000
#define BALABALA_SAVE_INTERVAL_MS 1000
#define BALABALA_SAVE_RETRY_MS 5000
#define BALABALA_SLEEP_RELEASE_MS 30
#define BALABALA_AUTO_SLEEP_MS (5u * 60u * 1000u)
/* 返回值：save 0已保存/无dirty、1失败、2存储不可用；其余0成功。
 * display同步完成或返回失败；其内部须周期调用runtime_service。
 * poll只输入，不嵌套step；所有依赖与ABI在同一任务调用。 */
typedef struct {
    uint64_t (*now_ms)(void *context);
    void (*poll)(void *context, BalabalaApp *app);
    int (*display)(void *context, const uint8_t *frame, bool partial);
    int (*save)(void *context, BalabalaApp *app);
    int (*display_sleep)(void *context);
    bool (*power_released)(void *context);
    int (*sleep)(void *context);
    void *context;
    BalabalaNetwork *network;
    /* check只读，返回0或DOWNLOAD错误枚举；enter成功不返回，返回即锁止本次开机。
     * 这两个为项目注入接口，不是ESP-IDF API。 */
    int (*download_check)(void *context);
    void (*enter_download)(void *context);
    /* NULL禁用自动休眠；必须确认原始候选/已确认按键全释放且采样有效。 */
    bool (*input_idle)(void *context);
    /* 原版X4 GPIO20高电平为USB供电；接电阻止自动休眠，手动关机不受影响。 */
    bool (*external_power)(void *context);
} BalabalaRuntimeOps;
typedef struct {
    BalabalaApp *app;
    uint8_t *frame;
    uint16_t *stripe;
    BalabalaRuntimeOps ops;
    uint64_t revision, retry_at, save_at, released_since, last_activity_ms;
    uint16_t held_keys;
    unsigned attempts, download;
    uint64_t download_revision, network_frame_revision, shutdown_revision;
    uint32_t network_generation, network_phase;
    uint32_t requests, network_revision;
    int display_result, save_result, sleep_result, download_result;
    bool partial, download_blocked, pending, display_fault, shutdown, automatic_shutdown, shutdown_started, shutdown_prepared, releasing, sleep_attempted;
} BalabalaRuntime;
int balabala_runtime_init(BalabalaRuntime *runtime, BalabalaApp *app, uint8_t *frame,
                          uint16_t *stripe, const BalabalaRuntimeOps *ops, bool storage_available);
/* 物理输入在应用门禁前调用；未改变页面的按键和松开也重置无操作计时。 */
void balabala_runtime_activity(BalabalaRuntime *runtime, BalabalaKey key, bool pressed);
/* 可从display的等待/每行传输回调调用，只采样与收集请求，无SPI/存储/渲染。 */
void balabala_runtime_service(BalabalaRuntime *runtime);
/* 一次有界策略迭代，调用方每轮让出CPU。终态显示故障仍处理Home/Shutdown。
 * pending帧和revision在成功前保持；最多初次+两次完整重试，失败后不再刷新。 */
void balabala_runtime_step(BalabalaRuntime *runtime);
#endif
