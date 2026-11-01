#ifndef BALABALA_NETWORK_H
#define BALABALA_NETWORK_H
#include <stdbool.h>
#include <stdint.h>
#include "balabala_probe.h"
#define BALABALA_NETWORK_CONNECT_TIMEOUT_MS 300000u
#define BALABALA_NETWORK_NAME_LEN 11u
#define BALABALA_NETWORK_POP_LEN 16u
/* 位归并通道固定大小，END/失败不会被普通事件挤出。generation隔离旧会话。 */
enum { BALABALA_NET_EVENT_RECV=1, BALABALA_NET_EVENT_SUCCESS=2, BALABALA_NET_EVENT_FAIL=4, BALABALA_NET_EVENT_END=8,
       BALABALA_NET_EVENT_ADVERTISING=16, BALABALA_NET_EVENT_ADVERTISING_FAILED=32 };
typedef struct {
    int (*prepare)(void *context, uint32_t generation);
    int (*backup)(void *context);
    int (*radio_start)(void *context);
    /* PoP为6位ASCII数字；保留17字节缓冲，尾部必须清零。 */
    int (*identity)(void *context, char name[12], char pop[17]);
    int (*manager_init)(void *context);
    int (*manager_start)(void *context, const char *name, const char *pop);
    void (*manager_stop)(void *context);
    int (*manager_deinit)(void *context);
    int (*manager_cleanup_error)(void *context);
    int (*restore)(void *context);
    int (*release)(void *context);
    void (*lock)(void *context);
    void (*unlock)(void *context);
    void *context;
} BalabalaNetworkOps;
#ifdef BALABALA_AUTOTEST_PHASE2
typedef struct {
    uint32_t generation, ends, starts, stops, deinits, releases;
    int start_result, deinit_result, release_result;
} BalabalaNetworkObservation;
#endif
typedef struct {
    BalabalaNetworkOps ops;
#ifdef BALABALA_AUTOTEST_PHASE2
    BalabalaNetworkObservation observation;
#endif
    uint32_t generation, events, status, revision, phase, terminal_status;
    uint64_t deadline;
    char name[12], pop[17];
    bool active, stopping, manager, started, saved, success, fault, released;
    int error, cleanup_error;
} BalabalaNetwork;
#ifdef BALABALA_AUTOTEST_PHASE2
void balabala_network_observe(BalabalaNetwork *, BalabalaNetworkObservation *);
#endif
void balabala_network_init(BalabalaNetwork *network, const BalabalaNetworkOps *ops);
/* 唯一异步入口：只复制不含凭据的状态，lock/unlock必须线程安全。 */
void balabala_network_publish(BalabalaNetwork *network, uint32_t generation, uint32_t events);
/* 仅主平台任务安全点调用，禁止从显示service或默认事件回调调用。 */
void balabala_network_step(BalabalaNetwork *network, uint32_t requests, uint64_t now);
/* 两次精确显示门禁后各调用一次；phase 1准备无线/身份，phase 2激活manager。 */
int balabala_network_advance(BalabalaNetwork *network, uint32_t generation, uint32_t phase, uint64_t now);
/* 仅主任务手动诊断入口；不绑定按键、不自动调用，成功后仍锁止本次开机配网。 */
int balabala_network_retry_cleanup(BalabalaNetwork *network);
/* 用于平台敏感缓冲，volatile写避免优化移除。 */
void balabala_network_clear(void *data, unsigned length);
#endif
