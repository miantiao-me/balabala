#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "balabala_autotest.h"

typedef struct {
    BalabalaAutotest test;
    BalabalaAutotestRtc rtc;
    BalabalaRuntime runtime;
    BalabalaStorage storage;
    uint64_t now;
    unsigned downs, ups, held, restart, failures, memory, answer_mask;
    bool display_fail, memory_drop;
} Fixture;
static uint8_t frame[BALABALA_FRAMEBUFFER_BYTES];
static uint16_t stripe[BALABALA_STRIPE_PIXELS];
static uint32_t random_number(void *ctx) { unsigned *n=ctx; return ++*n; }
static uint32_t collision(void *ctx) { if (ctx) ++*(unsigned *)ctx; return 7; }
static void emit(void *ctx, BalabalaKey k, bool down) {
    Fixture *f=ctx;
    unsigned bit=1u<<k;
    if (down) { if (!(f->held & bit)) f->downs++; f->held|=bit; }
    else { assert(f->held & bit); f->held&=~bit; f->ups++; }
    BalabalaAutotestSnapshot s;
    assert(!balabala_app_autotest_snapshot(f->runtime.app,&s));
    if (down && k<4 && s.page==2 && s.mode==1 && s.answers[s.cursor]<0) f->answer_mask|=bit;
    assert(!balabala_app_key(f->runtime.app,k,down));
}
static void physical_emit(void *ctx, BalabalaKey key, bool pressed) {
    Fixture *f=ctx;
    balabala_autotest_user_input(&f->test);
    emit(ctx,key,pressed);
}
static void log_event(void *ctx,unsigned step,unsigned event,uint64_t a,uint64_t b) {
    Fixture *f=ctx;
    if (event>=100) {
        f->failures++;
        fprintf(stderr,"test event=%u step=%u a=%llu b=%llu\n",event,step,(unsigned long long)a,(unsigned long long)b);
    }
}
static void memory(void *ctx,unsigned cycle,uint32_t *free_bytes,uint32_t *largest) {
    Fixture *f=ctx; assert(cycle==f->memory++); *free_bytes=f->memory_drop ? 89999 : 90000; *largest=45056;
}
static void restart(void *ctx) { Fixture *f=ctx; f->restart++; }
static uint64_t now_ms(void *ctx) { return ((Fixture *)ctx)->now; }
static void poll(void *ctx,BalabalaApp *app) { (void)ctx;(void)app; }
static int display(void *ctx,const uint8_t *p,bool partial) { (void)p;(void)partial;return ((Fixture *)ctx)->display_fail ? -1:0; }
static int save(void *ctx,BalabalaApp *app) {
    Fixture *f=ctx; BalabalaAutotestSnapshot a,b;
    assert(!balabala_app_autotest_snapshot(app,&a));
    int result=balabala_storage_save(&f->storage,app);
    assert(!balabala_app_autotest_snapshot(app,&b));
    if (a.dirty) balabala_autotest_saved(&f->test,&a,&b,result,f->storage.report.status);
    return result;
}
static int noop(void *ctx) { (void)ctx;return 0; }
static bool released(void *ctx) { (void)ctx;return true; }
static void init(Fixture *f,const char *root) {
    assert(!balabala_storage_init(&f->storage,root,NULL));
    BalabalaRuntimeOps ops={.now_ms=now_ms,.poll=poll,.display=display,.save=save,
        .display_sleep=noop,.power_released=released,.sleep=noop,.context=f};
    assert(!balabala_runtime_init(&f->runtime,balabala_app_create(),frame,stripe,&ops,true));
    BalabalaAutotestOps at={.emit=emit,.log=log_event,.memory=memory,.restart=restart,.context=f};
    balabala_autotest_init(&f->test,&f->rtc,&at,f->now);
}
static void tick(Fixture *f) {
    balabala_autotest_tick(&f->test,&f->runtime,&f->storage,f->now,true);
    balabala_runtime_step(&f->runtime); f->now+=3000;
}
static void startup_policy(const char *mount, const BalabalaAutotestRtc *running,
                           const BalabalaAutotestRtc *armed, unsigned *random_count) {
    char root[BALABALA_AUTOTEST_ROOT_BYTES], armed_root[BALABALA_AUTOTEST_ROOT_BYTES];
    BalabalaAutotestRtc consumed=*armed;
    unsigned before=*random_count;
    assert(balabala_autotest_start(armed_root,mount,&consumed,ESP_RST_SW,random_number,random_count)==1);
    assert(consumed.stage==2 && *random_count==before);
    BalabalaAutotestRtc terminal=*armed;
    BalabalaAutotest stopped; BalabalaAutotestOps ops={0};
    balabala_autotest_init(&stopped,&terminal,&ops,0);
    balabala_autotest_user_input(&stopped);
    assert(terminal.stage==3);
    BalabalaAutotestRtc markers[7]={{0},*running,*armed,consumed,terminal,*armed,*armed};
    markers[5].checksum^=1; markers[6].magic^=1;
    const esp_reset_reason_t fresh[]={ESP_RST_POWERON,ESP_RST_USB};
    const esp_reset_reason_t rejected[]={ESP_RST_UNKNOWN,ESP_RST_EXT,ESP_RST_PANIC,
        ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_DEEPSLEEP,ESP_RST_BROWNOUT,
        ESP_RST_SDIO,ESP_RST_JTAG,ESP_RST_EFUSE,ESP_RST_PWR_GLITCH,ESP_RST_CPU_LOCKUP,
        (esp_reset_reason_t)123};
    for (unsigned i=0;i<7;i++) {
        for (unsigned j=0;j<sizeof(fresh)/sizeof(fresh[0]);j++) {
            BalabalaAutotestRtc r=markers[i]; before=*random_count;
            assert(balabala_autotest_start(root,mount,&r,fresh[j],random_number,random_count)==0);
            assert(root[0] && strcmp(root,armed_root) && r.stage==0 && *random_count==before+2);
            assert(!r.digest && !r.generation && !r.slot);
            char first[BALABALA_AUTOTEST_ROOT_BYTES]; strcpy(first,root);
            assert(balabala_autotest_start(root,mount,&r,fresh[j],random_number,random_count)==0);
            assert(strcmp(root,first) && r.stage==0);
        }
        BalabalaAutotestRtc r=markers[i]; before=*random_count;
        int result=balabala_autotest_start(root,mount,&r,ESP_RST_SW,random_number,random_count);
        if (i==2) {
            assert(result==1 && !strcmp(root,armed_root) && r.stage==2);
            assert(r.digest==armed->digest && r.generation==armed->generation && r.slot==armed->slot);
            assert(balabala_autotest_start(root,mount,&r,ESP_RST_SW,random_number,random_count)==-1 && !root[0]);
        } else assert(result==-1 && !root[0] && !memcmp(&r,&markers[i],sizeof(r)));
        assert(*random_count==before);
        for (unsigned j=0;j<sizeof(rejected)/sizeof(rejected[0]);j++) {
            r=markers[i]; strcpy(root,"production");
            assert(balabala_autotest_start(root,mount,&r,rejected[j],random_number,random_count)==-1);
            assert(!root[0] && *random_count==before && !memcmp(&r,&markers[i],sizeof(r)));
        }
    }
    /* SD缺失时也清理新运行的旧标记，且SW机会在任何SD IO前消耗。 */
    for (unsigned j=0;j<sizeof(fresh)/sizeof(fresh[0]);j++) {
        BalabalaAutotestRtc unavailable=*armed; before=*random_count;
        assert(balabala_autotest_start(root,NULL,&unavailable,fresh[j],random_number,random_count)==-1);
        assert(!root[0] && unavailable.stage==3 && !unavailable.digest && !unavailable.generation && !unavailable.slot);
        assert(*random_count==before);
        assert(balabala_autotest_start(root,mount,&unavailable,ESP_RST_SW,random_number,random_count)==-1 && !root[0]);
    }
    BalabalaAutotestRtc unavailable=*armed;
    assert(balabala_autotest_start(root,NULL,&unavailable,ESP_RST_SW,random_number,random_count)==-1);
    assert(!root[0] && unavailable.stage==2);
    assert(balabala_autotest_start(root,mount,&unavailable,ESP_RST_SW,random_number,random_count)==-1 && !root[0]);
    /* 恢复SD IO失败也先消耗stage=1，不会在下一次SW重试。 */
    BalabalaAutotestRtc missing=*armed;
    assert(balabala_autotest_start(root,"/nonexistent/bala",&missing,ESP_RST_SW,random_number,random_count)==-1);
    assert(!root[0] && missing.stage==2);
    assert(balabala_autotest_start(root,mount,&missing,ESP_RST_SW,random_number,random_count)==-1 && !root[0]);
    puts("startup policy: actual IDF reset enum, 7 RTC states x 17 reset reasons, fresh roots and single SW consumption passed");
}
static void candidate_noise(const char *mount, unsigned *n) {
    /* 两路ADC及电源候选均只暂停注入；瞬态返回空闲后能继续，未产生event101。 */
    for (unsigned bank=0;bank<3;bank++) {
        char root[BALABALA_AUTOTEST_ROOT_BYTES]; Fixture f={0};
        assert(!balabala_autotest_start(root,mount,&f.rtc,ESP_RST_POWERON,random_number,n));
        init(&f,root); tick(&f); tick(&f); tick(&f);
        assert(f.test.step==1 && !f.test.waiting && f.downs==0);
        BalabalaInput input; assert(!balabala_input_init(&input,NULL));
        balabala_input_sample(&input,0,4095,true,4095,true,1,physical_emit,&f);
        assert(balabala_autotest_input_idle(&input));
        balabala_input_sample(&input,10,bank==0?0:4095,true,bank==1?0:4095,true,bank==2?0:1,physical_emit,&f);
        assert(!balabala_autotest_input_idle(&input));
        for (unsigned i=0;i<3;i++) {
            balabala_autotest_tick(&f.test,&f.runtime,&f.storage,f.now+i,balabala_autotest_input_idle(&input));
            assert(!f.test.stopped && !f.failures && !f.downs && f.test.step==1);
        }
        balabala_input_sample(&input,20,4095,true,4095,true,1,physical_emit,&f);
        assert(balabala_autotest_input_idle(&input));
        balabala_autotest_tick(&f.test,&f.runtime,&f.storage,f.now+3,balabala_autotest_input_idle(&input));
        assert(!f.test.stopped && !f.failures && f.downs==1 && f.ups==1 && !f.held);
        balabala_app_destroy(f.runtime.app);
    }
    puts("input gate: transient ADC0/ADC1/power candidates pause injection and resume without user-abort passed");
}
int main(void) {
    char mount[]="/tmp/bala-at-XXXXXX"; assert(mkdtemp(mount));
    char root[BALABALA_AUTOTEST_ROOT_BYTES];
    BalabalaAutotestRtc r={0}; unsigned n=0;
    assert(balabala_autotest_start(root,mount,&r,ESP_RST_SW,random_number,&n)==-1 && !root[0] && !n);
    assert(balabala_autotest_start(root,"/nonexistent/bala",&r,ESP_RST_POWERON,random_number,&n)==-1 && !root[0]);
    memset(&r,0,sizeof(r));
    assert(!balabala_autotest_start(root,mount,&r,ESP_RST_POWERON,collision,NULL));
    BalabalaAutotestRtc interrupted=r;
    assert(balabala_autotest_start(root,mount,&interrupted,ESP_RST_SW,random_number,&n)==-1 && !root[0]);
    interrupted=r; interrupted.checksum^=1;
    assert(balabala_autotest_start(root,mount,&interrupted,ESP_RST_SW,random_number,&n)==-1 && !root[0]);
    memset(&r,0,sizeof(r));
    unsigned collisions=0;
    assert(balabala_autotest_start(root,mount,&r,ESP_RST_POWERON,collision,&collisions)==-1 && !root[0]);
    assert(collisions==16);
    Fixture f={0};
    assert(!balabala_autotest_start(root,mount,&f.rtc,ESP_RST_POWERON,random_number,&n));
    BalabalaAutotestRtc running=f.rtc;
    init(&f,root);
    for (unsigned i=0;i<500 && !f.test.stopped;i++) tick(&f);
    assert(f.test.stopped && f.restart==1 && !f.failures && f.memory==4);
    assert(f.downs==f.ups && !f.held && f.answer_mask==15 && f.test.saves>=2);
    startup_policy(mount,&running,&f.rtc,&n);
    candidate_noise(mount,&n);
    /* 唯一软件重启恢复：新开机App仍通过真实双槽restore读回。 */
    balabala_app_destroy(f.runtime.app);
    BalabalaAutotestRtc unexpected=f.rtc;
    assert(balabala_autotest_start(root,mount,&unexpected,ESP_RST_PANIC,random_number,&n)==-1 && !root[0]);
    assert(balabala_autotest_start(root,mount,&f.rtc,ESP_RST_SW,random_number,&n)==1);
    f.now=0; init(&f,root);
    assert(!balabala_storage_restore(&f.storage,f.runtime.app));
    BalabalaAutotestSnapshot s;
    assert(!balabala_app_autotest_snapshot(f.runtime.app,&s));
    assert(s.digest==f.rtc.digest && s.generation==f.rtc.generation && s.slot==f.rtc.slot);
    for (unsigned i=0;i<10 && !f.test.stopped;i++) tick(&f);
    assert(f.test.stopped && f.test.step==41 && !f.failures && f.restart==1);
    assert(balabala_autotest_start(root,mount,&f.rtc,ESP_RST_SW,random_number,&n)==-1 && !root[0]);
    balabala_app_destroy(f.runtime.app);
    Fixture g={0}; assert(!balabala_autotest_start(root,mount,&g.rtc,ESP_RST_POWERON,random_number,&n)); init(&g,root);
    tick(&g);tick(&g);tick(&g);
    unsigned before=g.downs;
    balabala_autotest_tick(&g.test,&g.runtime,&g.storage,g.now,true);
    assert(g.downs==before+1 && g.downs==g.ups);
    before=g.downs;
    /* 不调用runtime，revision无真实ack，不能发下一键。 */
    for (unsigned i=0;i<5;i++) {
        g.now+=100;
        balabala_autotest_tick(&g.test,&g.runtime,&g.storage,g.now,true);
        assert(g.downs==before && !g.test.stopped);
    }
    g.now=g.test.since+BALABALA_AUTOTEST_TIMEOUT_MS;
    balabala_autotest_tick(&g.test,&g.runtime,&g.storage,g.now,true);
    assert(g.test.stopped && g.failures==1 && g.downs==g.ups);
    balabala_app_destroy(g.runtime.app);
    /* 去抖稳定的两路ADC与电源事件仍终止，实体release仍成对送达。 */
    for (unsigned bank=0;bank<3;bank++) {
        Fixture h={0}; assert(!balabala_autotest_start(root,mount,&h.rtc,ESP_RST_POWERON,random_number,&n)); init(&h,root);
        BalabalaInput input; assert(!balabala_input_init(&input,NULL));
        balabala_input_sample(&input,0,bank==0?0:4095,true,bank==1?0:4095,true,bank==2?0:1,physical_emit,&h);
        assert(!h.test.stopped && !balabala_autotest_input_idle(&input));
        balabala_input_sample(&input,30,bank==0?0:4095,true,bank==1?0:4095,true,bank==2?0:1,physical_emit,&h);
        assert(!balabala_autotest_input_idle(&input));
        if (bank<2) assert(h.test.stopped && h.failures==1);
        else assert(!h.test.stopped && !h.failures);
        balabala_input_sample(&input,40,4095,true,4095,true,1,physical_emit,&h);
        balabala_input_sample(&input,70,4095,true,4095,true,1,physical_emit,&h);
        tick(&h);
        assert(h.test.stopped && h.failures==1 && h.downs==1 && h.ups==1 && !h.held);
        balabala_app_destroy(h.runtime.app);
    }
    Fixture m={0}; assert(!balabala_autotest_start(root,mount,&m.rtc,ESP_RST_POWERON,random_number,&n)); init(&m,root);
    /* 真实运行到相同状态的内存阶段，再制造实际可观测的下降，必须停止。 */
    for (unsigned i=0;i<500 && m.test.step!=28 && !m.test.stopped;i++) tick(&m);
    assert(!m.test.stopped && m.test.step==28);
    tick(&m);
    assert(m.test.cycle==1);
    for (unsigned i=0;i<10 && m.test.step!=28;i++) tick(&m);
    assert(m.test.step==28);
    m.memory_drop=true;
    tick(&m);
    assert(m.test.stopped && m.failures==1 && !m.restart && m.downs==m.ups);
    balabala_app_destroy(m.runtime.app);
    puts("autotest: real learning/save/restore/reboot, isolation/collision/RTC rejection, ack gate, paired release, timeout, user abort, memory decline passed");
    /* 保留临时隔离存档作为宿主证据；从不递归删除目录。 */
    return 0;
}
