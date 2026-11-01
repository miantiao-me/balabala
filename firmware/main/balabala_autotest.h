#ifndef BALABALA_AUTOTEST_H
#define BALABALA_AUTOTEST_H
#include "balabala_runtime.h"
#include "balabala_storage.h"
#include "balabala_input.h"
#include "esp_system.h"
#ifdef BALABALA_AUTOTEST_PHASE2
#define BALABALA_AUTOTEST_PHASE 2
#define BALABALA_AUTOTEST_TIMEOUT_MS 60000
#else
#define BALABALA_AUTOTEST_PHASE 1
#define BALABALA_AUTOTEST_TIMEOUT_MS 120000
#endif
#define BALABALA_AUTOTEST_ROOT_BYTES 48
/* 全部为固定宽度整数；须与 feature=autotest 的 Rust repr(C) 完全一致。 */
typedef struct {
    uint64_t revision, displayed, mutation, generation, digest;
    uint32_t page, selected, subject, held, rendering, dirty, slot;
    uint32_t mode, cursor, flipped, correct, count, scored, progress;
    int32_t answers[10];
} BalabalaAutotestSnapshot;
_Static_assert(sizeof(BalabalaAutotestSnapshot) == 136, "autotest snapshot ABI");
int32_t balabala_app_autotest_snapshot(const BalabalaApp *, BalabalaAutotestSnapshot *);
/* RTC仅保存隔离身份与恢复预期，不写NVS；stage=1已武装，2已消耗唯一恢复机会，3终态。 */
typedef struct {
    uint32_t magic, stage, id[2], checksum;
    uint32_t slot;
    uint64_t digest, generation;
} BalabalaAutotestRtc;
typedef struct {
    void (*emit)(void *, BalabalaKey, bool);
    void (*log)(void *, unsigned, unsigned, uint64_t, uint64_t);
    void (*memory)(void *, unsigned, uint32_t *, uint32_t *);
    void (*restart)(void *);
    void *context;
} BalabalaAutotestOps;
typedef struct {
    BalabalaAutotestOps ops;
    BalabalaAutotestRtc *rtc;
    BalabalaAutotestSnapshot before;
    uint64_t since, dwell, memory_digest;
    uint32_t saves, step, subject, cycle, memory_free, memory_largest;
    bool stopped, waiting;
#ifdef BALABALA_AUTOTEST_PHASE2
    uint32_t network_generation, sampled_generation;
    BalabalaNetworkObservation tail_network;
    bool tail_network_valid;
#endif
} BalabalaAutotest;
/* POWERON/USB清RTC并独占新目录；phase1 SW仅恢复stage=1一次，phase2拒绝SW；其他原因拒绝。
 * mount=NULL表示SD不可用：仍执行RTC策略但不进行SD IO。
 * mkdir只接受新目录，8次碰撞上限。失败root清空，调用方必须停止启动。 */
int balabala_autotest_start(char root[BALABALA_AUTOTEST_ROOT_BYTES], const char *mount,
                           BalabalaAutotestRtc *rtc, esp_reset_reason_t reset_reason,
                           uint32_t (*random_u32)(void *), void *context);
/* 候选或稳定按键仅阻止脚本注入；只有physical_emit才宣告人工退出。 */
bool balabala_autotest_input_idle(const BalabalaInput *input);
void balabala_autotest_init(BalabalaAutotest *, BalabalaAutotestRtc *, const BalabalaAutotestOps *, uint64_t);
void balabala_autotest_user_input(BalabalaAutotest *);
void balabala_autotest_saved(BalabalaAutotest *, const BalabalaAutotestSnapshot *,
                            const BalabalaAutotestSnapshot *, int, BalabalaStorageStatus);
#ifdef BALABALA_AUTOTEST_PHASE2
void balabala_autotest_before_display(BalabalaAutotest *, BalabalaRuntime *);
#endif
void balabala_autotest_tick(BalabalaAutotest *, BalabalaRuntime *, BalabalaStorage *, uint64_t, bool);
#endif
