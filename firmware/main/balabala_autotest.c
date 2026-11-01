#define _POSIX_C_SOURCE 200809L
#include "balabala_autotest.h"
#include "balabala_alloc_diag.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define RTC_MAGIC 0x42415431u
static uint32_t checksum(const BalabalaAutotestRtc *r) {
    return RTC_MAGIC ^ r->stage ^ r->id[0] ^ r->id[1] ^ r->slot ^
        (uint32_t)r->digest ^ (uint32_t)(r->digest >> 32) ^
        (uint32_t)r->generation ^ (uint32_t)(r->generation >> 32);
}
static void mark(BalabalaAutotestRtc *r, uint32_t stage) {
    r->magic = RTC_MAGIC; r->stage = stage; r->checksum = checksum(r);
}
static int isolated_root(char root[BALABALA_AUTOTEST_ROOT_BYTES], const char *mount,
                          BalabalaAutotestRtc *r, bool sw, uint32_t (*random_u32)(void *), void *ctx) {
    root[0] = 0;
    /* 软件重启只能恢复已武装的运行，空标记也不能新建第二轮。 */
    if (!r->magic && sw) return -1;
    if (r->magic) {
        if (r->magic != RTC_MAGIC || r->checksum != checksum(r) || r->stage != 1 || !sw) return -1;
        /* 先消耗机会，再执行任何SD IO；失败后重启也不能重新尝试。 */
        mark(r, 2);
        if (!mount) return -1;
        int n = snprintf(root, BALABALA_AUTOTEST_ROOT_BYTES, "%s/bala-diag-%08x%08x", mount,
                         (unsigned)r->id[0], (unsigned)r->id[1]);
        struct stat st;
        if (n < 0 || n >= BALABALA_AUTOTEST_ROOT_BYTES || stat(root, &st) || !S_ISDIR(st.st_mode)) {
            root[0] = 0; return -1;
        }
        return 1;
    }
    if (!mount) { mark(r, 3); return -1; }
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        r->id[0] = random_u32(ctx); r->id[1] = random_u32(ctx);
        int n = snprintf(root, BALABALA_AUTOTEST_ROOT_BYTES, "%s/bala-diag-%08x%08x", mount,
                         (unsigned)r->id[0], (unsigned)r->id[1]);
        if (n < 0 || n >= BALABALA_AUTOTEST_ROOT_BYTES) break;
        if (mkdir(root, 0775) == 0) { mark(r, 0); return 0; }
        if (errno != EEXIST) break;
    }
    root[0] = 0; mark(r, 3); return -1;
}
int balabala_autotest_start(char root[BALABALA_AUTOTEST_ROOT_BYTES], const char *mount,
                           BalabalaAutotestRtc *r, esp_reset_reason_t reason,
                           uint32_t (*random_u32)(void *), void *ctx) {
    root[0] = 0;
    switch (reason) {
    case ESP_RST_POWERON:
    case ESP_RST_USB:
        memset(r, 0, sizeof(*r));
        return isolated_root(root, mount, r, false, random_u32, ctx);
    case ESP_RST_SW:
#ifdef BALABALA_AUTOTEST_PHASE2
        return -1;
#else
        return isolated_root(root, mount, r, true, random_u32, ctx);
#endif
    default:
        return -1;
    }
}
bool balabala_autotest_input_idle(const BalabalaInput *input) {
    return input->adc[0].stable < 0 && input->adc[0].candidate < 0 &&
        input->adc[1].stable < 0 && input->adc[1].candidate < 0 &&
        input->power.stable == 1 && input->power.candidate == 1;
}
static void log_event(BalabalaAutotest *t, unsigned event, uint64_t a, uint64_t b) {
    if (t->ops.log) t->ops.log(t->ops.context, t->step, event, a, b);
}
static void fail(BalabalaAutotest *t, unsigned code, uint64_t a, uint64_t b) {
    if (t->stopped) return;
    log_event(t, code, a, b); t->stopped = true; mark(t->rtc, 3);
}
void balabala_autotest_init(BalabalaAutotest *t, BalabalaAutotestRtc *r,
                           const BalabalaAutotestOps *ops, uint64_t now) {
    memset(t, 0, sizeof(*t)); t->rtc = r; t->ops = *ops; t->since = now;
#ifdef BALABALA_AUTOTEST_PHASE2
    t->step = 50;
#else
    if (r->stage == 2) t->step = 41;
#endif
}
void balabala_autotest_user_input(BalabalaAutotest *t) { fail(t, 101, 0, 0); }
void balabala_autotest_saved(BalabalaAutotest *t, const BalabalaAutotestSnapshot *a,
                            const BalabalaAutotestSnapshot *b, int result, BalabalaStorageStatus status) {
    if (t->stopped) return;
    log_event(t, 10, (uint64_t)(uint32_t)result, status);
    log_event(t, 11, b->generation, b->slot);
    log_event(t, 12, a->dirty, b->dirty);
    if (result || status != BALABALA_STORAGE_SAVED || !a->dirty || b->dirty ||
        b->generation != a->generation + 1 || b->slot != 1 - a->slot) {
        fail(t, 102, b->generation, b->dirty); return;
    }
    t->saves++;
}
static bool idle(const BalabalaAutotestSnapshot *s, const BalabalaRuntime *r) {
    return !r->pending && !r->display_fault && !r->shutdown && !r->download &&
        !r->requests && s->revision == s->displayed && !s->rendering && !s->held;
}
#ifdef BALABALA_AUTOTEST_PHASE2
static bool alloc_stopped(BalabalaAutotest *t) {
    if (!balabala_alloc_diag_blocked()) return false;
    BalabalaAllocObservation o; balabala_alloc_diag_observe(&o);
    fail(t,113,o.latched,(uint32_t)o.register_result);
    return true;
}
#endif
static void send_key(BalabalaAutotest *t, const BalabalaAutotestSnapshot *s,
                     BalabalaKey key, unsigned next, uint64_t now, bool repeat) {
#ifdef BALABALA_AUTOTEST_PHASE2
    if (alloc_stopped(t)) return;
#endif
    t->before = *s; t->waiting = true; t->since = now;
    log_event(t, 1, key, s->revision);
#ifdef BALABALA_AUTOTEST_PHASE2
    if (alloc_stopped(t)) return;
#endif
    if (key == BALABALA_PARENT) {
        t->ops.emit(t->ops.context, BALABALA_FRONT3, true);
#ifdef BALABALA_AUTOTEST_PHASE2
        if (alloc_stopped(t)) return;
#endif
    }
    t->ops.emit(t->ops.context, key, true);
    if (repeat) {
#ifdef BALABALA_AUTOTEST_PHASE2
        if (alloc_stopped(t)) return;
#endif
        t->ops.emit(t->ops.context, key, true);
    }
#ifdef BALABALA_AUTOTEST_PHASE2
    if (alloc_stopped(t)) return;
#endif
    t->ops.emit(t->ops.context, key, false);
    if (key == BALABALA_PARENT) {
#ifdef BALABALA_AUTOTEST_PHASE2
        if (alloc_stopped(t)) return;
#endif
        t->ops.emit(t->ops.context, BALABALA_FRONT3, false);
    }
    t->step = next; t->dwell = 0;
}
#ifdef BALABALA_AUTOTEST_PHASE2
static bool network_clean(const BalabalaNetwork *n) {
    if (!n->released || n->active || n->manager || n->started || n->stopping || n->saved || n->cleanup_error || n->fault) return false;
    for (unsigned i=0; i<sizeof(n->name); ++i) if (n->name[i]) return false;
    for (unsigned i=0; i<sizeof(n->pop); ++i) if (n->pop[i]) return false;
    return true;
}
static void log_network_observation(BalabalaAutotest *t, const BalabalaNetworkObservation *o) {
    log_event(t,20,o->generation,o->starts); log_event(t,23,o->generation,(uint32_t)o->start_result);
    log_event(t,24,o->generation,o->stops); log_event(t,21,o->generation,o->ends);
    log_event(t,25,o->deinits,(uint32_t)o->deinit_result);
    log_event(t,26,o->releases,(uint32_t)o->release_result);
}
static void log_network(BalabalaAutotest *t, BalabalaNetwork *n) {
    BalabalaNetworkObservation o; balabala_network_observe(n,&o);
    log_network_observation(t,&o);
}
static void log_network_tail(BalabalaAutotest *t, BalabalaNetwork *n) {
    BalabalaNetworkObservation o; balabala_network_observe(n,&o);
    const BalabalaNetworkObservation *last=&t->tail_network;
    if (t->tail_network_valid && o.generation==last->generation && o.starts==last->starts &&
        o.start_result==last->start_result && o.stops==last->stops && o.ends==last->ends &&
        o.deinits==last->deinits && o.deinit_result==last->deinit_result &&
        o.releases==last->releases && o.release_result==last->release_result) return;
    /* 脚本停止不停止只读尾采；按字段比较，避免padding触发重复输出。 */
    log_network_observation(t,&o);
    t->tail_network=o; t->tail_network_valid=true;
}
static void sample(BalabalaAutotest *t, unsigned point) {
    uint32_t free_bytes, largest;
    t->ops.memory(t->ops.context, t->cycle*4+point, &free_bytes, &largest);
    /* 仅同一Home/selected=0/clean状态，预热后比较。 */
    if (point==3) {
        if (t->cycle && (free_bytes<t->memory_free || largest<t->memory_largest)) {
            fail(t,107,free_bytes,largest); return;
        }
        t->memory_free=free_bytes; t->memory_largest=largest;
    }
}
void balabala_autotest_before_display(BalabalaAutotest *t, BalabalaRuntime *r) {
    BalabalaNetwork *n=r->ops.network;
    if (!t->stopped && t->step==54 && n && n->started && n->generation!=t->sampled_generation) {
        t->sampled_generation=n->generation;
        sample(t,1); /* 真实start返回后、display调用前。 */
    }
}
#endif
#define REQUIRE(c) do { if (!(c)) { fail(t, 103, s.page, s.revision); return; } } while (0)
#define SEND(k,n) do { send_key(t, &s, (k), (n), now, false); return; } while (0)
void balabala_autotest_tick(BalabalaAutotest *t, BalabalaRuntime *r, BalabalaStorage *storage,
                            uint64_t now, bool input_idle) {
#ifdef BALABALA_AUTOTEST_PHASE2
    (void)storage;
#endif
#ifdef BALABALA_AUTOTEST_PHASE2
    if (balabala_alloc_diag_blocked()) {
        if (r->ops.network) log_network_tail(t,r->ops.network);
        (void)alloc_stopped(t); return;
    }
#endif
    if (t->stopped) return;
    if (now - t->since >= BALABALA_AUTOTEST_TIMEOUT_MS) { fail(t, 104, now - t->since, 0); return; }
    if (r->display_fault || r->shutdown || r->download || r->save_result) { fail(t, 105, r->display_fault, r->save_result); return; }
    BalabalaAutotestSnapshot s;
    if (balabala_app_autotest_snapshot(r->app, &s)) { fail(t,106,0,0); return; }
#ifdef BALABALA_AUTOTEST_PHASE2
    BalabalaNetwork *n=r->ops.network;
    if (!n || n->cleanup_error || n->fault || n->error) {
        if (n) log_network(t,n);
        fail(t,112,n ? n->cleanup_error : 0,n ? n->error : 0); return;
    }
#endif
    if (!idle(&s, r) || !input_idle) return;
    if (t->waiting) {
        REQUIRE(s.revision > t->before.revision);
        log_event(t, 2, t->before.revision, s.displayed);
        t->waiting = false;
    }
    switch (t->step) {
#ifdef BALABALA_AUTOTEST_PHASE2
    case 50:
        REQUIRE(s.page==0);
        if (s.dirty) return;
        REQUIRE(t->saves>0 && network_clean(n));
        t->memory_digest=s.digest; t->step=51; t->since=now; return;
    case 51:
        REQUIRE(s.page==0 && network_clean(n));
        SEND(BALABALA_PARENT,52);
    case 52:
        REQUIRE(s.page==5 && network_clean(n));
        if (s.selected!=3) SEND(BALABALA_DOWN,52);
        sample(t,0); t->network_generation=n->generation;
        SEND(BALABALA_FRONT4,54);
    case 54: {
        if (n->phase || n->status==BALABALA_NETWORK_PREPARING) return;
        BalabalaNetworkObservation o; balabala_network_observe(n,&o);
        log_network(t,n);
        REQUIRE(s.page==7 && n->status==BALABALA_NETWORK_WAITING && n->active && n->manager && n->started && n->saved &&
                !n->stopping && n->pop[0] && n->name[0] && n->generation!=t->network_generation &&
                o.generation==n->generation && o.starts==1 && !o.start_result && !o.ends);
        t->network_generation=n->generation;
        sample(t,2);
        SEND(t->cycle==2 ? BALABALA_FRONT3 : t->cycle==3 ? BALABALA_HOME : BALABALA_FRONT4,55);
    }
    case 55: {
        if (!network_clean(n)) return;
        REQUIRE(s.page==(t->cycle<2 ? 5u : 0u));
        BalabalaNetworkObservation o; balabala_network_observe(n,&o);
        log_network(t,n);
        REQUIRE(o.generation==t->network_generation && o.ends==1 && o.starts==1 && o.stops==1 &&
                o.deinits==1 && o.releases==1 && !o.start_result && !o.deinit_result && !o.release_result);
        if (s.page==0) { t->step=56; t->since=now; t->dwell=0; return; }
        SEND(BALABALA_HOME,56);
    }
    case 56: {
        BalabalaNetworkObservation o; balabala_network_observe(n,&o);
        REQUIRE(o.generation==t->network_generation && o.ends==1);
        REQUIRE(s.page==0 && s.selected==0 && !s.dirty && s.digest==t->memory_digest && network_clean(n));
        if (!t->dwell) t->dwell=now;
        if (now-t->dwell<2000) return;
        sample(t,3); if (alloc_stopped(t) || t->stopped) return;
        if (++t->cycle<4) { t->step=51; t->since=now; return; }
        log_event(t,8,t->cycle,t->network_generation); mark(t->rtc,3); t->stopped=true; return;
    }
#else
    case 0:
        REQUIRE(s.page == 0);
        if (s.dirty) return;
        REQUIRE(t->saves > 0); t->step = 1; t->since = now; return;
    case 1: case 7:
        REQUIRE(s.page == 0);
        if (s.selected != t->subject + 1) SEND(BALABALA_DOWN, t->step);
        SEND(BALABALA_FRONT4, t->step == 1 ? 2 : 8);
    case 2: REQUIRE(s.page == 1 && s.selected == 0); SEND(BALABALA_FRONT4, 3);
    case 3:
        REQUIRE(s.page == 2 && s.subject == t->subject && s.mode == 0 && s.count == 5 && s.scored == 0 && s.cursor == 0);
        SEND(BALABALA_FRONT4, 4);
    case 4: REQUIRE(s.flipped && s.scored == 0 && s.progress == t->before.progress); SEND(BALABALA_DOWN, 5);
    case 5: REQUIRE(s.cursor == 1 && !s.flipped && s.scored == 0 && s.progress == t->before.progress); SEND(BALABALA_UP, 6);
    case 6: REQUIRE(s.cursor == 0 && !s.flipped && s.scored == 0 && s.progress == t->before.progress); SEND(BALABALA_HOME, 7);
    case 8: REQUIRE(s.page == 1); SEND(BALABALA_DOWN, 9);
    case 9: REQUIRE(s.page == 1 && s.selected == 1); SEND(BALABALA_FRONT4, 10);
    case 10:
        REQUIRE(s.page == 2 && s.subject == t->subject && s.mode == 1 && s.cursor == 0 && s.scored == 0 && s.correct < 4);
        send_key(t, &s, (BalabalaKey)s.correct, 11, now, true); return;
    case 11:
        REQUIRE(s.page == 2 && s.cursor == 0 && s.scored == 1 && s.answers[0] == 1 &&
                s.mutation == t->before.mutation + 1 && s.revision == t->before.revision + 1);
        if (!t->dwell) t->dwell = now;
        if (now - t->dwell < 2000) return;
        log_event(t, 3, now - t->dwell, s.revision); SEND(BALABALA_DOWN, 12);
    case 12: REQUIRE(s.cursor == 1 && s.scored == 1); SEND(BALABALA_UP, 13);
    case 13: REQUIRE(s.cursor == 0 && s.scored == 1); SEND(BALABALA_FRONT1, 14);
    case 14:
        REQUIRE(s.cursor == 0 && s.scored == 1 && s.answers[0] == 1 && s.progress == t->before.progress);
        SEND(BALABALA_DOWN, 15);
    case 15:
        REQUIRE(s.cursor == 1 && s.scored == 1 && s.answers[1] == -1 && s.correct < 4);
        SEND((BalabalaKey)((s.correct + 1 + t->subject % 3) % 4), 16);
    case 16: REQUIRE(s.scored == 2 && s.answers[1] == 0); SEND(BALABALA_DOWN, 17);
    case 17:
        REQUIRE(s.cursor == 2 && s.scored == 2 && s.answers[2] == -1);
        SEND((BalabalaKey)(s.correct == 2 ? 3 : 2), 18);
    case 18: REQUIRE(s.scored == 3 && s.answers[2] == 0); SEND(BALABALA_DOWN, 19);
    case 19: REQUIRE(s.cursor == 3 && s.scored == 3 && s.answers[3] == -1); SEND(BALABALA_DOWN, 20);
    case 20:
        REQUIRE(s.cursor == 4 && s.scored == 3 && s.answers[3] == -1 && s.correct < 4);
        SEND((BalabalaKey)s.correct, 21);
    case 21: REQUIRE(s.cursor == 4 && s.scored == 4 && s.answers[4] == 1); SEND(BALABALA_DOWN, 22);
    case 22: REQUIRE(s.page == 3 && s.scored == 4); SEND(BALABALA_HOME, 23);
    case 23: REQUIRE(s.page == 0 && s.selected == 0); SEND(BALABALA_FRONT4, 24);
    case 24: REQUIRE(s.page == 2 && s.cursor == 4 && s.scored == 4); SEND(BALABALA_HOME, 25);
    case 25:
        REQUIRE(s.page == 0);
        if (s.dirty) return;
        log_event(t, 4, t->subject, s.generation);
        if (++t->subject < 4) { t->step = 1; t->since = now; return; }
        t->memory_digest = s.digest; t->step = 26; t->since = now; return;
    /* 相同持久化状态、Home/selected=0/clean；循环只进继续学习再回Home，进度不增长。 */
    case 26:
        REQUIRE(s.page == 0 && !s.dirty && s.digest == t->memory_digest);
        SEND(BALABALA_FRONT4, 27);
    case 27: REQUIRE(s.page == 2 && !s.dirty); SEND(BALABALA_HOME, 28);
    case 28: {
        REQUIRE(s.page == 0 && s.selected == 0 && !s.dirty && s.digest == t->memory_digest);
        uint32_t free_bytes = 0, largest = 0;
        t->ops.memory(t->ops.context, t->cycle, &free_bytes, &largest);
        if (t->cycle && (free_bytes < t->memory_free || largest < t->memory_largest)) {
            fail(t,107,free_bytes,largest); return;
        }
        t->memory_free = free_bytes; t->memory_largest = largest;
        if (++t->cycle < 4) { t->step = 26; t->since = now; return; }
        t->step = 29; t->since = now; return;
    }
    case 29: {
        REQUIRE(!s.dirty && t->saves >= 2);
        int result = balabala_storage_restore(storage, r->app);
        BalabalaAutotestSnapshot restored;
        REQUIRE(balabala_app_autotest_snapshot(r->app, &restored) == 0);
        REQUIRE(result == 0 && storage->report.status == BALABALA_STORAGE_RESTORED &&
                s.digest == restored.digest && s.generation == restored.generation && s.slot == restored.slot);
        log_event(t,5,restored.generation,restored.slot);
        t->step = 30; t->since = now; return;
    }
    case 30:
        if (s.dirty) return;
        t->rtc->digest = s.digest; t->rtc->generation = s.generation; t->rtc->slot = s.slot;
        mark(t->rtc, 1); log_event(t,6,s.generation,s.slot);
        t->stopped = true; t->ops.restart(t->ops.context); return;
    case 41:
        if (s.dirty) return;
        REQUIRE(t->saves > 0); log_event(t,8,s.generation,s.slot);
        mark(t->rtc,3); t->stopped = true; return;
#endif
    default: fail(t,108,t->step,0);
    }
}
