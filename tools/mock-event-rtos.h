/* 宿主替身只提供RTOS/allocator；事件注册、派发、回收用固定IDF完整源码。 */
#pragma once
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <setjmp.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
static const char *esp_err_to_name(esp_err_t e) { (void)e; return "mock error"; }
#define CONFIG_ESP_EVENT_LOOP_PROFILING 1
#define CONFIG_ESP_EVENT_POST_FROM_ISR 0
#define CONFIG_ESP_EVENT_LOOP_IN_EXT_RAM 0
#define FORCE_INLINE_ATTR static inline __attribute__((always_inline))
#define MALLOC_CAP_DEFAULT (1U << 12)
#define CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE 8
#define ESP_TASKD_EVENT_STACK 2816
#define ESP_TASKD_EVENT_PRIO 20
#define configUSE_16_BIT_TICKS 0
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define ESP_LOGE(tag,...) do { (void)(tag); if(0) fprintf(stderr,__VA_ARGS__); } while(0)
#define ESP_LOGD ESP_LOGE
#define ESP_LOGI ESP_LOGE
#define ESP_LOGW ESP_LOGE
#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(var,head,field,tmp) \
    for((var)=SLIST_FIRST(head); (var) && ((tmp)=SLIST_NEXT(var,field),1); (var)=(tmp))
#endif

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef void *TaskHandle_t;
typedef pthread_mutex_t *SemaphoreHandle_t;
typedef struct { pthread_mutex_t mutex; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {PTHREAD_MUTEX_INITIALIZER}
#define portENTER_CRITICAL(p) assert(!pthread_mutex_lock(&(p)->mutex))
#define portEXIT_CRITICAL(p) assert(!pthread_mutex_unlock(&(p)->mutex))
static _Thread_local int task_identity;
static _Thread_local TaskHandle_t actor_task;
static int default_task;
static atomic_uint ticks;
static unsigned created_mutexes;
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return actor_task ? actor_task : &task_identity; }
static TickType_t xTaskGetTickCount(void) { return atomic_load(&ticks); }
static void vTaskDelay(TickType_t n) { atomic_fetch_add(&ticks,n); }
static void vTaskSuspend(void *p) { (void)p; assert(false); }
static void vTaskDelete(TaskHandle_t p) { assert(p==&default_task); }
static BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *task,int core) {
    assert(fn && name && stack && arg && priority && core==0); *task=&default_task; return pdPASS;
}
static long long esp_timer_get_time(void) { return (long long)atomic_fetch_add(&ticks,1)*1000; }
static struct timespec deadline(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_REALTIME,&t)); t.tv_sec+=3; return t;
}
static pthread_mutex_t allocation_mutex=PTHREAD_MUTEX_INITIALIZER;
static void *live_pointers[8192];
static size_t live_sizes[8192];
static size_t live_allocations,allocation_calls,allocation_failures;
static long fail_after=-1;
static bool deny_allocations;
static void *test_calloc(size_t n,size_t size) {
    assert(!pthread_mutex_lock(&allocation_mutex)); ++allocation_calls;
    if(deny_allocations || fail_after==0) {
        if(fail_after==0) fail_after=-1;
        ++allocation_failures; assert(!pthread_mutex_unlock(&allocation_mutex)); return NULL;
    }
    if(fail_after>0) --fail_after;
    void *p=calloc(n,size); assert(p); size_t slot=0;
    while(live_pointers[slot]) { ++slot; assert(slot<8192); }
    live_pointers[slot]=p; live_sizes[slot]=n*size; ++live_allocations;
    assert(!pthread_mutex_unlock(&allocation_mutex)); return p;
}
static void *heap_caps_calloc(size_t n,size_t size,uint32_t caps) {
    assert(caps==MALLOC_CAP_DEFAULT); return test_calloc(n,size);
}
static void *test_malloc(size_t n) { return test_calloc(1,n); }
static char *test_strdup(const char *s) { size_t n=strlen(s)+1; char *p=test_malloc(n); if(p) memcpy(p,s,n); return p; }
static void test_free(void *p) {
    if(!p) return;
    assert(!pthread_mutex_lock(&allocation_mutex)); size_t slot=0;
    while(live_pointers[slot]!=p) { ++slot; assert(slot<8192); }
    live_pointers[slot]=NULL; --live_allocations; free(p);
    assert(!pthread_mutex_unlock(&allocation_mutex));
}
#define malloc test_malloc
#define calloc test_calloc
#define strdup test_strdup
#define free test_free
static SemaphoreHandle_t new_mutex(bool recursive) {
    SemaphoreHandle_t p=malloc(sizeof(*p)); if(!p) return NULL;
    pthread_mutexattr_t attr; assert(!pthread_mutexattr_init(&attr));
    if(recursive) assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(p,&attr)); assert(!pthread_mutexattr_destroy(&attr)); ++created_mutexes; return p;
}
static SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return new_mutex(true); }
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return new_mutex(false); }
static BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t p,TickType_t wait) {
    if(!wait) { int r=pthread_mutex_trylock(p); assert(!r || r==EBUSY); return !r; }
    assert(!pthread_mutex_lock(p)); return pdTRUE;
}
static BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t p) { assert(!pthread_mutex_unlock(p)); return pdTRUE; }
static void vSemaphoreDelete(SemaphoreHandle_t p) { assert(!pthread_mutex_destroy(p)); free(p); }

