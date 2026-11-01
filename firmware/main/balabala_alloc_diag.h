#ifndef BALABALA_ALLOC_DIAG_H
#define BALABALA_ALLOC_DIAG_H
#ifdef BALABALA_AUTOTEST_PHASE2
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_system.h"
#define BALABALA_ALLOC_DIAG_CAPACITY 4u
enum {
    BALABALA_ALLOC_PREPARE_BEFORE=1, BALABALA_ALLOC_PREPARE_AFTER,
    BALABALA_ALLOC_RADIO_AFTER, BALABALA_ALLOC_MANAGER_BEFORE,
    BALABALA_ALLOC_MANAGER_AFTER, BALABALA_ALLOC_FAILED_FINISH_AFTER,
};
typedef struct {
    uint32_t size, caps, phase, generation;
} BalabalaAllocFailure;
/* 独立RTC记录；magic包含版本，最后提交。generation是checkpoint的网络代次。 */
typedef struct {
    uint32_t magic, size, caps, phase, generation, checksum;
} BalabalaAllocRtc;
_Static_assert(sizeof(BalabalaAllocRtc) == 24, "allocation RTC record size");
/* 每次启动在init/注册hook/自动测试入口之前调用；复制后消费，冷启动残留丢弃。 */
bool balabala_alloc_diag_rtc_take(esp_reset_reason_t, BalabalaAllocRtc *);
#ifndef ESP_PLATFORM
/* 宿主故障注入访问同一记录，不替换保存或消费路径。 */
volatile BalabalaAllocRtc *balabala_alloc_diag_host_rtc(void);
#endif
typedef struct {
    BalabalaAllocFailure failures[BALABALA_ALLOC_DIAG_CAPACITY];
    uint32_t count, overflow, latched;
    int register_result;
} BalabalaAllocObservation;
/* init仅在注册hook之前调用。sample只在调用方主任务中执行。 */
void balabala_alloc_diag_init(void (*sample)(uint32_t, uint32_t, int));
void balabala_alloc_diag_registered(int result);
void balabala_alloc_diag_failed(size_t, uint32_t, const char *);
bool balabala_alloc_diag_blocked(void);
void balabala_alloc_diag_observe(BalabalaAllocObservation *);
void balabala_alloc_diag_checkpoint(uint32_t phase, uint32_t generation, int result);
#else
#define balabala_alloc_diag_checkpoint(phase, generation, result) ((void)0)
#endif
#endif
