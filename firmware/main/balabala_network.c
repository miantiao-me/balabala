#include "balabala_network.h"
#include "balabala_alloc_diag.h"
#include <string.h>

void balabala_network_clear(void *data, unsigned length) {
    volatile unsigned char *p = data;
    while (length--) *p++ = 0;
}
void balabala_network_init(BalabalaNetwork *n, const BalabalaNetworkOps *ops) {
    memset(n, 0, sizeof(*n));
    n->ops = *ops;
    n->released = true;
}
void balabala_network_publish(BalabalaNetwork *n, uint32_t generation, uint32_t events) {
    n->ops.lock(n->ops.context);
    if (generation == n->generation) {
        n->events |= events;
#ifdef BALABALA_AUTOTEST_PHASE2
        if ((events & BALABALA_NET_EVENT_END) && n->observation.ends < UINT32_MAX) ++n->observation.ends;
#endif
    }
    n->ops.unlock(n->ops.context);
}
#ifdef BALABALA_AUTOTEST_PHASE2
void balabala_network_observe(BalabalaNetwork *n, BalabalaNetworkObservation *out) {
    n->ops.lock(n->ops.context); *out = n->observation; n->ops.unlock(n->ops.context);
}
#endif
static uint32_t take(BalabalaNetwork *n) {
    n->ops.lock(n->ops.context);
    uint32_t events = n->events;
    n->events = 0;
    n->ops.unlock(n->ops.context);
    return events;
}
static void status(BalabalaNetwork *n, uint32_t value) {
    if (n->status != value) { n->status = value; ++n->revision; }
}
static void error(BalabalaNetwork *n, int value) {
    if (value && !n->error) n->error = value;
}
static void cleanup_fault(BalabalaNetwork *n, int result) {
    bool changed = !n->cleanup_error || n->cleanup_error != result || n->name[0] || n->pop[0];
    n->cleanup_error = result;
    error(n, result);
    n->fault = n->stopping = true;
    balabala_network_clear(n->name, sizeof(n->name));
    balabala_network_clear(n->pop, sizeof(n->pop));
    n->phase = 0;
    status(n, BALABALA_NETWORK_CLEANUP_FAILED);
    if (changed) ++n->revision;
}
static int release_owned(BalabalaNetwork *n) {
    int first = 0;
    if (n->saved && !n->success) {
        first = n->ops.restore(n->ops.context);
        error(n, first);
        if (first) n->fault = true;
    }
    int result = 0;
    if (!n->released) {
        result = n->ops.release(n->ops.context);
#ifdef BALABALA_AUTOTEST_PHASE2
        ++n->observation.releases; n->observation.release_result = result;
#endif
    }
    if (result) { cleanup_fault(n, result); return result; }
    n->released = true;
    n->active = n->started = n->stopping = n->saved = false;
    n->phase = 0;
    balabala_network_clear(n->name, sizeof(n->name));
    balabala_network_clear(n->pop, sizeof(n->pop));
    status(n, n->fault ? BALABALA_NETWORK_FAILED : n->terminal_status);
    ++n->revision;
    return first;
}