typedef struct Fifo {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    size_t capacity,item_size,head,count;
    unsigned char *data;
} *QueueHandle_t;
static atomic_uint queue_waits,queue_failures;
static QueueHandle_t xQueueCreate(size_t n,size_t size) {
    QueueHandle_t q=calloc(1,sizeof(*q)); if(!q) return NULL;
    q->data=calloc(n,size); if(!q->data) { free(q); return NULL; }
    q->capacity=n; q->item_size=size; assert(!pthread_mutex_init(&q->mutex,NULL));
    assert(!pthread_cond_init(&q->changed,NULL)); return q;
}
static BaseType_t xQueueSendToBack(QueueHandle_t q,const void *p,TickType_t wait) {
    assert(!pthread_mutex_lock(&q->mutex)); struct timespec until=deadline();
    while(q->count==q->capacity) {
        if(!wait) { ++queue_failures; assert(!pthread_mutex_unlock(&q->mutex)); return pdFALSE; }
        ++queue_waits; assert(pthread_cond_timedwait(&q->changed,&q->mutex,&until)==0);
    }
    memcpy(q->data+((q->head+q->count)%q->capacity)*q->item_size,p,q->item_size); ++q->count;
    assert(!pthread_cond_broadcast(&q->changed)); assert(!pthread_mutex_unlock(&q->mutex)); return pdTRUE;
}
static BaseType_t xQueueReceive(QueueHandle_t q,void *p,TickType_t wait) {
    assert(!pthread_mutex_lock(&q->mutex));
    if(!q->count) {
        if(wait!=portMAX_DELAY) atomic_fetch_add(&ticks,wait);
        assert(!pthread_mutex_unlock(&q->mutex)); return pdFALSE;
    }
    memcpy(p,q->data+q->head*q->item_size,q->item_size); q->head=(q->head+1)%q->capacity; --q->count;
    assert(!pthread_cond_broadcast(&q->changed)); assert(!pthread_mutex_unlock(&q->mutex)); return pdTRUE;
}
static void vQueueDelete(QueueHandle_t q) {
    assert(!pthread_mutex_destroy(&q->mutex)); assert(!pthread_cond_destroy(&q->changed)); free(q->data); free(q);
}
static jmp_buf fatal_jump;
static bool fatal_expected;
static unsigned fatal_count;
static void test_fatal(esp_err_t error,const char *file,int line) {
    assert(error!=ESP_OK && file && line>0); ++fatal_count;
    assert(fatal_expected); longjmp(fatal_jump,1);
}
#define ESP_ERROR_CHECK(expr) do { esp_err_t e_=(expr); if(e_!=ESP_OK) test_fatal(e_,__FILE__,__LINE__); } while(0)
