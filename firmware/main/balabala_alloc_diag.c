#include "balabala_alloc_diag.h"
#ifdef BALABALA_AUTOTEST_PHASE2
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#if !CONFIG_IDF_TARGET_ESP32C3 || !CONFIG_FREERTOS_UNICORE
#error "allocation diagnostic synchronization requires single-core ESP32-C3"
#endif
#define DIAG_IRAM IRAM_ATTR
#define DIAG_DRAM DRAM_ATTR
#define DIAG_RTC RTC_NOINIT_ATTR
#define diag_fence() __asm__ volatile ("fence rw, rw" ::: "memory")
#define diag_lock() portSET_INTERRUPT_MASK_FROM_ISR()
#define diag_unlock(previous) portCLEAR_INTERRUPT_MASK_FROM_ISR(previous)
_Static_assert(sizeof(size_t) == sizeof(uint32_t), "C3 allocation size width");
#else
#define DIAG_IRAM
#define DIAG_DRAM
#define DIAG_RTC
#define diag_fence() __asm__ volatile ("" ::: "memory")
uint32_t balabala_alloc_diag_host_lock(void);
void balabala_alloc_diag_host_unlock(uint32_t);
#define diag_lock() balabala_alloc_diag_host_lock()
#define diag_unlock(previous) balabala_alloc_diag_host_unlock(previous)
#endif

static DIAG_DRAM volatile BalabalaAllocObservation observation;
static DIAG_DRAM volatile uint32_t current_phase, current_generation;
static void (*sample_heap)(uint32_t, uint32_t, int);
static DIAG_RTC volatile BalabalaAllocRtc alloc_rtc;
#define ALLOC_RTC_MAGIC_V1 0x42414601u

static inline __attribute__((always_inline)) uint32_t rtc_checksum(
        uint32_t size, uint32_t caps, uint32_t phase, uint32_t generation) {
    return ALLOC_RTC_MAGIC_V1 ^ size ^ ((caps << 7) | (caps >> 25)) ^
        ((phase << 13) | (phase >> 19)) ^ ((generation << 19) | (generation >> 13));
}
bool balabala_alloc_diag_rtc_take(esp_reset_reason_t reason, BalabalaAllocRtc *out) {
    /* 仅注册hook前的正常任务调用；先消费，即使输出后又复位也不会重放。 */
    BalabalaAllocRtc saved = alloc_rtc;
    alloc_rtc.magic = 0;
    diag_fence();
    switch (reason) {
    case ESP_RST_SW:
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        break;
    default:
        return false;
    }
    if (saved.magic != ALLOC_RTC_MAGIC_V1 ||
        saved.checksum != rtc_checksum(saved.size, saved.caps, saved.phase, saved.generation))
        return false;
    *out = saved;
    return true;
}
#ifndef ESP_PLATFORM
volatile BalabalaAllocRtc *balabala_alloc_diag_host_rtc(void) { return &alloc_rtc; }
#endif

void balabala_alloc_diag_init(void (*sample)(uint32_t, uint32_t, int)) {
    observation = (BalabalaAllocObservation){0};
    current_phase = current_generation = 0;
    sample_heap = sample;
}
void balabala_alloc_diag_registered(int result) {
    uint32_t previous = diag_lock();
    __asm__ volatile ("" ::: "memory");
    observation.register_result = result;
    __asm__ volatile ("" ::: "memory");
    diag_unlock(previous);
}
/* 不保留function_name，不获取heap锁，不采样/输出。固定阈值4仅支持允许
 * FreeRTOS/heap调用的上下文；不能宣称任意高优先级ISR或原阈值>4安全。 */
void DIAG_IRAM balabala_alloc_diag_failed(size_t size, uint32_t caps, const char *function_name) {
    (void)function_name;
    uint32_t previous = diag_lock();
    __asm__ volatile ("" ::: "memory");
    if (!observation.latched) {
        /* 同一临界区内使用与DRAM记录相同的phase/generation快照。RTC直连
         * 内存不走flash cache；先失效，再写完整载荷，fence后最后提交magic。 */
        uint32_t phase = current_phase, generation = current_generation;
        alloc_rtc.magic = 0;
        diag_fence();
        alloc_rtc.size = (uint32_t)size;
        alloc_rtc.caps = caps;
        alloc_rtc.phase = phase;
        alloc_rtc.generation = generation;
        alloc_rtc.checksum = rtc_checksum((uint32_t)size, caps, phase, generation);
        diag_fence();
        alloc_rtc.magic = ALLOC_RTC_MAGIC_V1;
        diag_fence();
    }
    observation.latched = 1;
    uint32_t index = observation.count;
    if (index < BALABALA_ALLOC_DIAG_CAPACITY) {
        observation.failures[index].size = (uint32_t)size;
        observation.failures[index].caps = caps;
        observation.failures[index].phase = current_phase;
        observation.failures[index].generation = current_generation;
        observation.count = index + 1;
    } else {
        observation.overflow = 1;
    }
    __asm__ volatile ("" ::: "memory");
    diag_unlock(previous);
}
bool balabala_alloc_diag_blocked(void) {
    uint32_t previous = diag_lock();
    __asm__ volatile ("" ::: "memory");
    bool blocked = observation.latched || observation.register_result;
    __asm__ volatile ("" ::: "memory");
    diag_unlock(previous);
    return blocked;
}
void balabala_alloc_diag_observe(BalabalaAllocObservation *out) {
    uint32_t previous = diag_lock();
    __asm__ volatile ("" ::: "memory");
    *out = observation;
    __asm__ volatile ("" ::: "memory");
    diag_unlock(previous);
}
void balabala_alloc_diag_checkpoint(uint32_t phase, uint32_t generation, int result) {
    uint32_t previous = diag_lock();
    __asm__ volatile ("" ::: "memory");
    current_phase = phase; current_generation = generation;
    __asm__ volatile ("" ::: "memory");
    diag_unlock(previous);
    if (sample_heap) sample_heap(phase, generation, result);
}
#endif
