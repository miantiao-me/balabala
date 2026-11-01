#include "balabala_runtime.h"
#include <string.h>

int balabala_runtime_init(BalabalaRuntime *r, BalabalaApp *app, uint8_t *frame,
                          uint16_t *stripe, const BalabalaRuntimeOps *ops, bool available) {
    if (!r || !app || !frame || !stripe || !ops || !ops->now_ms || !ops->poll ||
        !ops->display || !ops->save || !ops->display_sleep || !ops->power_released || !ops->sleep)
        return -1;
    memset(r, 0, sizeof(*r));
    r->app = app; r->frame = frame; r->stripe = stripe; r->ops = *ops;
    r->last_activity_ms = ops->now_ms(ops->context);
    r->save_at = r->last_activity_ms + BALABALA_SAVE_INTERVAL_MS;
    balabala_app_storage_status(app, available ? BALABALA_SAVE_AVAILABLE : BALABALA_SAVE_UNAVAILABLE);
    return 0;
}
void balabala_runtime_activity(BalabalaRuntime *r, BalabalaKey key, bool pressed) {
    r->last_activity_ms = r->ops.now_ms(r->ops.context);
    uint16_t bit = (uint16_t)(1u << key);
    if (pressed) r->held_keys |= bit;
    else r->held_keys &= (uint16_t)~bit;
}
void balabala_runtime_service(BalabalaRuntime *r) {
    r->ops.poll(r->ops.context, r->app);
    uint32_t requests = balabala_app_platform_requests(r->app);
    if (requests & BALABALA_REQUEST_SHUTDOWN) {
        r->shutdown = true;
        r->automatic_shutdown = false;
    }
    if (requests & (BALABALA_REQUEST_DOWNLOAD_CANCEL | BALABALA_REQUEST_SHUTDOWN)) r->download = 0;
    r->requests |= requests;
}
static void save(BalabalaRuntime *r) {
    r->save_result = r->ops.save(r->ops.context, r->app);
    balabala_app_storage_status(r->app, r->save_result == 0 ? BALABALA_SAVE_AVAILABLE :
                                r->save_result == 2 ? BALABALA_SAVE_UNAVAILABLE : BALABALA_SAVE_FAILED);
    r->save_at = r->ops.now_ms(r->ops.context) +
                (r->save_result == 0 ? BALABALA_SAVE_INTERVAL_MS : BALABALA_SAVE_RETRY_MS);
}
static void refresh(BalabalaRuntime *r) {
    r->attempts++;
    r->display_result = r->ops.display(r->ops.context, r->frame, r->partial);
    if (r->display_result == 0) {
        r->display_result = balabala_app_display_complete(r->app, r->revision);
        if (r->display_result == 0) { r->pending = false; r->attempts = 0; return; }
    }
    if (r->attempts >= BALABALA_DISPLAY_MAX_ATTEMPTS) r->display_fault = true;
    r->retry_at = r->ops.now_ms(r->ops.context) + BALABALA_DISPLAY_RETRY_MS;
}
static void render(BalabalaRuntime *r) {
    int result = balabala_app_render(r->app, r->frame, BALABALA_FRAMEBUFFER_BYTES,
                                     r->stripe, BALABALA_STRIPE_PIXELS, &r->revision);
    if (result == 0) {
        r->partial = balabala_app_refresh_partial(r->app) != 0;
        r->pending = true; r->attempts = 0;
    }
    else if (result < 0) { r->display_result = result; r->display_fault = true; }
}
static bool network_clean(BalabalaRuntime *r);
static bool external_power(BalabalaRuntime *r) {
    return r->ops.external_power && r->ops.external_power(r->ops.context);
}
static bool cancel_powered_sleep(BalabalaRuntime *r) {
    if (!r->automatic_shutdown || !external_power(r)) return false;
    /* 接电可能发生在同步全刷/关屏内部；撤销易失休眠帧，恢复可操作的首页。 */
    if (balabala_app_sleep_cancel(r->app) > 0) {
        r->pending = false;
        r->attempts = 0;
    }
    r->shutdown = r->automatic_shutdown = r->shutdown_started = r->shutdown_prepared = false;
    r->releasing = r->sleep_attempted = false;
    r->shutdown_revision = 0;
    r->last_activity_ms = r->ops.now_ms(r->ops.context);
    return true;
}
static void shutdown_step(BalabalaRuntime *r) {
    if (cancel_powered_sleep(r)) return;
    if (!r->shutdown_prepared) {
        if (!r->shutdown_started) {
            save(r);
            if (r->automatic_shutdown && (r->save_result ||
                (balabala_app_status(r->app) & BALABALA_SAVE_DIRTY))) {
                /* 自动关机不能丢弃未保存进度；保留页面及错误，五分钟后才再尝试。 */
                r->shutdown = false;
                r->automatic_shutdown = false;
                r->last_activity_ms = r->ops.now_ms(r->ops.context);
                return;
            }
            r->shutdown_started = true;
        }
        if (cancel_powered_sleep(r)) return;
        /* 先确认已有帧，再提交休眠帧；沿用三次有界重试，避免瞬时错误直接关屏。 */
        if (!r->display_fault && r->pending) {
            if (r->attempts == 0 || r->ops.now_ms(r->ops.context) >= r->retry_at) refresh(r);
            if (r->pending && !r->display_fault) return;
        }
        if (!r->display_fault && !r->shutdown_revision) {
            if (r->save_result == 0 && network_clean(r))
                r->shutdown_revision = balabala_app_sleep_ready(r->app);
            render(r);
            if (!r->shutdown_revision) r->shutdown_revision = r->revision;
            if (r->pending) { r->partial = false; refresh(r); }
        }
        if (!r->display_fault && (r->pending || r->revision != r->shutdown_revision ||
            (balabala_app_status(r->app) & BALABALA_DISPLAY_PENDING))) return;
        if (cancel_powered_sleep(r)) return;
        r->sleep_result = r->ops.display_sleep(r->ops.context);
        if (cancel_powered_sleep(r)) return;
        r->shutdown_prepared = true;
        r->releasing = false;
    }
    if (r->sleep_attempted) return;
    uint64_t now = r->ops.now_ms(r->ops.context);
    if (!r->ops.power_released(r->ops.context)) { r->releasing = false; return; }
    if (!r->releasing) { r->released_since = now; r->releasing = true; }
    if (now - r->released_since >= BALABALA_SLEEP_RELEASE_MS) {
        if (cancel_powered_sleep(r)) return;
        r->sleep_attempted = true;
        r->sleep_result = r->ops.sleep(r->ops.context);
    }
}
static void download_fail(BalabalaRuntime *r, unsigned reason) {
    r->download = 0;
    r->download_result = (int)reason;
    (void)balabala_app_download_failed(r->app, reason);
}
static bool network_clean(BalabalaRuntime *r) {
    BalabalaNetwork *n = r->ops.network;
    return !n || (n->released && !n->active && !n->manager && !n->cleanup_error && !n->fault);
}
static void automatic_sleep(BalabalaRuntime *r, uint32_t requests) {
    uint64_t now = r->ops.now_ms(r->ops.context);
    uint32_t status = balabala_app_status(r->app);
    if (requests || r->shutdown || r->download || r->download_blocked || r->pending ||
        r->display_fault || r->held_keys || external_power(r) || !network_clean(r) ||
        (status & (BALABALA_NEEDS_REFRESH | BALABALA_DISPLAY_PENDING |
                   BALABALA_DOWNLOAD_ACTIVE | BALABALA_NETWORK_OWNED)) ||
        !r->ops.input_idle || !r->ops.input_idle(r->ops.context)) {
        r->last_activity_ms = now;
        return;
    }
    if (now - r->last_activity_ms >= BALABALA_AUTO_SLEEP_MS) {
        r->shutdown = true;
        r->automatic_shutdown = true;
    }
}
static void download_start(BalabalaRuntime *r, uint32_t *requests) {
    if (!(*requests & BALABALA_REQUEST_DOWNLOAD) || r->shutdown || r->download ||
        !(balabala_app_status(r->app) & BALABALA_DOWNLOAD_ACTIVE)) return;
    BalabalaNetwork *n = r->ops.network;
    if (n && (n->active || !n->released)) {
        r->download_result = BALABALA_DOWNLOAD_NETWORK_FAILED;
        return; /* 平台仍拥有无线资源时保留物理帧，不能以错误页触发AOT。 */
    }
    if (r->download_blocked) { download_fail(r, BALABALA_DOWNLOAD_RETURNED); return; }
    if (r->display_fault) { download_fail(r, BALABALA_DOWNLOAD_DISPLAY_FAILED); return; }
    int result = r->ops.download_check && r->ops.enter_download ?
                 r->ops.download_check(r->ops.context) : BALABALA_DOWNLOAD_UNSUPPORTED;
    if (result) {
        download_fail(r, result >= 1 && result <= 6 ? result : BALABALA_DOWNLOAD_UNSUPPORTED);
        return;
    }
    save(r);
    if (r->save_result) {
        download_fail(r, r->save_result == 2 ? BALABALA_DOWNLOAD_NO_STORAGE : BALABALA_DOWNLOAD_SAVE_FAILED);
        return;
    }
    if (balabala_app_status(r->app) & BALABALA_SAVE_DIRTY) {
        download_fail(r, BALABALA_DOWNLOAD_DIRTY); return;
    }
    r->download = 1;
    /* 每个事务只发送一次；后续正常step等待END/deinit/依赖释放。 */
    *requests |= BALABALA_REQUEST_NETWORK_CANCEL;
}
static void download_step(BalabalaRuntime *r) {
    if (!r->download) return;
    unsigned status = balabala_app_status(r->app);
    if (r->shutdown || !(status & BALABALA_DOWNLOAD_ACTIVE)) { r->download = 0; return; }
    if (r->display_fault) { download_fail(r, BALABALA_DOWNLOAD_DISPLAY_FAILED); return; }
    BalabalaNetwork *n = r->ops.network;
    if (n && (n->cleanup_error || n->fault)) {
        download_fail(r, BALABALA_DOWNLOAD_NETWORK_FAILED); return;
    }
    if (status & BALABALA_SAVE_DIRTY) { download_fail(r, BALABALA_DOWNLOAD_DIRTY); return; }
    if (r->download == 1 && network_clean(r)) {
        r->download_revision = balabala_app_download_ready(r->app);
        if (!r->download_revision) { r->download = 0; return; }
        r->download = 2;
    }
}
static void download_commit(BalabalaRuntime *r) {
    if (r->download != 2 || r->shutdown || r->pending || r->display_fault ||
        r->revision != r->download_revision || !network_clean(r)) return;
    unsigned status = balabala_app_status(r->app);
    if (!(status & BALABALA_DOWNLOAD_PROMPT) ||
        (status & (BALABALA_NEEDS_REFRESH | BALABALA_DISPLAY_PENDING | BALABALA_SAVE_DIRTY))) return;
    int result = r->ops.display_sleep(r->ops.context);
    /* display_sleep内部service可以撤销事务；返回后再采样并复核所有门槛。 */
    balabala_runtime_service(r);
    if (!r->download || r->shutdown) return;
    if (result) { download_fail(r, BALABALA_DOWNLOAD_SLEEP_FAILED); return; }
    status = balabala_app_status(r->app);
    if (!(status & BALABALA_DOWNLOAD_PROMPT) ||
        (status & (BALABALA_NEEDS_REFRESH | BALABALA_DISPLAY_PENDING | BALABALA_SAVE_DIRTY)) || !network_clean(r)) {
        download_fail(r, BALABALA_DOWNLOAD_DIRTY); return;
    }
    r->download = 0;
    r->download_blocked = true;
    r->ops.enter_download(r->ops.context);
    download_fail(r, BALABALA_DOWNLOAD_RETURNED);
}
static void network_sync(BalabalaRuntime *r) {
    BalabalaNetwork *n = r->ops.network;
    if (!n || r->network_revision == n->revision) return;
    r->network_revision = n->revision;
    if (n->fault && r->shutdown) {
        r->shutdown = false;
        r->shutdown_prepared = false;
        r->requests &= ~BALABALA_REQUEST_SHUTDOWN;
        (void)balabala_app_network_shutdown_blocked(r->app);
    }
    (void)balabala_app_network_info(r->app, (const uint8_t *)n->name, n->name[0] ? 11 : 0,
                                   (const uint8_t *)n->pop, n->pop[0] ? 6 : 0);
    r->network_frame_revision = balabala_app_network_phase(r->app, n->generation, n->status,
                                n->phase, (n->active ? 1u : 0u) | (n->released ? 2u : 0u) | (n->fault ? 4u : 0u));
    r->network_generation = n->generation;
    r->network_phase = n->phase;
}
static void network_commit(BalabalaRuntime *r) {
    BalabalaNetwork *n = r->ops.network;
    if (!n || !n->active || !n->phase) return;
    /* 显示过程中只收输入；真实ack后重新采样，在安全点处理取消/超时。 */
    balabala_runtime_service(r);
    uint32_t requests = r->requests;
    r->requests = 0;
    if (r->display_fault) requests |= BALABALA_REQUEST_NETWORK_CANCEL;
    balabala_network_step(n, requests, r->ops.now_ms(r->ops.context));
    network_sync(r);
    if (r->shutdown || r->display_fault || r->pending || requests ||
        r->network_generation != n->generation || r->network_phase != n->phase ||
        r->revision != r->network_frame_revision ||
        !balabala_app_network_ready(r->app, n->generation, n->phase, r->network_frame_revision)) return;
    (void)balabala_network_advance(n, n->generation, n->phase, r->ops.now_ms(r->ops.context));
    network_sync(r);
}
void balabala_runtime_step(BalabalaRuntime *r) {
    balabala_runtime_service(r);
    uint32_t requests = r->requests;
    r->requests = 0;
    uint64_t started_at = r->ops.now_ms(r->ops.context);
    if ((!r->ops.network || (r->ops.network->released && !r->ops.network->active && !r->ops.network->fault)) &&
        (requests & BALABALA_REQUEST_NETWORK_START) &&
        !(requests & (BALABALA_REQUEST_NETWORK_CANCEL | BALABALA_REQUEST_SHUTDOWN))) {
        if (balabala_app_status(r->app) & BALABALA_SAVE_DIRTY) save(r);
        if (balabala_app_status(r->app) & BALABALA_SAVE_DIRTY) {
            requests &= ~BALABALA_REQUEST_NETWORK_START;
            (void)balabala_app_network_save_failed(r->app);
        }
    }
    download_start(r, &requests);
    if (r->ops.network) {
        BalabalaNetwork *n = r->ops.network;
        balabala_network_step(n, requests, started_at);
        network_sync(r);
        /* 故障后的再次关机也不可绕过资源门禁或保留待重放意图。 */
        if (r->shutdown && n->fault && !n->released) {
            r->shutdown = false;
            (void)balabala_app_network_shutdown_blocked(r->app);
        }
    } else if (requests & BALABALA_REQUEST_NETWORK_START) {
        (void)balabala_app_network_phase(r->app, 0, BALABALA_NETWORK_FAILED, 0, 2);
    }
    automatic_sleep(r, requests);
    bool owned = (balabala_app_status(r->app) & BALABALA_NETWORK_OWNED) != 0;
    if (r->shutdown && (!r->ops.network || (r->ops.network->released && !r->ops.network->active))) {
        shutdown_step(r);
        return;
    }
    if (!owned && r->ops.network && (r->ops.network->active || !r->ops.network->released)) return;
    if (!owned) download_step(r);
    uint64_t now = r->ops.now_ms(r->ops.context);
    if (!r->display_fault) {
        if (!r->pending && (balabala_app_status(r->app) & BALABALA_NEEDS_REFRESH)) render(r);
        if (r->pending && (r->attempts == 0 || now >= r->retry_at)) refresh(r);
    }
    bool identity_render_failed = r->display_fault && !r->pending && r->network_phase == 2;
    network_commit(r);
    /* 第二帧mount失败不写帧；已有首帧基底允许固定错误标记再走真实刷新。 */
    if (identity_render_failed) r->display_fault = false;
    if (!(balabala_app_status(r->app) & BALABALA_NETWORK_OWNED)) {
        download_step(r);
        download_commit(r);
        if (!r->shutdown && !r->download && (balabala_app_status(r->app) & BALABALA_SAVE_DIRTY) &&
            (!r->ops.network || r->ops.network->released) && r->ops.now_ms(r->ops.context) >= r->save_at) save(r);
    }
}