static void finish(BalabalaNetwork *n) {
    if (n->cleanup_error) return;
    if (n->manager) {
        int result = n->ops.manager_cleanup_error(n->ops.context);
        if (result) { cleanup_fault(n, result); return; }
        result = n->ops.manager_deinit(n->ops.context);
#ifdef BALABALA_AUTOTEST_PHASE2
        ++n->observation.deinits; n->observation.deinit_result = result;
#endif
        if (result) { cleanup_fault(n, result); return; }
        n->manager = false;
    }
    (void)release_owned(n);
}
int balabala_network_retry_cleanup(BalabalaNetwork *n) {
    if (!n->active || !n->manager || !n->cleanup_error) return -1;
    int result = n->ops.manager_deinit(n->ops.context);
#ifdef BALABALA_AUTOTEST_PHASE2
    ++n->observation.deinits; n->observation.deinit_result = result;
#endif
    if (result) { cleanup_fault(n, result); return result; }
    n->manager = false;
    n->cleanup_error = 0;
    return release_owned(n);
}
static void stop(BalabalaNetwork *n, uint32_t value) {
    if (n->stopping) return;
    n->terminal_status = value;
    n->phase = 0;
    n->stopping = true;
    balabala_network_clear(n->name, sizeof(n->name));
    balabala_network_clear(n->pop, sizeof(n->pop));
    status(n, BALABALA_NETWORK_CLOSING);
    if (!n->manager) { finish(n); return; }
    n->ops.manager_stop(n->ops.context);
#ifdef BALABALA_AUTOTEST_PHASE2
    ++n->observation.stops;
#endif
}
static void start(BalabalaNetwork *n, uint64_t now) {
    n->ops.lock(n->ops.context);
    ++n->generation;
    if (!n->generation) ++n->generation;
    n->events = 0;
#ifdef BALABALA_AUTOTEST_PHASE2
    n->observation = (BalabalaNetworkObservation){.generation=n->generation};
#endif
    n->ops.unlock(n->ops.context);
    n->active = true; n->success = false; n->error = 0;
    n->phase = 1;
    n->terminal_status = BALABALA_NETWORK_FAILED;
    n->deadline = now + BALABALA_NETWORK_CONNECT_TIMEOUT_MS;
    status(n, BALABALA_NETWORK_PREPARING);
    ++n->revision;
}
int balabala_network_advance(BalabalaNetwork *n, uint32_t generation, uint32_t phase, uint64_t now) {
    if (!n->active || n->fault || n->stopping || n->generation != generation ||
        n->phase != phase || (phase != 1 && phase != 2)) return -1;
    if (now >= n->deadline) { stop(n, BALABALA_NETWORK_TIMEOUT); return -1; }
    n->phase = 0; /* 每个阶段只提交一次，失败不能借旧ack重入。 */
    int result;
    if (phase == 1) {
        n->released = false;
        balabala_alloc_diag_checkpoint(BALABALA_ALLOC_PREPARE_BEFORE, n->generation, 0);
        result = n->ops.prepare(n->ops.context, n->generation);
        balabala_alloc_diag_checkpoint(BALABALA_ALLOC_PREPARE_AFTER, n->generation, result);
        if (result) goto failed;
        result = n->ops.backup(n->ops.context);
        if (result) goto failed;
        n->saved = true;
        result = n->ops.radio_start(n->ops.context);
        balabala_alloc_diag_checkpoint(BALABALA_ALLOC_RADIO_AFTER, n->generation, result);
        if (result) goto failed;
        result = n->ops.identity(n->ops.context, n->name, n->pop);
        if (result) goto failed;
        n->phase = 2;
        ++n->revision;
        return 0;
    }
    result = n->ops.manager_init(n->ops.context);
    if (result) goto failed;
    n->manager = true;
    balabala_alloc_diag_checkpoint(BALABALA_ALLOC_MANAGER_BEFORE, n->generation, 0);
    result = n->ops.manager_start(n->ops.context, n->name, n->pop);
#ifdef BALABALA_AUTOTEST_PHASE2
    ++n->observation.starts; n->observation.start_result = result;
#endif
    balabala_alloc_diag_checkpoint(BALABALA_ALLOC_MANAGER_AFTER, n->generation, result);
    if (result) goto failed;
    n->started = true;
    ++n->revision;
    return 0;
failed:
    error(n, result);
    n->terminal_status = BALABALA_NETWORK_FAILED;
    finish(n);
    balabala_alloc_diag_checkpoint(BALABALA_ALLOC_FAILED_FINISH_AFTER, n->generation, result);
    return result;
}
void balabala_network_step(BalabalaNetwork *n, uint32_t requests, uint64_t now) {
    /* 清理完成前拒绝重入；同批cancel/shutdown优先，不能先打开入口。 */
    if (requests & BALABALA_REQUEST_NETWORK_START) {
        if (!n->active && n->released && !n->fault && !(requests & (BALABALA_REQUEST_NETWORK_CANCEL | BALABALA_REQUEST_SHUTDOWN)))
            start(n, now);
        else {
            if (!n->active) n->status = BALABALA_NETWORK_FAILED;
            /* 拒绝也须回复当前所有权；同值FAILED不能吞掉本次START。 */
            ++n->revision;
        }
    }
    uint32_t events = take(n);
    if (!n->active || n->cleanup_error) return;
    /* 同一批事件中的成功先提交；进入停止态后的迟到成功仍拒绝。 */
    if ((events & BALABALA_NET_EVENT_SUCCESS) && !n->stopping) {
        n->success = true;
        n->terminal_status = BALABALA_NETWORK_SUCCESS;
        balabala_network_clear(n->name, sizeof(n->name));
        balabala_network_clear(n->pop, sizeof(n->pop));
        status(n, BALABALA_NETWORK_SUCCESS);
    }
    if (n->manager) {
        int result = n->ops.manager_cleanup_error(n->ops.context);
        if (result) { cleanup_fault(n, result); return; }
    }
    if (!n->stopping && !n->success) {
        if (events & BALABALA_NET_EVENT_RECV) status(n, BALABALA_NETWORK_CONNECTING);
        else if ((events & BALABALA_NET_EVENT_ADVERTISING) && n->status == BALABALA_NETWORK_PREPARING)
            status(n, BALABALA_NETWORK_WAITING);
    }
    if (requests & (BALABALA_REQUEST_NETWORK_CANCEL | BALABALA_REQUEST_SHUTDOWN))
        stop(n, n->success ? BALABALA_NETWORK_SUCCESS : BALABALA_NETWORK_CANCELLED);
    else if (!n->stopping && !n->success && (events & (BALABALA_NET_EVENT_FAIL | BALABALA_NET_EVENT_ADVERTISING_FAILED)))
        stop(n, BALABALA_NETWORK_FAILED);
    else if (!n->stopping && !n->success && now >= n->deadline)
        stop(n, BALABALA_NETWORK_TIMEOUT);
    /* GOT_IP后保留手机读取窗口，等官方查询触发stop或30秒autostop。 */
    if (events & BALABALA_NET_EVENT_END) {
        if (!n->stopping && !n->success) n->terminal_status = BALABALA_NETWORK_FAILED;
        finish(n);
    }
}
