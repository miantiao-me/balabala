#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "balabala_network.h"

enum { PREPARE=1, BACKUP, RADIO, IDENTITY, INIT, START, STOP, DEINIT, RESTORE, RELEASE };
typedef struct {
    BalabalaNetwork n;
    pthread_mutex_t mutex;
    unsigned calls[11], order[64], count, fail;
    char persisted[16], old[16];
    bool resources, advertising;
    unsigned queries, query_failure_at;
} Fake;
static int call(Fake *f, unsigned step) {
    ++f->calls[step]; assert(f->count < 64); f->order[f->count++] = step;
    return f->fail == step ? -(int)step : 0;
}
static int prepare(void *ctx, uint32_t generation) {
    Fake *f=ctx; assert(generation); f->resources=true; return call(f,PREPARE);
}
static int backup(void *ctx) {
    Fake *f=ctx; int result=call(f,BACKUP);
    if(!result) memcpy(f->old,f->persisted,sizeof(f->old));
    return result;
}
static int radio(void *ctx) { return call(ctx,RADIO); }
static int identity(void *ctx, char name[12], char pop[17]) {
    Fake *f=ctx; assert(f->calls[RADIO]);
    memcpy(name,"PROV_A1B2C3",12); memcpy(pop,"012345",7); return call(f,IDENTITY);
}
static int init_manager(void *ctx) { return call(ctx,INIT); }
static int start_manager(void *ctx, const char *name, const char *pop) {
    Fake *f=ctx; assert(strlen(name)==11 && strlen(pop)==6);
    int result=call(f,START); f->advertising=!result; return result;
}
static void stop_manager(void *ctx) { Fake *f=ctx; (void)call(f,STOP); }
static int deinit(void *ctx) { Fake *f=ctx; int r=call(f,DEINIT); if(!r) f->advertising=false; return r; }
static int cleanup_error(void *ctx) { Fake *f=ctx; ++f->queries; return f->query_failure_at && f->queries>=f->query_failure_at ? -91 : 0; }
static int restore(void *ctx) {
    Fake *f=ctx; int result=call(f,RESTORE);
    if(!result) memcpy(f->persisted,f->old,sizeof(f->old));
    return result;
}
static int release(void *ctx) {
    Fake *f=ctx; int r=call(f,RELEASE); if(!r) { f->resources=false; memset(f->old,0,sizeof(f->old)); } return r;
}
static void lock(void *ctx) { assert(pthread_mutex_lock(&((Fake *)ctx)->mutex)==0); }
static void unlock(void *ctx) { assert(pthread_mutex_unlock(&((Fake *)ctx)->mutex)==0); }
static void create(Fake *f) {
    memset(f,0,sizeof(*f)); assert(pthread_mutex_init(&f->mutex,NULL)==0);
    memcpy(f->persisted,"old-config",11);
    BalabalaNetworkOps ops={prepare,backup,radio,identity,init_manager,start_manager,stop_manager,
                            deinit,cleanup_error,restore,release,lock,unlock,f};
    balabala_network_init(&f->n,&ops);
}
static void drop(Fake *f) { assert(!f->n.active || f->n.fault); assert(pthread_mutex_destroy(&f->mutex)==0); }
static void step(Fake *f, uint32_t req, uint64_t now) { balabala_network_step(&f->n,req,now); }
static void publish(Fake *f, uint32_t bits) { balabala_network_publish(&f->n,f->n.generation,bits); }
static void end(Fake *f) { publish(f,BALABALA_NET_EVENT_END); step(f,0,1000); }
static void advance(Fake *f, uint64_t now) {
    if(f->n.phase==1) (void)balabala_network_advance(&f->n,f->n.generation,1,now);
    if(f->n.phase==2) (void)balabala_network_advance(&f->n,f->n.generation,2,now);
}
static void start(Fake *f) {
    step(f,BALABALA_REQUEST_NETWORK_START,100);
    assert(f->n.phase==1 && f->n.released);
    advance(f,100);
    assert(f->n.active && f->advertising && f->n.status==BALABALA_NETWORK_PREPARING);
    publish(f,BALABALA_NET_EVENT_ADVERTISING); step(f,0,100);
    assert(f->n.status==BALABALA_NETWORK_WAITING);
    assert(f->n.deadline==300100);
}
static void applied(Fake *f) { memcpy(f->persisted,"new-config",11); publish(f,BALABALA_NET_EVENT_RECV); }
static void test_success_keeps_phone_query_window_and_new_config(void) {
    Fake f; create(&f); start(&f); applied(&f);
    publish(&f,BALABALA_NET_EVENT_SUCCESS); step(&f,0,200);
    assert(f.n.status==BALABALA_NETWORK_SUCCESS && f.n.active && f.advertising);
    assert(!f.calls[STOP] && !f.calls[DEINIT] && !f.calls[RELEASE]);
    /* 即使会话deadline已到，成功仍由官方查询/30秒autostop结束。 */
    step(&f,0,300200); assert(!f.calls[STOP]); end(&f);
    assert(!f.n.active && !f.advertising && !f.resources && !f.calls[RESTORE]);
    assert(strcmp(f.persisted,"new-config")==0 && !f.n.pop[0] && !f.n.name[0]); drop(&f);
}
static void test_wrong_password_or_ap_failure_rolls_back(void) {
    for(unsigned variant=0;variant<2;++variant) {
        Fake f; create(&f); start(&f); applied(&f);
        publish(&f,BALABALA_NET_EVENT_FAIL); step(&f,0,300);
        assert(f.n.stopping && f.n.status==BALABALA_NETWORK_CLOSING && f.n.terminal_status==BALABALA_NETWORK_FAILED && f.calls[STOP]==1);
        assert(!f.calls[DEINIT] && strcmp(f.persisted,"new-config")==0);
        end(&f); assert(f.calls[DEINIT]==1 && f.calls[RESTORE]==1);
        assert(strcmp(f.persisted,"old-config")==0 && !f.resources); drop(&f);
    }
}
static void test_cancel_before_and_after_apply_and_late_success(void) {
    for(unsigned applied_config=0;applied_config<2;++applied_config) {
        Fake f; create(&f); start(&f); if(applied_config) applied(&f);
        step(&f,BALABALA_REQUEST_NETWORK_CANCEL,200);
        publish(&f,BALABALA_NET_EVENT_SUCCESS); step(&f,0,201);
        assert(!f.n.success && f.n.status==BALABALA_NETWORK_CLOSING && f.n.terminal_status==BALABALA_NETWORK_CANCELLED);
        end(&f); assert(strcmp(f.persisted,"old-config")==0 && f.calls[RESTORE]==1); drop(&f);
    }
}
static void test_deadline_is_300_seconds_and_terminal_is_sticky(void) {
    Fake f; create(&f); start(&f); applied(&f);
    step(&f,0,300099); assert(!f.n.stopping);
    step(&f,0,300100); assert(f.n.stopping && f.n.status==BALABALA_NETWORK_CLOSING && f.n.terminal_status==BALABALA_NETWORK_TIMEOUT);
    publish(&f,BALABALA_NET_EVENT_SUCCESS); step(&f,0,300101);
    assert(!f.n.success && f.n.status==BALABALA_NETWORK_CLOSING && f.n.terminal_status==BALABALA_NETWORK_TIMEOUT);
    for(unsigned i=0;i<100;++i) step(&f,0,300200+i);
    assert(f.calls[STOP]==1); end(&f); assert(strcmp(f.persisted,"old-config")==0); drop(&f);
}
static void test_success_at_deadline_preserves_phone_window_and_cancel(void) {
    const uint64_t received[] = {299100, 300100};
    for(unsigned i=0;i<2;++i) {
        Fake f; create(&f); start(&f); applied(&f);
        publish(&f,BALABALA_NET_EVENT_SUCCESS); step(&f,0,received[i]);
        assert(f.n.success && !f.n.stopping && !f.calls[STOP]);
        uint32_t revision=f.n.revision;
        publish(&f,BALABALA_NET_EVENT_SUCCESS); step(&f,0,400000);
        assert(f.n.revision==revision && !f.calls[STOP] && f.n.deadline==300100);
        if(i==0) { /* 手机成功查询可提前产生END，无需平台再stop。 */
            end(&f); assert(!f.calls[STOP]);
        } else {
            step(&f,BALABALA_REQUEST_NETWORK_CANCEL,400001);
            assert(f.calls[STOP]==1 && f.n.status==BALABALA_NETWORK_CLOSING && f.n.terminal_status==BALABALA_NETWORK_SUCCESS);
            end(&f);
        }
        assert(strcmp(f.persisted,"new-config")==0 && !f.calls[RESTORE]); drop(&f);
    }
}
static void test_every_start_stage_failure_releases_and_retries(void) {
    for(unsigned failure=PREPARE;failure<=START;++failure) {
        Fake f; create(&f); f.fail=failure; step(&f,BALABALA_REQUEST_NETWORK_START,0); advance(&f,0);
        assert(!f.n.active && !f.n.fault && !f.resources && !f.advertising);
        assert(f.n.status==BALABALA_NETWORK_FAILED && f.n.error==-(int)failure);
        assert(f.calls[RELEASE]==1 && !f.n.pop[0]);
        assert(f.calls[RESTORE]==(failure>BACKUP ? 1u : 0u));
        assert(f.calls[DEINIT]==(failure==START ? 1u : 0u));
        f.fail=0; start(&f); step(&f,BALABALA_REQUEST_NETWORK_CANCEL,200); end(&f); drop(&f);
    }
}
static void test_reopen_waits_for_end_and_ignores_old_generation(void) {
    Fake f; create(&f); start(&f); uint32_t old=f.n.generation;
    step(&f,BALABALA_REQUEST_NETWORK_CANCEL,200);
    step(&f,BALABALA_REQUEST_NETWORK_START,201); assert(f.calls[START]==1);
    end(&f); start(&f); assert(f.n.generation!=old && f.calls[START]==2);
    balabala_network_publish(&f.n,old,BALABALA_NET_EVENT_SUCCESS|BALABALA_NET_EVENT_FAIL|BALABALA_NET_EVENT_END);
    step(&f,0,200); assert(f.n.active && !f.n.success && f.n.status==BALABALA_NETWORK_WAITING);
    step(&f,BALABALA_REQUEST_NETWORK_CANCEL,201); end(&f); drop(&f);
}
static void *storm(void *ctx) {
    Fake *f=ctx;
    for(unsigned i=0;i<20000;++i) publish(f,BALABALA_NET_EVENT_RECV);
    publish(f,BALABALA_NET_EVENT_END); return NULL;
}
static void test_bounded_threadsafe_channel_cannot_drop_end(void) {
    Fake f; create(&f); start(&f); step(&f,BALABALA_REQUEST_NETWORK_CANCEL,200);
    pthread_t threads[4];
    for(unsigned i=0;i<4;++i) assert(pthread_create(&threads[i],NULL,storm,&f)==0);
    for(unsigned i=0;i<4;++i) assert(pthread_join(threads[i],NULL)==0);
    assert(f.n.events==(BALABALA_NET_EVENT_RECV|BALABALA_NET_EVENT_END));
    step(&f,0,201); assert(!f.n.active && f.calls[DEINIT]==1); drop(&f);
}
static void test_shutdown_closes_entry_before_cleanup_and_preserves_success(void) {
    for(unsigned success=0;success<2;++success) {
        Fake f; create(&f); start(&f); applied(&f);
        if(success) { publish(&f,BALABALA_NET_EVENT_SUCCESS); step(&f,0,200); }
        step(&f,BALABALA_REQUEST_SHUTDOWN,201);
        assert(f.calls[STOP]==1 && !f.calls[RELEASE]); end(&f);
        assert(!f.n.active && !f.resources && !f.advertising);
        assert(strcmp(f.persisted,success?"new-config":"old-config")==0); drop(&f);
    }
}
static void test_backup_failure_never_uses_unsaved_config(void) {
    Fake f; create(&f); f.fail=BACKUP; step(&f,BALABALA_REQUEST_NETWORK_START,0); advance(&f,0);
    assert(!f.calls[RADIO] && !f.calls[START] && !f.calls[RESTORE]);
    assert(f.calls[RELEASE]==1 && strcmp(f.persisted,"old-config")==0); drop(&f);
}
static void test_cleanup_errors_are_visible_and_block_parallel_start(void) {
    for(unsigned failure=RESTORE;failure<=RELEASE;++failure) {
        Fake f; create(&f); start(&f); applied(&f); f.fail=failure;
        step(&f,BALABALA_REQUEST_NETWORK_CANCEL,200); end(&f);
        assert(f.n.fault && f.n.active==(failure==RELEASE) && f.n.released==(failure!=RELEASE));
        assert(f.n.status==(failure==RELEASE?BALABALA_NETWORK_CLEANUP_FAILED:BALABALA_NETWORK_FAILED));
        assert(f.calls[RELEASE]==1 && !f.n.pop[0]);
        assert(f.resources==(failure==RELEASE));
        step(&f,BALABALA_REQUEST_NETWORK_START,300); assert(f.calls[START]==1); drop(&f);
    }
}
static void test_cancel_same_batch_never_opens_entry(void) {
    Fake f; create(&f);
    step(&f,BALABALA_REQUEST_NETWORK_START|BALABALA_REQUEST_NETWORK_CANCEL,0);
    assert(!f.calls[PREPARE] && !f.n.active); drop(&f);
}
static void test_success_commits_before_same_batch_cancel_shutdown_and_end(void) {
    const uint32_t requests[]={BALABALA_REQUEST_NETWORK_CANCEL,BALABALA_REQUEST_SHUTDOWN};
    for(unsigned request=0;request<2;++request) for(unsigned ended=0;ended<2;++ended) {
        Fake f; create(&f); start(&f); applied(&f);
        publish(&f,BALABALA_NET_EVENT_SUCCESS | (ended?BALABALA_NET_EVENT_END:0));
        step(&f,requests[request],200);
        assert(f.n.success && f.n.status==(ended?BALABALA_NETWORK_SUCCESS:BALABALA_NETWORK_CLOSING) && f.calls[STOP]==1);
        if(!ended) end(&f);
        assert(!f.n.active && !f.calls[RESTORE] && !strcmp(f.persisted,"new-config")); drop(&f);
    }
}
static void test_success_fail_order_and_same_batch_semantics(void) {
    /* 成功先被消费后，失败不推翻提交；失败先进入停止态后，成功为迟到事件。 */
    for(unsigned success_first=0;success_first<2;++success_first) {
        Fake f; create(&f); start(&f); applied(&f);
        publish(&f,success_first?BALABALA_NET_EVENT_SUCCESS:BALABALA_NET_EVENT_FAIL); step(&f,0,200);
        publish(&f,success_first?BALABALA_NET_EVENT_FAIL:BALABALA_NET_EVENT_SUCCESS); step(&f,0,201);
        assert(f.n.success==!!success_first);
        assert(f.calls[STOP]==(success_first?0u:1u));
        end(&f); assert(f.calls[RESTORE]==(success_first?0u:1u));
        assert(!strcmp(f.persisted,success_first?"new-config":"old-config")); drop(&f);
    }
    /* 有界位通道不保存顺序：同批RECV/FAIL/SUCCESS明确以成功提交为准。 */
    for(unsigned success_first=0;success_first<2;++success_first) {
        Fake f; create(&f); start(&f); applied(&f);
        publish(&f,success_first?BALABALA_NET_EVENT_SUCCESS:BALABALA_NET_EVENT_FAIL);
        publish(&f,success_first?BALABALA_NET_EVENT_FAIL:BALABALA_NET_EVENT_SUCCESS); step(&f,0,200);
        assert(f.n.success && f.n.status==BALABALA_NETWORK_SUCCESS && !f.calls[STOP]);
        end(&f); assert(!f.calls[RESTORE] && !strcmp(f.persisted,"new-config")); drop(&f);
    }
}
static void test_finish_query_and_deinit_errors_retain_ownership_until_manual_retry(void) {
    for(unsigned queried=0;queried<2;++queried) {
        Fake f; create(&f); start(&f); applied(&f);
        step(&f,BALABALA_REQUEST_NETWORK_CANCEL,200);
        if(queried) f.query_failure_at=f.queries+2; /* step check succeeds, finish check fails */
        else f.fail=DEINIT;
        end(&f);
        assert(f.n.active && f.n.manager && f.n.saved && f.n.fault && f.n.cleanup_error);
        assert(f.advertising && f.resources && !f.n.name[0] && !f.n.pop[0]);
        assert(!f.calls[RESTORE] && !f.calls[RELEASE] && !strcmp(f.persisted,"new-config"));
        unsigned deinits=f.calls[DEINIT];
        for(unsigned i=0;i<10;++i) {
            publish(&f,BALABALA_NET_EVENT_END|BALABALA_NET_EVENT_SUCCESS);
            step(&f,BALABALA_REQUEST_SHUTDOWN|BALABALA_REQUEST_NETWORK_CANCEL|BALABALA_REQUEST_NETWORK_START,300+i);
        }
        assert(f.calls[DEINIT]==deinits && f.calls[START]==1 && f.calls[STOP]==1);
        f.fail=DEINIT; assert(balabala_network_retry_cleanup(&f.n)==-DEINIT && f.n.active);
        assert(!f.calls[RESTORE] && !f.calls[RELEASE]);
        f.fail=0; assert(!balabala_network_retry_cleanup(&f.n));
        assert(!f.n.active && !f.n.manager && !f.n.cleanup_error && f.n.fault);
        assert(f.calls[RESTORE]==1 && f.calls[RELEASE]==1 && !strcmp(f.persisted,"old-config"));
        assert(balabala_network_retry_cleanup(&f.n)==-1 && f.calls[RELEASE]==1);
        step(&f,BALABALA_REQUEST_NETWORK_START,500); assert(f.calls[START]==1); drop(&f);
    }
}
static void test_manual_retry_propagates_restore_or_release_errors_without_double_release(void) {
    for(unsigned failure=RESTORE;failure<=RELEASE;++failure) {
        Fake f; create(&f); start(&f); f.fail=DEINIT; end(&f);
        assert(f.n.active && f.n.cleanup_error);
        f.fail=failure; assert(balabala_network_retry_cleanup(&f.n)==-(int)failure);
        assert(f.n.active==(failure==RELEASE) && !f.n.manager && f.n.fault);
        assert(f.n.released==(failure!=RELEASE) && !!f.n.cleanup_error==(failure==RELEASE));
        assert(f.calls[RESTORE]==1 && f.calls[RELEASE]==1);
        assert(balabala_network_retry_cleanup(&f.n)==-1 && f.calls[RELEASE]==1); drop(&f);
    }
}
static void test_stages_deadline_identity_and_advertising_contract(void) {
    Fake f; create(&f); step(&f,BALABALA_REQUEST_NETWORK_START,100);
    assert(f.n.phase==1 && f.n.released && !f.count && !f.n.name[0]);
    assert(balabala_network_advance(&f.n,f.n.generation+1,1,101)==-1 && !f.count);
    assert(balabala_network_advance(&f.n,f.n.generation,2,101)==-1 && !f.count);
    assert(!balabala_network_advance(&f.n,f.n.generation,1,102));
    assert(f.n.phase==2 && !f.n.released && !f.calls[INIT] && f.n.deadline==300100);
    assert(!strcmp(f.n.name,"PROV_A1B2C3") && !strcmp(f.n.pop,"012345"));
    assert(balabala_network_advance(&f.n,f.n.generation,1,103)==-1 && f.calls[IDENTITY]==1);
    assert(!balabala_network_advance(&f.n,f.n.generation,2,104));
    assert(f.n.status==BALABALA_NETWORK_PREPARING && f.calls[INIT]==1 && f.calls[START]==1);
    assert(balabala_network_advance(&f.n,f.n.generation,2,105)==-1 && f.calls[START]==1);
    publish(&f,BALABALA_NET_EVENT_ADVERTISING); step(&f,0,106); assert(f.n.status==BALABALA_NETWORK_WAITING);
    publish(&f,BALABALA_NET_EVENT_RECV|BALABALA_NET_EVENT_ADVERTISING); step(&f,0,107);
    assert(f.n.status==BALABALA_NETWORK_CONNECTING);
    publish(&f,BALABALA_NET_EVENT_ADVERTISING_FAILED); step(&f,0,108);
    assert(f.n.stopping && f.n.terminal_status==BALABALA_NETWORK_FAILED); end(&f); drop(&f);
    for(unsigned phase=1;phase<=2;++phase) for(unsigned timeout=0;timeout<2;++timeout) {
        create(&f); step(&f,BALABALA_REQUEST_NETWORK_START,100);
        if(phase==2) assert(!balabala_network_advance(&f.n,f.n.generation,1,101));
        step(&f,timeout?0:BALABALA_REQUEST_NETWORK_CANCEL,timeout?300100:102);
        assert(!f.n.active && f.n.released && !f.calls[STOP] && !f.calls[INIT]);
        assert(f.calls[RELEASE]==(phase==2) && f.n.status==(timeout?BALABALA_NETWORK_TIMEOUT:BALABALA_NETWORK_CANCELLED));
        drop(&f);
    }
}
int main(void) {
    unsigned passed=0;
#define RUN(test) do { test(); printf("ok %u - %s\n",++passed,#test); } while(0)
    RUN(test_stages_deadline_identity_and_advertising_contract);
    RUN(test_finish_query_and_deinit_errors_retain_ownership_until_manual_retry);
    RUN(test_manual_retry_propagates_restore_or_release_errors_without_double_release);
    RUN(test_success_commits_before_same_batch_cancel_shutdown_and_end);
    RUN(test_success_fail_order_and_same_batch_semantics);
    RUN(test_success_keeps_phone_query_window_and_new_config);
    RUN(test_wrong_password_or_ap_failure_rolls_back);
    RUN(test_cancel_before_and_after_apply_and_late_success);
    RUN(test_deadline_is_300_seconds_and_terminal_is_sticky);
    RUN(test_success_at_deadline_preserves_phone_window_and_cancel);
    RUN(test_every_start_stage_failure_releases_and_retries);
    RUN(test_reopen_waits_for_end_and_ignores_old_generation);
    RUN(test_bounded_threadsafe_channel_cannot_drop_end);
    RUN(test_shutdown_closes_entry_before_cleanup_and_preserves_success);
    RUN(test_backup_failure_never_uses_unsaved_config);
    RUN(test_cleanup_errors_are_visible_and_block_parallel_start);
    RUN(test_cancel_same_batch_never_opens_entry);
    printf("network tests: %u passed; 0 failed (C ASan/UBSan, mock driver)\n",passed);
}
