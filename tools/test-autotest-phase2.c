#define _DARWIN_C_SOURCE 1
#define main adapter_test_main
#include "test-network-idf.c"
#undef main
#include <stdlib.h>
#include <unistd.h>
#include "balabala_autotest.h"
#include "balabala_alloc_diag.h"
#include <pthread.h>

static pthread_mutex_t alloc_mutex=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local uint32_t alloc_lock_depth;
uint32_t balabala_alloc_diag_host_lock(void) {
    uint32_t previous=alloc_lock_depth++;
    if (!previous) assert(!pthread_mutex_lock(&alloc_mutex));
    return previous;
}
void balabala_alloc_diag_host_unlock(uint32_t previous) {
    assert(alloc_lock_depth==previous+1);
    alloc_lock_depth=previous;
    if (!previous) assert(!pthread_mutex_unlock(&alloc_mutex));
}

typedef struct {
    BalabalaAutotest test;
    BalabalaAutotestRtc rtc;
    BalabalaRuntime runtime;
    BalabalaStorage storage;
    uint64_t now;
    unsigned downs, ups, failures, samples, displayed, cancel_mask;
    unsigned successes, oom_failures, network_logs, network_event_count[6];
    uint64_t network_a[6], network_b[6];
    bool no_end, duplicate_end, memory_drop, hold_display, oom_on_memory, oom_on_emit;
} Fixture;
static uint8_t frame[BALABALA_FRAMEBUFFER_BYTES];
static uint16_t stripe[BALABALA_STRIPE_PIXELS];
static unsigned random_count;
static uint32_t random_number(void *ctx) { (void)ctx; return ++random_count; }
static void emit_key(void *ctx,BalabalaKey k,bool down) {
    Fixture *f=ctx;
    assert(!balabala_alloc_diag_blocked());
    if(down) {
        ++f->downs;
        if(f->test.step==54) {
            assert(f->displayed==f->test.cycle+1);
            f->cancel_mask|=1u<<k;
        }
    } else ++f->ups;
    assert(!balabala_app_key(f->runtime.app,k,down));
    if (f->oom_on_emit) balabala_alloc_diag_failed(2345,0x804,NULL);
}
static void log_event(void *ctx,unsigned step,unsigned event,uint64_t a,uint64_t b) {
    (void)step; Fixture *f=ctx;
    if(event>=100) ++f->failures;
    if(event==8) ++f->successes;
    if(event==113) ++f->oom_failures;
    const unsigned network_events[]={20,23,24,21,25,26};
    for(unsigned i=0;i<6;++i) if(event==network_events[i]) {
        ++f->network_logs; ++f->network_event_count[i];
        f->network_a[i]=a; f->network_b[i]=b;
    }
}
static void memory(void *ctx,unsigned point,uint32_t *free_bytes,uint32_t *largest) {
    Fixture *f=ctx; assert(point==f->samples++);
    if (f->oom_on_memory) balabala_alloc_diag_failed(1234,0x808,(const char *)(uintptr_t)1);
    *free_bytes=f->memory_drop ? 89999 : 90000; *largest=45056;
}
static void restart(void *ctx) { (void)ctx; assert(!"phase2 must never restart"); }
static uint64_t now_ms(void *ctx) { return ((Fixture *)ctx)->now; }
static void poll(void *ctx,BalabalaApp *app) { (void)ctx;(void)app; }
static int display(void *ctx,const uint8_t *p,bool partial) {
    (void)p; Fixture *f=ctx;
    if(f->runtime.ops.network->active) assert(!partial);
    balabala_autotest_before_display(&f->test,&f->runtime);
    if(f->test.step==54) {
        if(f->runtime.ops.network->status==BALABALA_NETWORK_WAITING) ++f->displayed;
        if(f->hold_display) return -1;
    }
    return 0;
}
static int save(void *ctx,BalabalaApp *app) {
    Fixture *f=ctx; BalabalaAutotestSnapshot a,b;
    assert(!balabala_app_autotest_snapshot(app,&a));
    int result=balabala_storage_save(&f->storage,app);
    assert(!balabala_app_autotest_snapshot(app,&b));
    if(a.dirty) balabala_autotest_saved(&f->test,&a,&b,result,f->storage.report.status);
    return result;
}
static int noop(void *ctx) { (void)ctx;return 0; }
static bool released(void *ctx) { (void)ctx;return true; }
static void init(Fixture *f,const char *mount) {
    BalabalaAllocRtc discarded;
    assert(!balabala_alloc_diag_rtc_take(ESP_RST_POWERON,&discarded));
    balabala_alloc_diag_init(NULL);
    char root[BALABALA_AUTOTEST_ROOT_BYTES];
    assert(!balabala_autotest_start(root,mount,&f->rtc,ESP_RST_POWERON,random_number,NULL));
    assert(!balabala_storage_init(&f->storage,root,NULL));
    BalabalaRuntimeOps ops={.now_ms=now_ms,.poll=poll,.display=display,.save=save,
        .display_sleep=noop,.power_released=released,.sleep=noop,.context=f,.network=fresh(0)};
    assert(!balabala_runtime_init(&f->runtime,balabala_app_create(),frame,stripe,&ops,true));
    BalabalaAutotestOps at={.emit=emit_key,.log=log_event,.memory=memory,.restart=restart,.context=f};
    balabala_autotest_init(&f->test,&f->rtc,&at,f->now);
    assert(f->test.step==50);
}
static void tick(Fixture *f) {
    BalabalaNetwork *n=f->runtime.ops.network;
    if(n->started && !n->stopping && n->status==BALABALA_NETWORK_PREPARING)
        mock.advertising_callback(mock.advertising_context,0);
    if(n->stopping && n->manager && !n->cleanup_error && !f->no_end) {
        terminal_event(n,NETWORK_PROV_END);
        if(f->duplicate_end) terminal_event(n,NETWORK_PROV_END);
    }
    balabala_autotest_tick(&f->test,&f->runtime,&f->storage,f->now,true);
    balabala_runtime_step(&f->runtime); f->now+=100;
}
static void until(Fixture *f,unsigned step) {
    for(unsigned i=0;i<1000 && f->test.step!=step && !f->test.stopped;++i) tick(f);
    assert(f->test.step==step && !f->test.stopped);
}
static void destroy(Fixture *f) { balabala_app_destroy(f->runtime.app); }
static unsigned diag_samples, diag_phases[6];
static int diag_results[6];
static void diag_sample(uint32_t phase,uint32_t generation,int result) {
    assert(!alloc_lock_depth && generation==1 && diag_samples<6);
    diag_phases[diag_samples]=phase; diag_results[diag_samples++]=result;
}
static void *concurrent_failure(void *ctx) {
    uint32_t value=(uint32_t)(uintptr_t)ctx;
    balabala_alloc_diag_failed(value,value+100,NULL);
    return NULL;
}
static void test_alloc_records(void) {
    BalabalaNetwork *n=fresh(0), before=*n;
    balabala_alloc_diag_init(NULL);
    assert(!balabala_alloc_diag_blocked());
    uint32_t previous=balabala_alloc_diag_host_lock();
    for (unsigned i=0;i<6;++i) {
        balabala_alloc_diag_checkpoint(i+1,17,0);
        balabala_alloc_diag_failed(100+i,200+i,(const char *)(uintptr_t)1);
    }
    assert(alloc_lock_depth==1);
    balabala_alloc_diag_host_unlock(previous);
    BalabalaAllocObservation o;
    balabala_alloc_diag_observe(&o);
    assert(o.latched==1 && o.count==4 && o.overflow==1 && !o.register_result);
    for (unsigned i=0;i<4;++i) {
        assert(o.failures[i].size==100+i && o.failures[i].caps==200+i);
        assert(o.failures[i].phase==i+1 && o.failures[i].generation==17);
    }
    balabala_alloc_diag_checkpoint(99,18,0);
    balabala_alloc_diag_registered(0);
    assert(balabala_alloc_diag_blocked() && !memcmp(&before,n,sizeof(before)));
    BalabalaAllocObservation again; balabala_alloc_diag_observe(&again);
    assert(!memcmp(&o,&again,sizeof(o))); /* 后续阶段、成功注册不清首错。 */
    balabala_alloc_diag_init(NULL);
    balabala_alloc_diag_checkpoint(4,19,0);
    pthread_t threads[8];
    for (unsigned i=0;i<8;++i) assert(!pthread_create(&threads[i],NULL,concurrent_failure,(void *)(uintptr_t)(i+1)));
    for (unsigned i=0;i<8;++i) assert(!pthread_join(threads[i],NULL));
    balabala_alloc_diag_observe(&o);
    assert(o.count==4 && o.latched==1 && o.overflow==1);
    for (unsigned i=0;i<4;++i) {
        assert(o.failures[i].caps==o.failures[i].size+100);
        assert(o.failures[i].phase==4 && o.failures[i].generation==19);
    }
    BalabalaAllocRtc first=*balabala_alloc_diag_host_rtc();
    assert(first.size==o.failures[0].size && first.caps==o.failures[0].caps);
    assert(first.phase==o.failures[0].phase && first.generation==o.failures[0].generation);
    balabala_alloc_diag_init(NULL);
}
static void rtc_failure(void) {
    balabala_alloc_diag_init(NULL);
    balabala_alloc_diag_checkpoint(BALABALA_ALLOC_MANAGER_BEFORE,7,0);
    balabala_alloc_diag_failed(7654,0x4000,(const char *)(uintptr_t)1);
}
static void test_alloc_rtc(const char *mount) {
    volatile BalabalaAllocRtc *rtc=balabala_alloc_diag_host_rtc();
    BalabalaAllocRtc recovered, first;
    const esp_reset_reason_t warm[]={ESP_RST_SW,ESP_RST_PANIC,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT};
    for (unsigned i=0;i<sizeof(warm)/sizeof(warm[0]);++i) {
        BalabalaAutotestRtc automatic={0};
        char root[BALABALA_AUTOTEST_ROOT_BYTES];
        assert(!balabala_autotest_start(root,mount,&automatic,ESP_RST_POWERON,random_number,NULL));
        BalabalaAutotestRtc before=automatic;
        rtc_failure(); first=*rtc;
        balabala_alloc_diag_checkpoint(BALABALA_ALLOC_MANAGER_AFTER,8,0);
        for (unsigned n=0;n<8;++n) balabala_alloc_diag_failed(9000+n,0x804,NULL);
        recovered=*rtc;
        assert(!memcmp(&first,&recovered,sizeof(first))); /* 首条不被后续失败覆盖。 */
        balabala_alloc_diag_init(NULL); /* init即使提前调用也不抹掉待恢复证据。 */
        assert(balabala_alloc_diag_rtc_take(warm[i],&recovered));
        assert(recovered.magic && recovered.size==7654 && recovered.caps==0x4000);
        assert(recovered.phase==BALABALA_ALLOC_MANAGER_BEFORE && recovered.generation==7);
        assert(!rtc->magic && !memcmp(&before,&automatic,sizeof(before)));
        assert(!balabala_alloc_diag_rtc_take(warm[i],&recovered));
        balabala_alloc_diag_init(NULL);
        assert(!balabala_alloc_diag_rtc_take(ESP_RST_SW,&recovered)); /* 消费后再次重启不重放。 */
        unsigned random_before=random_count;
        assert(balabala_autotest_start(root,mount,&automatic,warm[i],random_number,NULL)<0);
        assert(!root[0] && random_count==random_before); /* app_main仍进入event110拒绝路径。 */
        assert(!memcmp(&before,&automatic,sizeof(before)));
    }
    const esp_reset_reason_t discard[]={ESP_RST_POWERON,ESP_RST_USB,ESP_RST_BROWNOUT,
        ESP_RST_DEEPSLEEP,ESP_RST_UNKNOWN,ESP_RST_EXT,ESP_RST_SDIO,ESP_RST_JTAG,ESP_RST_EFUSE,
        ESP_RST_PWR_GLITCH,ESP_RST_CPU_LOCKUP};
    for (unsigned i=0;i<sizeof(discard)/sizeof(discard[0]);++i) {
        rtc_failure();
        assert(!balabala_alloc_diag_rtc_take(discard[i],&recovered));
        assert(!rtc->magic && !balabala_alloc_diag_rtc_take(ESP_RST_INT_WDT,&recovered));
    }
    /* 使用真实hook的完整记录损坏每个字段，避免测试自己实现一份校验算法。 */
    for (unsigned i=0;i<6;++i) {
        rtc_failure();
        switch(i) {
        case 0: rtc->magic^=1; break;
        case 1: rtc->size^=1; break;
        case 2: rtc->caps^=1; break;
        case 3: rtc->phase^=1; break;
        case 4: rtc->generation^=1; break;
        case 5: rtc->checksum^=1; break;
        }
        assert(!balabala_alloc_diag_rtc_take(ESP_RST_INT_WDT,&recovered) && !rtc->magic);
    }
    rtc_failure(); first=*rtc;
    /* 构造所有magic提交前的写入前缀：包括载荷/校验全写完但magic未提交。 */
    for (unsigned prefix=0;prefix<=5;++prefix) {
        *rtc=(BalabalaAllocRtc){0};
        if (prefix>=1) rtc->size=first.size;
        if (prefix>=2) rtc->caps=first.caps;
        if (prefix>=3) rtc->phase=first.phase;
        if (prefix>=4) rtc->generation=first.generation;
        if (prefix>=5) rtc->checksum=first.checksum;
        assert(!balabala_alloc_diag_rtc_take(ESP_RST_INT_WDT,&recovered));
    }
    /* 正常启动不产生记录，注册和checkpoint也不产生记录。 */
    balabala_alloc_diag_init(NULL);
    balabala_alloc_diag_registered(0);
    balabala_alloc_diag_checkpoint(BALABALA_ALLOC_PREPARE_BEFORE,1,0);
    assert(!balabala_alloc_diag_rtc_take(ESP_RST_SW,&recovered));
    assert(!balabala_alloc_diag_blocked() && !alloc_lock_depth);
    rtc_failure();
    assert(balabala_alloc_diag_rtc_take(ESP_RST_INT_WDT,&recovered));
    balabala_alloc_diag_failed(9999,0x804,NULL);
    assert(!balabala_alloc_diag_rtc_take(ESP_RST_INT_WDT,&recovered));
    puts("phase2 RTC: first failure retained, init preserves evidence, warm reset including INT_WDT, cold/unsupported discard, all six corrupt fields and uncommitted prefixes rejected, consume once, rerun rejected, normal empty passed");
}
static int (*real_manager_start)(void *,const char *,const char *);
static int oom_manager_start(void *ctx,const char *name,const char *pop) {
    balabala_alloc_diag_failed(7654,0x4000,(const char *)(uintptr_t)1);
    return real_manager_start(ctx,name,pop);
}
static void test_alloc_stops_and_natural_return(const char *mount) {
    Fixture reg={0};init(&reg,mount);
    balabala_alloc_diag_registered(-7);tick(&reg);
    assert(reg.test.stopped && reg.failures==1 && !reg.downs && !reg.ups);
    assert(!mock.calls[NVS] && !mock.calls[ADVERTISE]);destroy(&reg);

    Fixture sample_oom={0};init(&sample_oom,mount);until(&sample_oom,52);
    /* phase2既有memory采样与SEND之间发生OOM，不能再发打开配网的按键。 */
    sample_oom.oom_on_memory=true;
    for (unsigned i=0;i<20 && !sample_oom.test.stopped;++i) tick(&sample_oom);
    assert(sample_oom.test.stopped && sample_oom.failures==1 && !mock.calls[ADVERTISE]);
    unsigned downs=sample_oom.downs,ups=sample_oom.ups;
    for(unsigned i=0;i<10;++i) tick(&sample_oom);
    assert(sample_oom.downs==downs && sample_oom.ups==ups && !sample_oom.test.cycle);
    destroy(&sample_oom);

    Fixture emit_oom={0};init(&emit_oom,mount);until(&emit_oom,52);
    emit_oom.oom_on_emit=true;downs=emit_oom.downs;ups=emit_oom.ups;
    for(unsigned i=0;i<20 && !emit_oom.test.stopped;++i) tick(&emit_oom);
    assert(emit_oom.test.stopped && emit_oom.failures==1);
    assert(emit_oom.downs==downs+1 && emit_oom.ups==ups && !mock.calls[ADVERTISE]);
    destroy(&emit_oom);

    Fixture natural={0};init(&natural,mount);until(&natural,52);
    BalabalaNetwork *n=natural.runtime.ops.network;
    real_manager_start=n->ops.manager_start;n->ops.manager_start=oom_manager_start;
    mock.fail=ADVERTISE;diag_samples=0;
    balabala_alloc_diag_init(diag_sample);
    for(unsigned i=0;i<30 && !natural.test.stopped;++i) tick(&natural);
    assert(natural.test.stopped && natural.failures==1 && !natural.test.cycle);
    assert(diag_samples==6);
    for(unsigned i=0;i<6;++i) assert(diag_phases[i]==i+1);
    assert(!diag_results[0] && !diag_results[1] && !diag_results[2] && !diag_results[3]);
    assert(diag_results[4]==-ADVERTISE && diag_results[5]==-ADVERTISE);
    BalabalaAllocObservation o;balabala_alloc_diag_observe(&o);
    assert(o.count==1 && o.latched && o.failures[0].size==7654 && o.failures[0].caps==0x4000);
    assert(o.failures[0].phase==BALABALA_ALLOC_MANAGER_BEFORE && o.failures[0].generation==n->generation);
    BalabalaAllocRtc rtc_record=*balabala_alloc_diag_host_rtc();
    assert(rtc_record.size==o.failures[0].size && rtc_record.caps==o.failures[0].caps);
    assert(rtc_record.phase==o.failures[0].phase && rtc_record.generation==o.failures[0].generation);
    BalabalaNetworkObservation net;balabala_network_observe(n,&net);
    assert(net.starts==1 && net.start_result==-ADVERTISE && !net.ends);
    assert(net.deinits==1 && !net.deinit_result && net.releases==1 && !net.release_result);
    assert_released(n);
    downs=natural.downs;ups=natural.ups;
    for(unsigned i=0;i<10;++i) tick(&natural);
    assert(natural.downs==downs && natural.ups==ups && mock.calls[ADVERTISE]==1 && mock.calls[MANAGER_DEINIT]==1);
    assert(!mock.persistent_writes);destroy(&natural);

    Fixture successful={0};init(&successful,mount);until(&successful,52);
    n=successful.runtime.ops.network;real_manager_start=n->ops.manager_start;n->ops.manager_start=oom_manager_start;
    for(unsigned i=0;i<30 && !successful.test.stopped;++i) tick(&successful);
    balabala_network_observe(n,&net);
    assert(successful.test.stopped && successful.failures==1 && successful.oom_failures==1 && !successful.successes);
    assert(net.starts==1 && !net.start_result);
    assert(n->active && n->manager && !net.stops && !net.ends && !net.deinits && !net.releases);
    assert(successful.network_logs==6);
    downs=successful.downs;ups=successful.ups;
    for(unsigned i=0;i<10;++i) tick(&successful);
    assert(successful.network_logs==6 && successful.failures==1);
    assert(successful.downs==downs && successful.ups==ups && mock.calls[ADVERTISE]==1 && !mock.calls[MANAGER_DEINIT]);
    /* 已停止脚本，仅投递自然END；原runtime先回收，下一tick输出新计数。 */
    terminal_event(n,NETWORK_PROV_END);tick(&successful);
    assert_released(n);
    assert(successful.network_logs==12 && successful.network_b[3]==1);
    assert(!successful.network_a[4] && !successful.network_a[5]);
    tick(&successful);
    assert(successful.network_logs==18);
    for(unsigned i=0;i<6;++i) assert(successful.network_event_count[i]==3);
    assert(successful.network_a[0]==n->generation && successful.network_b[0]==1);
    assert(successful.network_a[1]==n->generation && !successful.network_b[1]);
    assert(successful.network_a[2]==n->generation && !successful.network_b[2]);
    assert(successful.network_a[3]==n->generation && successful.network_b[3]==1);
    assert(successful.network_a[4]==1 && !successful.network_b[4]);
    assert(successful.network_a[5]==1 && !successful.network_b[5]);
    for(unsigned i=0;i<10;++i) tick(&successful);
    assert(successful.network_logs==18 && successful.failures==1 && successful.oom_failures==1);
    assert(successful.test.stopped && !successful.successes && !successful.test.cycle);
    assert(successful.downs==downs && successful.ups==ups && mock.calls[ADVERTISE]==1);
    assert(mock.calls[MANAGER_DEINIT]==1 && !mock.persistent_writes);
    /* 新实例及重新初始化必须重新输出首个完整快照，缓存不能跨实例串扰。 */
    Fixture reset={0};BalabalaAutotestOps ops=successful.test.ops;ops.context=&reset;
    balabala_autotest_init(&reset.test,&reset.rtc,&ops,successful.now);
    balabala_autotest_tick(&reset.test,&successful.runtime,&successful.storage,successful.now,true);
    assert(reset.network_logs==6 && reset.oom_failures==1 && reset.test.stopped);
    balabala_autotest_tick(&reset.test,&successful.runtime,&successful.storage,successful.now,true);
    assert(reset.network_logs==6 && reset.oom_failures==1);
    balabala_autotest_init(&reset.test,&reset.rtc,&ops,successful.now);
    balabala_autotest_tick(&reset.test,&successful.runtime,&successful.storage,successful.now,true);
    assert(reset.network_logs==12 && reset.oom_failures==2 && reset.test.stopped);
    assert(!reset.downs && !reset.ups && successful.network_logs==18);
    destroy(&successful);

    Fixture retained={0};init(&retained,mount);until(&retained,52);
    n=retained.runtime.ops.network;real_manager_start=n->ops.manager_start;n->ops.manager_start=oom_manager_start;
    mock.fail=ADVERTISE;mock.cleanup_error=-81;
    for(unsigned i=0;i<30 && !retained.test.stopped;++i) tick(&retained);
    assert(retained.test.stopped && n->cleanup_error==-81 && n->fault && n->active && n->manager);
    for(unsigned i=0;i<10;++i) tick(&retained);
    assert(!mock.calls[MANAGER_DEINIT] && !mock.calls[WIFI_DEINIT] && mock.wifi && mock.nvs);
    destroy(&retained);
    balabala_alloc_diag_init(NULL);
}
static void test_final_home_alloc_failure(const char *mount) {
    Fixture f={0};init(&f,mount);
    for(unsigned i=0;i<1000 && !(f.test.cycle==3 && f.test.step==56) && !f.test.stopped;++i) tick(&f);
    assert(!f.test.stopped && f.test.cycle==3 && f.test.step==56 && f.samples==15);
    assert(f.test.memory_free==90000 && f.test.memory_largest==45056);
    unsigned downs=f.downs,ups=f.ups;
    f.oom_on_memory=true;
    for(unsigned i=0;i<30 && !f.test.stopped;++i) tick(&f);
    assert(f.test.stopped && f.failures==1 && f.oom_failures==1 && !f.successes);
    assert(f.test.cycle==3 && f.test.step==56 && f.samples==16 && f.rtc.stage==3);
    assert(f.test.memory_free==90000 && f.test.memory_largest==45056);
    for(unsigned i=0;i<10;++i) tick(&f);
    assert(f.downs==downs && f.ups==ups && f.test.cycle==3 && !f.successes && f.oom_failures==1);
    assert_released(f.runtime.ops.network);destroy(&f);
    balabala_alloc_diag_init(NULL);
}
int main(void) {
    adapter_test_main();
    test_alloc_records();
    char mount[]="/tmp/bala-p2-XXXXXX"; assert(mkdtemp(mount));
    test_alloc_rtc(mount);
    Fixture f={0};init(&f,mount);
    for(unsigned i=0;i<1000 && !f.test.stopped;++i) tick(&f);
    assert(f.test.stopped && !f.failures && f.successes==1 && f.test.cycle==4 && f.samples==16);
    assert(f.displayed==4 && f.downs==f.ups);
    assert(f.cancel_mask==((1u<<BALABALA_FRONT4)|(1u<<BALABALA_FRONT3)|(1u<<BALABALA_HOME)));
    assert_released(f.runtime.ops.network); destroy(&f);
    /* Phase2 cannot consume even an otherwise armed phase1 SW marker. */
    char root[BALABALA_AUTOTEST_ROOT_BYTES];
    const esp_reset_reason_t rejected[]={ESP_RST_SW,ESP_RST_PANIC,ESP_RST_WDT,ESP_RST_BROWNOUT,ESP_RST_DEEPSLEEP,ESP_RST_UNKNOWN};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(rejected[0]);++i) {
        BalabalaAutotestRtc r=f.rtc; r.stage=1;
        assert(balabala_autotest_start(root,mount,&r,rejected[i],random_number,NULL)==-1 && !root[0]);
    }
    BalabalaAutotestRtc usb=f.rtc;
    assert(!balabala_autotest_start(root,mount,&usb,ESP_RST_USB,random_number,NULL));
    assert(root[0] && usb.stage==0);
    Fixture gate={0};init(&gate,mount);until(&gate,55);tick(&gate);
    BalabalaNetwork *clean=gate.runtime.ops.network;
    assert(!clean->active && !clean->manager && !clean->started && !clean->stopping && !clean->saved);
    bool *flags[]={&clean->active,&clean->manager,&clean->started,&clean->stopping,&clean->saved};
    unsigned held_downs=gate.downs;
    for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);++i) {
        *flags[i]=true;
        balabala_autotest_tick(&gate.test,&gate.runtime,&gate.storage,gate.now,true);
        assert(!gate.test.stopped && gate.test.step==55 && gate.downs==held_downs);
        *flags[i]=false;
    }
    char *identity_tail[]={&clean->name[sizeof(clean->name)-1],&clean->pop[sizeof(clean->pop)-1]};
    for(unsigned i=0;i<2;++i) {
        *identity_tail[i]=1;
        balabala_autotest_tick(&gate.test,&gate.runtime,&gate.storage,gate.now,true);
        assert(!gate.test.stopped && gate.test.step==55 && gate.downs==held_downs);
        *identity_tail[i]=0;
    }
    tick(&gate);assert(gate.test.step==56 && !gate.test.stopped);destroy(&gate);
    Fixture g={.hold_display=true};init(&g,mount);until(&g,54);
    /* 首准备帧缺少真实ack时，prepare/manager均不得启动，也不注入取消。 */
    unsigned before=g.downs;
    assert(g.runtime.pending && !mock.calls[NVS] && !mock.calls[ADVERTISE]);
    for(unsigned i=0;i<5;++i) balabala_autotest_tick(&g.test,&g.runtime,&g.storage,g.now+i,true);
    assert(g.downs==before && !g.test.stopped);
    g.now=g.test.since+60000;balabala_autotest_tick(&g.test,&g.runtime,&g.storage,g.now,true);
    assert(g.test.stopped && g.failures==1 && g.runtime.ops.network->active);destroy(&g);
    Fixture h={.no_end=true};init(&h,mount);until(&h,55);
    for(unsigned i=0;i<20;++i) tick(&h);
    assert(h.runtime.ops.network->active && h.test.cycle==0 && !h.test.stopped);
    h.now=h.test.since+60000;tick(&h);assert(h.test.stopped && h.failures==1);destroy(&h);
    Fixture d={.duplicate_end=true};init(&d,mount);until(&d,55);
    for(unsigned i=0;i<10 && !d.test.stopped;++i) tick(&d);
    assert(d.test.stopped && d.failures==1 && d.test.cycle==0);destroy(&d);
    Fixture fault={0};init(&fault,mount);until(&fault,55);
    mock.cleanup_error=-81;tick(&fault);tick(&fault);
    assert(fault.test.stopped && fault.failures==1);
    assert(fault.runtime.ops.network->fault && fault.runtime.ops.network->manager && mock.wifi && mock.nvs);
    assert(!mock.calls[MANAGER_DEINIT] && !mock.calls[WIFI_DEINIT]);destroy(&fault);
    Fixture m={0};init(&m,mount);until(&m,56);
    for(unsigned i=0;i<30 && m.test.cycle==0;++i) tick(&m);
    assert(m.test.cycle==1);
    until(&m,56);m.memory_drop=true;
    for(unsigned i=0;i<30 && !m.test.stopped;++i) tick(&m);
    assert(m.test.stopped && m.failures==1 && m.test.cycle==1);destroy(&m);
    Fixture input={0};init(&input,mount);until(&input,51);
    before=input.downs;
    balabala_autotest_tick(&input.test,&input.runtime,&input.storage,input.now,false);
    assert(input.downs==before && !input.test.stopped);
    balabala_autotest_user_input(&input.test);tick(&input);
    assert(input.test.stopped && input.failures==1 && input.downs==before);destroy(&input);
    test_alloc_stops_and_natural_return(mount);
    test_final_home_alloc_failure(mount);
    puts("phase2 review regression: final Home OOM reports event113 without success/cycle advance; stopped read-only END/deinit/release tail, unchanged-state dedup and per-instance cache reset passed");
    puts("phase2 allocation: capacity/overflow, first-failure latch, phase/generation, concurrent snapshot, registration failure, zero further injection, natural return/release, cleanup fault ownership, RAM WiFi passed");
    puts("phase2: real App/runtime + IDF mock, warmup+3 cycles, cancel/back/Home, END exact-once, ack/cleanup gates, timeout, natural fault ownership, input pause/abort, same-state memory passed");
    return 0;
}
