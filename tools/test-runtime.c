#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "balabala_display.h"
#include "balabala_runtime.h"
#include "balabala_input.h"
#include "balabala_storage.h"

typedef struct Fake Fake;
struct Fake {
    BalabalaRuntime runtime;
    BalabalaDisplay display;
    BalabalaInput input;
    BalabalaApp *app;
    uint8_t *frame;
    uint16_t *stripe;
    uint64_t now, busy_until, usb_sleep_revision;
    unsigned writes, resets, rows[2], refreshes, saves, sleeps, screen_sleeps, polls;
    unsigned fail_refreshes, fail_bank, reset_calls;
    uint8_t reset_levels;
    const uint8_t *expected_wire;
    size_t expected_length, expected_at;
    uint8_t command, control;
    bool timeout, inside, power_high, unavailable, save_failure;
    int bank, shutdown_order, save_order, screen_order, sleep_order;
    uint32_t frame_hash;
    bool compare_retries, requested_partial;
    unsigned partial_requests;
    void (*hook)(Fake *f);
    BalabalaNetwork network;
    BalabalaStorage *storage;
    unsigned net_prepares, net_identities, net_starts, net_stops, net_deinits, net_restores, net_releases;
    int net_prepare_error;
    bool cancel_after_ack;
    bool net_restore_failure, net_release_failure;
    unsigned checks, entries;
    int check_result, check_order, entry_order, net_stop_order, cancel_key;
    bool screen_failure, save_without_ack, invalidate_ack, cancel_in_sleep;
    bool input_error, external_power;
    int net_cleanup_error, net_deinit_error;
};
static uint64_t now_ms(void *context) { return ((Fake *)context)->now; }
static uint32_t hash(const uint8_t *frame) {
    uint32_t value=2166136261u;
    for (unsigned i=0;i<BALABALA_FRAMEBUFFER_BYTES;++i) value=(value ^ frame[i])*16777619u;
    return value;
}
static void emit(void *context, BalabalaKey key, bool pressed) {
    Fake *f=context;
    balabala_runtime_activity(&f->runtime,key,pressed);
    if ((!f->runtime.shutdown && !(balabala_app_status(f->app)&BALABALA_DOWNLOAD_ACTIVE)) ||
        !pressed || key==BALABALA_HOME || key==BALABALA_SHUTDOWN ||
        (!f->runtime.shutdown && key==BALABALA_FRONT3))
        assert(balabala_app_key(f->app,key,pressed)==0);
}
static void tap(Fake *f, BalabalaKey key) { emit(f,key,true); emit(f,key,false); }
static void enter_parent(Fake *f) {
    emit(f,BALABALA_FRONT3,true);
    tap(f,BALABALA_PARENT);
    emit(f,BALABALA_FRONT3,false);
}
static void poll(void *context, BalabalaApp *app) {
    Fake *f=context; assert(f->app==app); ++f->polls;
    if(f->inside && f->hook) f->hook(f);
    if(!f->inside && f->cancel_after_ack && f->network.phase &&
       balabala_app_network_ready(app,f->network.generation,f->network.phase,f->runtime.revision)) {
        f->cancel_after_ack=false; for(unsigned i=0;i<20;++i) tap(f,BALABALA_FRONT4);
    }
}
static void service(void *context, uint32_t delay) {
    Fake *f=context;
    f->now += delay;
    balabala_runtime_service(&f->runtime);
}
static int write_bytes(void *context, bool data, const uint8_t *bytes, size_t n) {
    Fake *f=context; ++f->writes;
    if(f->refreshes<=f->fail_refreshes) return -1;
    if(data && f->bank>=0 && f->fail_bank==(unsigned)f->bank+1 && f->rows[f->bank]==137)
        return -1;
    if(f->expected_wire) {
        assert(f->expected_at+1+n<=f->expected_length);
        assert(f->expected_wire[f->expected_at++]==(uint8_t)data);
        assert(memcmp(f->expected_wire+f->expected_at,bytes,n)==0);
        f->expected_at+=n;
    }
    if(!data) {
        assert(n==1); f->command=bytes[0]; f->bank=-1;
        if(bytes[0]==0x24) f->bank=0;
        if(bytes[0]==0x26) f->bank=1;
        if(bytes[0]==0x20) f->busy_until=f->now+50;
    } else if(f->bank>=0) {
        assert(n==100);
        uint8_t expected[100];
        unsigned row=f->rows[f->bank]++;
        assert(row<480 && balabala_display_rotate_row(f->frame,row,expected)==0);
        assert(memcmp(bytes,expected,100)==0);
    } else {
        switch(f->command) {
            case 0x18: assert(n==1 && bytes[0]==0x80); break;
            case 0x0c: assert(n==5 && memcmp(bytes,"\xae\xc7\xc3\xc0\x40",5)==0); break;
            case 0x01: assert(n==3 && memcmp(bytes,"\xdf\x01\x02",3)==0); break;
            case 0x3c: assert(n==1 && bytes[0]==1); break;
            case 0x11: assert(n==1 && bytes[0]==1); break;
            case 0x44: assert(n==4 && memcmp(bytes,"\0\0\x1f\x03",4)==0); break;
            case 0x45: assert(n==4 && memcmp(bytes,"\xdf\x01\0\0",4)==0); break;
            case 0x4e: assert(n==2 && bytes[0]==0 && bytes[1]==0); break;
            case 0x4f: assert(n==2 && bytes[0]==0xdf && bytes[1]==1); break;
            case 0x46: case 0x47: assert(n==1 && bytes[0]==0xf7); break;
            case 0x21: assert(n==1 && (bytes[0]==0x40 || bytes[0]==0)); break;
            case 0x22: assert(n==1 && (bytes[0]==0xf7 || bytes[0]==3 || bytes[0]==0xdf)); f->control=bytes[0]; break;
            case 0x10: assert(n==1 && bytes[0]==1); break;
            default: assert(0);
        }
    }
    return 0;
}
static int reset(void *context, bool high) {
    Fake *f=context; if(!high) ++f->resets;
    ++f->reset_calls; f->reset_levels=(uint8_t)((f->reset_levels<<1) | high); return 0;
}
static int busy(void *context) {
    Fake *f=context;
    return (f->timeout && f->command==0x20) || f->now < f->busy_until;
}
static int display(void *context, const uint8_t *frame, bool partial) {
    Fake *f=context; assert(frame==f->frame); ++f->refreshes;
    f->requested_partial=partial; f->partial_requests+=partial;
    if(balabala_app_status(f->app)&(BALABALA_NETWORK_OWNED | BALABALA_DOWNLOAD_ACTIVE)) assert(!partial);
    f->display.partial_limit=(uint8_t)balabala_app_clear_interval(f->app);
    bool fast=partial && f->display.baseline_valid && f->display.partials<f->display.partial_limit;
    f->rows[0]=f->rows[1]=0;
    if(f->compare_retries && f->refreshes>1) assert(hash(frame)==f->frame_hash);
    f->frame_hash=hash(frame); f->inside=true;
    int result=balabala_display_refresh(&f->display,frame,partial);
    f->inside=false;
    assert(hash(frame)==f->frame_hash);
    if (f->invalidate_ack && result==0) {
        f->invalidate_ack=false;
        assert(balabala_app_display_complete(f->app,f->runtime.revision)==0);
    }
    if(result==0) assert(f->rows[0]==480 && f->rows[1]==480 && f->control==(fast ? 0xdf : 0xf7));
    return result;
}
static int save(void *context, BalabalaApp *app) {
    Fake *f=context; assert(!f->network.active && f->network.released==!!f->runtime.ops.network); ++f->saves; f->save_order=++f->shutdown_order;
    if(f->storage) return balabala_storage_save(f->storage,app);
    if(f->unavailable) return 2;
    if(f->save_failure) return 1;
    if(f->save_without_ack) return 0;
    uint8_t *bytes=malloc(BALABALA_MAX_SAVE_BYTES); assert(bytes);
    uint32_t slot; uint64_t generation;
    assert(balabala_app_save_export(app,bytes,BALABALA_MAX_SAVE_BYTES,&slot,&generation)>0);
    assert(balabala_app_save_ack(app,generation,1)==0); free(bytes); return 0;
}
static int screen_sleep(void *context) {
    Fake *f=context; ++f->screen_sleeps; f->screen_order=++f->shutdown_order;
    if(f->runtime.shutdown && !f->runtime.display_fault) {
        assert(!f->runtime.pending && f->runtime.revision==f->runtime.shutdown_revision);
        assert(!(balabala_app_status(f->app)&BALABALA_DISPLAY_PENDING));
    }
    /* 关机仍使用同一个真实SSD1677休眠函数。 */
    f->inside=true; int result=balabala_display_sleep(&f->display); f->inside=false; return f->screen_failure ? -1 : result;
}
static bool released(void *context) { return ((Fake *)context)->power_high; }
static bool external_power(void *context) { return ((Fake *)context)->external_power; }
static bool input_idle(void *context) {
    Fake *f=context;
    return !f->input_error && f->power_high && balabala_input_idle(&f->input);
}
static int sleep_mcu(void *context) {
    Fake *f=context; assert(f->power_high); ++f->sleeps; f->sleep_order=++f->shutdown_order; return 0;
}
static int download_check(void *context) {
    Fake *f=context; ++f->checks; f->check_order=++f->shutdown_order; return f->check_result;
}
static void enter_download(void *context) {
    Fake *f=context; ++f->entries; f->entry_order=++f->shutdown_order;
    assert(!f->runtime.pending && !(balabala_app_status(f->app)&BALABALA_SAVE_DIRTY));
    assert(!f->network.active && !f->network.manager && !f->network.cleanup_error && !f->network.fault);
    assert(f->check_order<f->save_order && f->save_order<f->screen_order && f->screen_order<f->entry_order);
}
static void init(Fake *f, bool available) {
    memset(f,0,sizeof(*f)); f->bank=-1; f->power_high=true; f->unavailable=!available;
    f->app=balabala_app_create(); f->frame=malloc(BALABALA_FRAMEBUFFER_BYTES);
    f->stripe=malloc(BALABALA_STRIPE_PIXELS*sizeof(uint16_t)); assert(f->frame && f->stripe);
    assert(balabala_input_init(&f->input,NULL)==0);
    f->display=(BalabalaDisplay){.write=write_bytes,.reset=reset,.busy=busy,.now_ms=now_ms,.service=service,.context=f};
    BalabalaRuntimeOps ops={now_ms,poll,display,save,screen_sleep,released,sleep_mcu,f,NULL,download_check,enter_download,input_idle,external_power};
    assert(balabala_runtime_init(&f->runtime,f->app,f->frame,f->stripe,&ops,available)==0);
}
static void drop(Fake *f) { balabala_app_destroy(f->app); free(f->frame); free(f->stripe); }
static void step(Fake *f) { balabala_runtime_step(&f->runtime); }
static char *snapshot(Fake *f) {
    uint8_t *bytes=calloc(1,BALABALA_MAX_SAVE_BYTES+1); uint32_t slot; uint64_t generation;
    assert(bytes && balabala_app_save_export(f->app,bytes,BALABALA_MAX_SAVE_BYTES,&slot,&generation)>24);
    assert(balabala_app_save_ack(f->app,generation,0)==0);
    size_t length=(size_t)bytes[24] | (size_t)bytes[25]<<8 | (size_t)bytes[26]<<16 | (size_t)bytes[27]<<24;
    assert(bytes[4]==2 && length+28<=BALABALA_MAX_SAVE_BYTES);
    memmove(bytes,bytes+28,length); bytes[length]=0; return (char *)bytes;
}
static BalabalaKey choice(Fake *f, unsigned source) {
    char *json=snapshot(f), *cursor=strstr(json,"\"cursor\":"), *order=strstr(json,"\"permutations\":[");
    assert(cursor && order && source<4);
    unsigned index=(unsigned)strtoul(cursor+9,NULL,10);
    order+=strlen("\"permutations\":[");
    for(unsigned i=0;i<index;++i) { order=strchr(order,']'); assert(order); ++order; }
    order=strchr(order,'['); assert(order);
    for(unsigned i=0;i<4;++i) if((unsigned)(order[1+i*2]-'0')==source) { free(json); return (BalabalaKey)i; }
    assert(false); free(json); return BALABALA_FRONT1;
}
static void open_quiz(Fake *f) {
    step(f); tap(f,BALABALA_DOWN); step(f); tap(f,BALABALA_FRONT4); step(f);
    tap(f,BALABALA_DOWN); step(f); tap(f,BALABALA_FRONT4); step(f);
}
static void storage_path(char path[256], const char *root, const char *relative) {
    int n=snprintf(path,256,"%s/balabala/%s",root,relative); assert(n>0 && n<256);
}
static void storage_put(const char *root, const char *relative, const void *data, size_t n) {
    char path[256]; storage_path(path,root,relative); FILE *file=fopen(path,"wb"); assert(file);
    assert(fwrite(data,1,n,file)==n && fclose(file)==0);
}
static uint8_t *storage_get(const char *root, const char *relative, size_t *n) {
    char path[256]; storage_path(path,root,relative); FILE *file=fopen(path,"rb"); assert(file);
    assert(fseek(file,0,SEEK_END)==0); long length=ftell(file); assert(length>=0); rewind(file);
    *n=(size_t)length; uint8_t *data=calloc(1,*n+1); assert(data);
    assert(fread(data,1,*n,file)==*n && fclose(file)==0); return data;
}
static FILE *storage_deny(const char *path, const char *mode, void *context) {
    if(mode[0]=='r' && strstr(path,(const char *)context)) { errno=EIO; return NULL; }
    return fopen(path,mode);
}
static void storage_unchanged(const char *root, const uint8_t *old, size_t old_n) {
    size_t n; uint8_t *data=storage_get(root,"saves/slot0.bin",&n);
    assert(n==old_n && memcmp(data,old,n)==0); free(data);
    char path[256]; storage_path(path,root,"saves/slot1.bin");
    assert(access(path,F_OK)==-1 && errno==ENOENT);
}
static uint8_t *custom_deck(size_t *n) {
    FILE *file=fopen("content/hanzi.json","rb"); assert(file);
    uint8_t *data=calloc(1,BALABALA_MAX_DECK_BYTES+1); assert(data);
    *n=fread(data,1,BALABALA_MAX_DECK_BYTES,file); assert(!ferror(file) && fclose(file)==0);
    char *id=strstr((char *)data,"hanzi-01"); assert(id);
    memmove(id+9,id+8,*n-(size_t)(id-(char *)data)-8+1);
    memcpy(id,"custom-01",9); ++*n; return data;
}
static void test_storage_restore_faults_preserve_files_through_runtime_and_shutdown(void) {
    // One unread slot and three custom-deck startup failures use the real storage/ABI/runtime.
    for(unsigned failure=0;failure<4;++failure) {
        char root[80]=".build/runtime-storage-XXXXXX", path[256]; assert(mkdtemp(root));
        storage_path(path,root,""); assert(mkdir(path,0775)==0);
        storage_path(path,root,"decks"); assert(mkdir(path,0775)==0);
        BalabalaStorage storage; assert(balabala_storage_init(&storage,root,NULL)==0);
        Fake f; init(&f,true);
        size_t deck_n=0; uint8_t *deck=NULL;
        if(failure) {
            deck=custom_deck(&deck_n); storage_put(root,"decks/hanzi.json",deck,deck_n);
            assert(balabala_storage_import_decks(&storage,f.app)==0);
        }
        open_quiz(&f); tap(&f,BALABALA_FRONT1); step(&f);
        tap(&f,BALABALA_DOWN); step(&f); tap(&f,BALABALA_FRONT1); step(&f);
        char *before=snapshot(&f);
        assert(strstr(before,"\"cursor\":1") && strstr(before,"\"answers\":[1,0,") && strstr(before,"\"status\":2"));
        assert(balabala_storage_save(&storage,f.app)==0);
        size_t old_n; uint8_t *old=storage_get(root,"saves/slot0.bin",&old_n); drop(&f);
        storage_path(path,root,"decks/hanzi.json");
        if(failure==1) assert(unlink(path)==0);
        if(failure==2) storage_put(root,"decks/hanzi.json","{broken",7);
        BalabalaStorageIo io={.open=storage_deny,.context=failure==0 ? "slot0.bin" : "hanzi.json"};
        assert(balabala_storage_init(&storage,root,(failure==0 || failure==3) ? &io : NULL)==0);
        init(&f,true); f.storage=&storage;
        (void)balabala_storage_import_decks(&storage,f.app);
        (void)balabala_storage_show_import_error(&storage,f.app);
        assert(balabala_storage_restore(&storage,f.app)==1);
        assert(storage.report.status==BALABALA_STORAGE_DAMAGED);
        // Clearing the transient IO and reinitializing C storage must not unlock the model.
        assert(balabala_storage_init(&storage,root,NULL)==0);
        step(&f); tap(&f,BALABALA_FRONT4); step(&f); tap(&f,BALABALA_HOME); step(&f);
        open_quiz(&f); tap(&f,BALABALA_FRONT1); step(&f);
        unsigned saves=f.saves; f.now=f.runtime.save_at+BALABALA_SAVE_INTERVAL_MS+1; step(&f);
        assert(f.saves==saves+1 && f.runtime.save_result==1);
        assert(balabala_app_status(f.app)&BALABALA_SAVE_DIRTY); storage_unchanged(root,old,old_n);
        assert(balabala_storage_save(&storage,f.app)==1 && storage.report.app_result==-3);
        storage_unchanged(root,old,old_n);
        tap(&f,BALABALA_SHUTDOWN); step(&f);
        assert(f.runtime.shutdown_prepared && f.runtime.save_result==1);
        assert(balabala_app_status(f.app)&BALABALA_SAVE_DIRTY); storage_unchanged(root,old,old_n); drop(&f);
        if(failure) storage_put(root,"decks/hanzi.json",deck,deck_n);
        init(&f,true); assert(balabala_storage_import_decks(&storage,f.app)==0);
        assert(balabala_storage_restore(&storage,f.app)==0);
        char *after=snapshot(&f); assert(strcmp(before,after)==0); free(after); free(before);
        assert(balabala_storage_save(&storage,f.app)==0); drop(&f); free(old); free(deck);
        const char *files[]={"decks/hanzi.json","saves/slot0.bin","saves/slot1.bin"};
        for(unsigned i=0;i<3;++i) { storage_path(path,root,files[i]); if(unlink(path)) assert(errno==ENOENT); }
        storage_path(path,root,"decks"); assert(rmdir(path)==0);
        storage_path(path,root,"saves"); assert(rmdir(path)==0);
        storage_path(path,root,""); assert(rmdir(path)==0); assert(rmdir(root)==0);
    }
}
static bool pixel(const uint8_t *bytes,unsigned stride,unsigned x,unsigned y) {
    return (bytes[y*stride+x/8] & (0x80u>>(x%8)))!=0;
}
static void test_rotation_corners_checkerboard_and_asymmetry(void) {
    uint8_t *frame=malloc(BALABALA_FRAMEBUFFER_BYTES);
    uint8_t *old=calloc(1,BALABALA_FRAMEBUFFER_BYTES); assert(frame && old);
    uint8_t guarded[102], *row=guarded+1;
    /* 固定点同时跨越逻辑字节与传输字节边界，避免公式自身充当唯一预期。 */
    const unsigned points[][4]={
        {0,0,0,479},{479,0,0,0},{0,799,799,479},{479,799,799,0},
        {7,0,0,472},{8,799,799,471},{471,799,799,8},{472,0,0,7},
        {0,7,7,479},{479,8,8,0},{479,791,791,0},{0,792,792,479},
        {7,7,7,472},{8,8,8,471},{17,63,63,462},{309,645,645,170}
    };
    for(unsigned i=0;i<sizeof(points)/sizeof(points[0]);++i) {
        memset(frame,0xff,BALABALA_FRAMEBUFFER_BYTES);
        unsigned x=points[i][0], y=points[i][1]; frame[y*60+x/8]&=(uint8_t)~(0x80u>>(x%8));
        for(unsigned py=0;py<480;++py) {
            guarded[0]=guarded[101]=0x5a;
            assert(balabala_display_rotate_row(frame,py,row)==0);
            assert(guarded[0]==0x5a && guarded[101]==0x5a);
            for(unsigned px=0;px<800;++px)
                assert(pixel(row,100,px,0)!=(px==points[i][2] && py==points[i][3]));
        }
    }
    for(unsigned y=0;y<800;++y) for(unsigned x=0;x<480;++x) {
        bool white=((x/7 + y/11)%2)==0;
        unsigned at=y*60+x/8; if(white) frame[at]|=(uint8_t)(0x80u>>(x%8)); else frame[at]&=(uint8_t)~(0x80u>>(x%8));
        /* 旧传输帧的正向映射：(lx,ly) -> (799-ly,lx)。 */
        if(white) old[x*100+(799-y)/8]|=(uint8_t)(0x80u>>((799-y)%8));
    }
    bool differs_from_old=false, differs_from_mirror_x=false, differs_from_mirror_y=false;
    for(unsigned y=0;y<480;++y) {
        assert(balabala_display_rotate_row(frame,y,row)==0);
        for(unsigned x=0;x<800;++x) {
            bool white=pixel(row,100,x,0);
            assert(white==pixel(old,100,799-x,479-y));
            differs_from_old|=white!=pixel(old,100,x,y);
            differs_from_mirror_x|=white!=pixel(old,100,799-x,y);
            differs_from_mirror_y|=white!=pixel(old,100,x,479-y);
        }
    }
    assert(differs_from_old && differs_from_mirror_x && differs_from_mirror_y);
    memset(guarded,0x5a,sizeof(guarded)); assert(balabala_display_rotate_row(frame,480,row)==-1);
    for(unsigned i=0;i<sizeof(guarded);++i) assert(guarded[i]==0x5a);
    free(old); free(frame);
}
static void wire_append(Fake *f, const uint8_t *bytes, size_t n) {
    memcpy((uint8_t *)f->expected_wire+f->expected_length,bytes,n);
    f->expected_length+=n;
}
static void wire_area(Fake *f) {
    const uint8_t bytes[]={0,0x11,1,1,0,0x44,1,0,0,0x1f,3,
        0,0x45,1,0xdf,1,0,0,0,0x4e,1,0,0,0,0x4f,1,0xdf,1};
    wire_append(f,bytes,sizeof(bytes));
}
static void wire_frame(Fake *f, uint8_t bank) {
    wire_area(f);
    const uint8_t cmd[]={0,bank}; wire_append(f,cmd,sizeof(cmd));
    for(unsigned y=0;y<480;++y) {
        uint8_t row[101]={1}; assert(!balabala_display_rotate_row(f->frame,y,row+1));
        wire_append(f,row,sizeof(row));
    }
}
static void check_driver_refresh(Fake *f, bool request, bool fast) {
    /* 固定旧全刷的每个命令/数据边界和字节，局刷另有独立期望序列。 */
    f->expected_wire=malloc(99000); assert(f->expected_wire);
    f->expected_length=f->expected_at=0;
    if(!fast) {
        const uint8_t init[]={0,0x12,0,0x18,1,0x80,0,0x0c,1,0xae,0xc7,0xc3,0xc0,0x40,
            0,0x01,1,0xdf,1,2,0,0x3c,1,1};
        const uint8_t clear[]={0,0x46,1,0xf7,0,0x47,1,0xf7};
        wire_append(f,init,sizeof(init)); wire_area(f); wire_append(f,clear,sizeof(clear));
    }
    wire_frame(f,0x24);
    if(!fast) wire_frame(f,0x26);
    const uint8_t update[]={0,0x21,1,fast ? 0 : 0x40,0,0x22,1,fast ? 0xdf : 0xf7,0,0x20};
    wire_append(f,update,sizeof(update));
    if(fast) wire_frame(f,0x26);
    unsigned resets=f->reset_calls;
    f->reset_levels=0;
    assert(!display(f,f->frame,request));
    assert(f->expected_at==f->expected_length);
    assert(f->reset_calls==resets+(fast ? 0 : 3));
    assert(f->reset_levels==(fast ? 0 : 5));
    assert(f->display.baseline_valid);
    free((void *)f->expected_wire); f->expected_wire=NULL;
}
static void test_driver_full_bytes_first_partial_and_valid_baseline(void) {
    Fake f; init(&f,true);
    for(unsigned i=0;i<BALABALA_FRAMEBUFFER_BYTES;++i) f.frame[i]=(uint8_t)(i*37u+i/97u);
    check_driver_refresh(&f,true,false);
    assert(!f.display.partials);
    f.frame[1234]^=0x55;
    check_driver_refresh(&f,true,true);
    assert(f.display.partials==1);
    check_driver_refresh(&f,false,false);
    assert(!f.display.partials); drop(&f);
}
static void test_driver_partial_failures_force_full_retry(void) {
    for(unsigned fault=0;fault<3;++fault) {
        Fake f; init(&f,true); memset(f.frame,0x96,BALABALA_FRAMEBUFFER_BYTES);
        check_driver_refresh(&f,false,false);
        f.fail_bank=fault==0 ? 1 : fault==2 ? 2 : 0;
        f.timeout=fault==1;
        unsigned resets=f.reset_calls;
        assert(display(&f,f.frame,true)==(fault==1 ? BALABALA_EPD_TIMEOUT : BALABALA_EPD_IO));
        assert(!f.display.baseline_valid && !f.display.partials && f.reset_calls==resets);
        if(fault==1) assert(f.rows[0]==480 && !f.rows[1]);
        if(fault==2) assert(f.rows[0]==480 && f.rows[1]==137);
        f.fail_bank=0; f.timeout=false;
        check_driver_refresh(&f,true,false);
        assert(!f.display.partials);
        check_driver_refresh(&f,true,true); drop(&f);
    }
}
static void test_driver_sleep_invalidates_baseline(void) {
    for(unsigned fault=0;fault<2;++fault) {
        Fake f; init(&f,true); memset(f.frame,0xff,BALABALA_FRAMEBUFFER_BYTES);
        check_driver_refresh(&f,false,false);
        f.timeout=fault;
        assert(balabala_display_sleep(&f.display)==(fault ? BALABALA_EPD_TIMEOUT : 0));
        assert(!f.display.baseline_valid);
        f.timeout=false;
        check_driver_refresh(&f,true,false); drop(&f);
    }
}
static void open_clear_interval(Fake *f) {
    tap(f,BALABALA_HOME); step(f); enter_parent(f); step(f);
    tap(f,BALABALA_FRONT4); step(f);
    for(unsigned i=0;i<3;++i) tap(f,BALABALA_DOWN);
    step(f);
}
static void test_driver_partial_limit_restarts_after_full(void) {
    Fake f; init(&f,true); step(&f);
    assert(balabala_app_clear_interval(NULL)==8);
    const unsigned limits[]={8,16,32,64,128};
    for(unsigned n=0;n<5;++n) {
        if(n) { open_clear_interval(&f); tap(&f,BALABALA_FRONT4); step(&f); }
        assert(balabala_app_clear_interval(f.app)==limits[n]);
        tap(&f,BALABALA_HOME); step(&f);
        memset(f.frame,0xa5,BALABALA_FRAMEBUFFER_BYTES);
        check_driver_refresh(&f,false,false);
        for(unsigned cycle=0;cycle<2;++cycle) {
            for(unsigned i=1;i<=limits[n];++i) {
                check_driver_refresh(&f,true,true); assert(f.display.partials==i);
            }
            check_driver_refresh(&f,true,false); assert(!f.display.partials);
        }
    }
    for(unsigned i=0;i<20;++i) check_driver_refresh(&f,true,true);
    open_clear_interval(&f);
    assert(f.display.partials>8);
    tap(&f,BALABALA_FRONT4);
    assert(balabala_app_clear_interval(f.app)==8);
    step(&f);
    assert(f.control==0xf7 && !f.display.partials);
    check_driver_refresh(&f,true,true); assert(f.display.partials==1); drop(&f);
}
static void home_during_partial_sync(Fake *f) {
    if(f->bank!=1 || f->control!=0xdf || f->rows[1]!=1) return;
    f->hook=NULL;
    /* 局刷BUSY已结束、0x26刚开始同步：基准仍无效且帧未ACK，Home只合并不重入。 */
    assert(f->now>=f->busy_until && !f->display.baseline_valid && f->runtime.pending);
    unsigned refreshes=f->refreshes;
    tap(f,BALABALA_HOME);
    assert(f->inside && f->refreshes==refreshes && f->runtime.pending);
}
static void test_partial_sync_after_busy_before_ack(void) {
    Fake f; init(&f,true); step(&f);
    tap(&f,BALABALA_DOWN);
    f.hook=home_during_partial_sync; unsigned before=f.refreshes; step(&f);
    assert(!f.hook && f.refreshes==before+1 && f.requested_partial);
    assert(f.display.baseline_valid && f.display.partials==1 && !f.runtime.pending);
    assert(balabala_app_status(f.app)&BALABALA_NEEDS_REFRESH);
    step(&f); assert(f.refreshes==before+2 && f.display.partials==2); drop(&f);
}
static void test_runtime_partial_value_survives_failed_display_retry(void) {
    Fake f; init(&f,true); step(&f); assert(!f.requested_partial);
    tap(&f,BALABALA_DOWN);
    f.fail_refreshes=f.refreshes+1; step(&f);
    assert(f.requested_partial && f.runtime.partial && f.runtime.pending && !f.display.baseline_valid);
    unsigned resets=f.resets, refreshes=f.refreshes, partials=f.partial_requests;
    f.compare_retries=true;
    tap(&f,BALABALA_HOME);
    f.now=f.runtime.retry_at-1; step(&f); assert(f.refreshes==refreshes);
    f.now=f.runtime.retry_at; step(&f);
    assert(f.requested_partial && f.partial_requests==partials+1 && f.resets==resets+1);
    assert(!f.runtime.pending && !f.runtime.display_fault);
    f.compare_retries=false; step(&f);
    tap(&f,BALABALA_FRONT4); step(&f); assert(f.requested_partial); drop(&f);
}
static void test_full_sequence_and_only_success_ack(void) {
    Fake f; init(&f,true); step(&f);
    assert(f.refreshes==1 && f.resets==1 && !f.runtime.pending);
    assert(!(balabala_app_status(f.app)&BALABALA_DISPLAY_PENDING));
    assert(f.polls>=960 && f.control==0xf7); unsigned displayed=f.refreshes;
    for(unsigned i=0;i<100;++i) step(&f);
    assert(f.refreshes==displayed); drop(&f);
}
static void home_during_busy(Fake *f) {
    if(f->command!=0x20 || (f->control!=0xf7 && f->control!=0xdf)) return;
    f->hook=NULL;
    /* 实际输入去抖状态机在BUSY期间上报释放，电源短按产生Home。 */
    balabala_input_sample(&f->input,f->now,4095,true,4095,true,1,emit,f);
    f->now+=30;
    balabala_input_sample(&f->input,f->now,4095,true,4095,true,1,emit,f);
    tap(f,BALABALA_HOME);
    tap(f,BALABALA_FRONT4); /* 新桌面还未完成，正面键不得穿透。 */
}
static void test_busy_release_home_and_latest_state_no_queue(void) {
    Fake f; init(&f,true); open_quiz(&f);
    tap(&f,BALABALA_FRONT1); /* 第一题正确，停在反馈。 */
    balabala_input_sample(&f.input,f.now,3500,true,4095,true,1,emit,&f);
    f.now+=30; balabala_input_sample(&f.input,f.now,3500,true,4095,true,1,emit,&f);
    f.hook=home_during_busy; unsigned before=f.refreshes; step(&f);
    assert(f.input.adc[0].stable==-1);
    assert(balabala_app_status(f.app)&BALABALA_NEEDS_REFRESH);
    char *state=snapshot(&f); assert(strstr(state,"\"answers\":[1,-1,-1,-1,-1]")); free(state);
    step(&f); assert(f.refreshes==before+2);
    for(unsigned i=0;i<30;++i) step(&f);
    assert(f.refreshes==before+2); drop(&f);
}
static void answers_during_busy(Fake *f) {
    if(f->command!=0x20 || (f->control!=0xf7 && f->control!=0xdf)) return;
    f->hook=NULL;
    emit(f,BALABALA_FRONT1,false);
    for(unsigned i=0;i<40;++i) tap(f,BALABALA_FRONT1);
}
static void test_new_question_gate_release_and_single_score(void) {
    Fake f; init(&f,true); open_quiz(&f);
    emit(&f,BALABALA_FRONT1,true); step(&f); /* 第一题正确且仍按着。 */
    tap(&f,BALABALA_DOWN); f.hook=answers_during_busy; step(&f);
    char *state=snapshot(&f); assert(strstr(state,"\"answers\":[1,-1,-1,-1,-1]")); free(state);
    tap(&f,choice(&f,1)); step(&f);
    state=snapshot(&f); assert(strstr(state,"\"answers\":[1,1,-1,-1,-1]")); free(state);
    drop(&f);
}
static void test_spi_failure_retries_same_frame_and_ack_after_success(void) {
    Fake f; init(&f,true); f.fail_refreshes=2; f.compare_retries=true; step(&f);
    assert(f.runtime.pending && f.runtime.attempts==1 && (balabala_app_status(f.app)&BALABALA_DISPLAY_PENDING));
    tap(&f,BALABALA_DOWN); /* 真实导航产生新revision，旧帧仍须重试直到成功。 */
    for(unsigned i=0;i<2;++i) {
        f.now=f.runtime.retry_at-1; step(&f); assert(f.refreshes==i+1);
        f.now=f.runtime.retry_at; step(&f);
    }
    assert(f.refreshes==3 && f.resets==3 && !f.runtime.pending && !f.runtime.display_fault);
    assert(balabala_app_status(f.app)&BALABALA_NEEDS_REFRESH);
    f.compare_retries=false; step(&f); assert(f.refreshes==4); drop(&f);
}
static void test_timeout_bounded_fault_still_home_and_shutdown(void) {
    Fake f; init(&f,true); f.timeout=true; f.compare_retries=true; step(&f);
    assert(f.runtime.display_result==BALABALA_EPD_TIMEOUT && f.runtime.pending);
    for(unsigned i=1;i<3;++i) { f.now=f.runtime.retry_at; step(&f); }
    assert(f.refreshes==3 && f.runtime.display_fault && (balabala_app_status(f.app)&BALABALA_DISPLAY_PENDING));
    unsigned count=f.refreshes;
    tap(&f,BALABALA_HOME);
    for(unsigned i=0;i<100;++i) { f.now+=5000; step(&f); }
    assert(f.refreshes==count);
    tap(&f,BALABALA_SHUTDOWN); step(&f); assert(f.runtime.shutdown_prepared && f.screen_sleeps==1);
    assert(f.runtime.sleep_result==BALABALA_EPD_TIMEOUT); /* 关屏也超时，仍可结束等待并睡眠。 */
    f.now+=30; step(&f); assert(f.sleeps==1);
    assert(f.now<700000); drop(&f); /* fake时间，真实运行不等待30秒。 */
}
static void test_shutdown_during_busy_save_failure_and_release_before_sleep(void) {
    Fake f; init(&f,true); open_quiz(&f);
    f.save_failure=true; f.power_high=false;
    tap(&f,BALABALA_FRONT1);
    f.hook=NULL;
    tap(&f,BALABALA_SHUTDOWN); step(&f);
    assert(f.runtime.shutdown && f.runtime.shutdown_prepared && f.runtime.save_result==1);
    assert(f.save_order<f.screen_order && f.sleeps==0 && f.screen_sleeps==1);
    assert(balabala_app_status(f.app)&BALABALA_SAVE_DIRTY);
    for(unsigned i=0;i<10;++i) { f.now+=100; step(&f); }
    assert(f.sleeps==0);
    f.power_high=true; step(&f); f.now+=29; step(&f); assert(f.sleeps==0);
    f.power_high=false; step(&f); f.power_high=true; step(&f); f.now+=30; step(&f);
    assert(f.sleeps==1 && f.screen_order<f.sleep_order);
    step(&f); assert(f.sleeps==1); drop(&f);
}
static void shutdown_in_refresh(Fake *f) {
    if(f->command==0x20 && f->control==0xf7) { f->hook=NULL; f->power_high=false; tap(f,BALABALA_SHUTDOWN); }
}
static void test_shutdown_collected_inside_real_display_wait(void) {
    Fake f; init(&f,true); f.hook=shutdown_in_refresh; step(&f);
    assert(f.runtime.shutdown && !f.runtime.shutdown_prepared && !f.runtime.pending);
    step(&f); assert(f.runtime.shutdown_prepared && f.sleeps==0);
    f.power_high=true; step(&f); f.now+=30; step(&f); assert(f.sleeps==1); drop(&f);
}
static void test_shutdown_refresh_retries_before_sleep_and_saves_once(void) {
    for(unsigned failed_save=0;failed_save<2;++failed_save) {
        Fake f; init(&f,true); step(&f);
        f.save_failure=failed_save;
        f.fail_refreshes=f.refreshes+1;
        tap(&f,BALABALA_SHUTDOWN); step(&f);
        assert(f.saves==1 && f.runtime.pending && f.runtime.shutdown_revision);
        assert(!f.runtime.shutdown_prepared && !f.screen_sleeps && !f.sleeps && !f.requested_partial);
        uint64_t target=f.runtime.shutdown_revision;
        unsigned refreshes=f.refreshes; f.compare_retries=true;
        f.now=f.runtime.retry_at-1; step(&f);
        assert(f.refreshes==refreshes && !f.screen_sleeps);
        f.now=f.runtime.retry_at; step(&f);
        assert(f.refreshes==refreshes+1 && f.saves==1 && !f.runtime.pending);
        assert(f.runtime.revision==target && f.runtime.shutdown_prepared && f.screen_sleeps==1);
        f.now+=BALABALA_SLEEP_RELEASE_MS; step(&f);
        assert(f.sleeps==1 && f.save_order<f.screen_order && f.screen_order<f.sleep_order);
        drop(&f);
    }
}
static void test_shutdown_waits_for_preexisting_pending_then_acks_sleep_frame(void) {
    Fake f; init(&f,true); f.fail_refreshes=1; step(&f);
    uint64_t old_revision=f.runtime.revision;
    assert(f.runtime.pending);
    tap(&f,BALABALA_SHUTDOWN); step(&f);
    assert(f.saves==1 && f.runtime.pending && !f.screen_sleeps && !f.runtime.shutdown_revision);
    f.now=f.runtime.retry_at; step(&f);
    assert(f.refreshes==3 && f.runtime.revision>old_revision && f.runtime.shutdown_revision==f.runtime.revision);
    assert(f.saves==1 && f.screen_sleeps==1 && f.runtime.shutdown_prepared && !f.requested_partial);
    f.now+=BALABALA_SLEEP_RELEASE_MS; step(&f); assert(f.sleeps==1); drop(&f);
}
static void test_shutdown_retry_exhaustion_still_allows_power_off(void) {
    Fake f; init(&f,true); step(&f); f.fail_refreshes=f.refreshes+BALABALA_DISPLAY_MAX_ATTEMPTS;
    tap(&f,BALABALA_SHUTDOWN); step(&f);
    for(unsigned i=1;i<BALABALA_DISPLAY_MAX_ATTEMPTS;++i) {
        assert(!f.screen_sleeps && !f.sleeps);
        f.now=f.runtime.retry_at; step(&f);
    }
    assert(f.runtime.display_fault && f.runtime.shutdown_prepared && f.screen_sleeps==1 && f.saves==1);
    f.now+=BALABALA_SLEEP_RELEASE_MS; step(&f); assert(f.sleeps==1); drop(&f);
}
static void test_no_sd_builtin_learning_and_save_throttle(void) {
    Fake f; init(&f,false); open_quiz(&f); tap(&f,BALABALA_FRONT1); step(&f);
    char *state=snapshot(&f); assert(strstr(state,"\"status\":2")); free(state);
    f.now=f.runtime.save_at; step(&f);
    assert(f.runtime.save_result==2 && (balabala_app_status(f.app)&BALABALA_SAVE_DIRTY));
    unsigned saves=f.saves;
    for(unsigned i=0;i<100;++i) step(&f);
    assert(f.saves==saves);
    f.now=f.runtime.save_at; step(&f); assert(f.saves==saves+1);
    f.unavailable=false; f.now=f.runtime.save_at; step(&f);
    assert(f.runtime.save_result==0 && !(balabala_app_status(f.app)&BALABALA_SAVE_DIRTY)); drop(&f);
}
static void test_failed_save_retries_after_interval(void) {
    Fake f; init(&f,true); f.save_failure=true; step(&f); f.now=f.runtime.save_at; step(&f);
    assert(f.runtime.save_result==1 && f.saves==1);
    for(unsigned i=0;i<100;++i) { step(&f); }
    assert(f.saves==1);
    f.now=f.runtime.save_at-1; step(&f); assert(f.saves==1);
    f.save_failure=false; f.now=f.runtime.save_at; step(&f);
    assert(f.saves==2 && !(balabala_app_status(f.app)&BALABALA_SAVE_DIRTY)); drop(&f);
}
static void test_network_request_reports_failed_without_platform_service(void) {
    Fake f; init(&f,true); step(&f);
    enter_parent(&f); step(&f);
    for(unsigned i=0;i<3;++i) tap(&f,BALABALA_DOWN);
    step(&f); tap(&f,BALABALA_FRONT4);
    assert(balabala_app_network_status(f.app,BALABALA_NETWORK_CONNECTING)==0);
    step(&f); /* 真正runtime消费START并写入FAILED，不假成功。 */
    assert(balabala_app_platform_requests(f.app)==0);
    assert(balabala_app_network_status(f.app,BALABALA_NETWORK_SUCCESS)==-1);
    drop(&f);
}
static int net_ok(void *ctx) { assert(!((Fake *)ctx)->inside); return 0; }
static int net_prepare(void *ctx, uint32_t generation) { Fake *f=ctx; assert(generation); net_ok(ctx); ++f->net_prepares; return f->net_prepare_error; }
static int net_identity(void *ctx, char name[12], char pop[17]) {
    net_ok(ctx); ++((Fake *)ctx)->net_identities; memcpy(name,"PROV_A1B2C3",12); memcpy(pop,"012345",7); return 0;
}
static int net_start(void *ctx, const char *name, const char *pop) {
    Fake *f=ctx; net_ok(ctx); assert(!strcmp(name,f->network.name) && !strcmp(pop,f->network.pop)); ++f->net_starts; return 0;
}
static void net_stop(void *ctx) { Fake *f=ctx; net_ok(ctx); ++f->net_stops; f->net_stop_order=++f->shutdown_order; }
static int net_deinit(void *ctx) { Fake *f=ctx; net_ok(ctx); ++f->net_deinits; return f->net_deinit_error; }
static int net_cleanup_error(void *ctx) { Fake *f=ctx; net_ok(ctx); return f->net_cleanup_error; }
static int net_restore(void *ctx) { Fake *f=ctx; net_ok(ctx); ++f->net_restores; return f->net_restore_failure?-71:0; }
static int net_release(void *ctx) { Fake *f=ctx; net_ok(ctx); ++f->net_releases; return f->net_release_failure?-72:0; }
static void net_lock(void *ctx) { (void)ctx; } /* 此测试仅同线程注入。 */
static void enable_network(Fake *f) {
    BalabalaNetworkOps ops={net_prepare,net_ok,net_ok,net_identity,net_ok,net_start,net_stop,
                            net_deinit,net_cleanup_error,net_restore,net_release,net_lock,net_lock,f};
    balabala_network_init(&f->network,&ops); f->runtime.ops.network=&f->network;
}
static void open_network(Fake *f) {
    step(f); enter_parent(f); step(f);
    for(unsigned i=0;i<3;++i) tap(f,BALABALA_DOWN);
    step(f); tap(f,BALABALA_FRONT4);
}
static void activate_network(Fake *f) {
    unsigned starts=f->net_starts;
    step(f); assert(f->network.phase==2 && f->net_prepares && f->net_starts==starts);
    step(f); assert(f->network.started && !f->network.phase);
    balabala_network_publish(&f->network,f->network.generation,BALABALA_NET_EVENT_ADVERTISING);
    step(f); assert(f->network.status==BALABALA_NETWORK_WAITING);
}
static void start_network_direct(Fake *f) {
    balabala_network_step(&f->network,BALABALA_REQUEST_NETWORK_START,f->now);
    assert(!balabala_network_advance(&f->network,f->network.generation,1,f->now));
    assert(!balabala_network_advance(&f->network,f->network.generation,2,f->now));
}
static void cancel_and_end_in_display(Fake *f) {
    if(f->command!=0x20 || f->control!=0xf7) return;
    f->hook=NULL;
    tap(f,BALABALA_HOME);
    assert(!f->net_stops && !f->net_deinits && f->network.active);
}
static void test_network_service_only_collects_and_step_owns_lifecycle(void) {
    for(unsigned phase=1;phase<=2;++phase) {
        Fake f; init(&f,true); enable_network(&f); open_network(&f);
        balabala_runtime_service(&f.runtime);
        assert(!f.net_prepares && !f.net_starts && (f.runtime.requests & BALABALA_REQUEST_NETWORK_START));
        if(phase==2) { step(&f); assert(f.network.phase==2 && !f.net_starts); }
        f.hook=cancel_and_end_in_display; step(&f);
        assert(!f.net_starts && !f.net_stops && !f.net_deinits && !f.network.active && f.network.released);
        assert(f.net_prepares==(phase==2) && f.net_releases==(phase==2));
        step(&f); unsigned count=f.refreshes;
        for(unsigned i=0;i<100;++i) step(&f);
        assert(f.refreshes==count); drop(&f);
    }
}
static void test_network_status_storm_has_no_frame_queue_and_shutdown_waits_end(void) {
    Fake f; init(&f,true); enable_network(&f); open_network(&f); activate_network(&f);
    balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_RECV); step(&f);
    unsigned count=f.refreshes;
    for(unsigned i=0;i<100;++i) {
        balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_RECV); step(&f);
    }
    assert(f.refreshes==count);
    balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_SUCCESS); step(&f);
    assert(f.network.success && f.network.active && !f.net_stops && f.refreshes==count+1);
    tap(&f,BALABALA_SHUTDOWN); step(&f);
    assert(f.net_stops==1 && !f.runtime.shutdown_prepared && !f.screen_sleeps && !f.sleeps);
    balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f);
    assert(f.net_deinits==1 && f.net_releases==1 && f.runtime.shutdown_prepared);
    f.now+=30; step(&f); assert(f.sleeps==1); drop(&f);
}
static void test_reenter_during_cleanup_never_shows_old_identity_or_starts_in_parallel(void) {
    Fake f; init(&f,true); enable_network(&f); open_network(&f); activate_network(&f);
    tap(&f,BALABALA_HOME); step(&f); assert(f.network.stopping);
    for(unsigned i=0;i<20;++i) { tap(&f,BALABALA_HOME); tap(&f,BALABALA_FRONT4); step(&f); }
    assert(f.net_starts==1 && f.network.stopping && (balabala_app_status(f.app)&BALABALA_NETWORK_OWNED));
    balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f);
    tap(&f,BALABALA_HOME); step(&f); open_network(&f); activate_network(&f);
    assert(f.net_starts==2 && f.network.active);
    tap(&f,BALABALA_HOME); step(&f);
    balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f); drop(&f);
}
static void test_front4_success_exit_abi_closes_entry_before_end(void) {
    Fake f; init(&f,true); enable_network(&f); open_network(&f); activate_network(&f);
    balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_SUCCESS); step(&f);
    assert(f.network.success && f.network.active && !f.net_stops);
    tap(&f,BALABALA_FRONT4); step(&f);
    assert(f.network.stopping && f.net_stops==1 && !f.net_deinits);
    assert(balabala_app_status(f.app)&BALABALA_NETWORK_OWNED); /* 清理前仍独占面板。 */
    balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f);
    assert(!f.network.active && f.net_deinits==1 && f.net_releases==1 && !f.net_restores); drop(&f);
}
static void test_cancel_timeout_cleanup_failures_reach_real_rust_abi_page(void) {
    for(unsigned timeout=0;timeout<2;++timeout) for(unsigned release_failure=0;release_failure<2;++release_failure) {
        Fake f; init(&f,true); enable_network(&f); open_network(&f); activate_network(&f);
        if(timeout) f.now=f.network.deadline; else tap(&f,BALABALA_FRONT4);
        step(&f);
        assert(f.network.status==BALABALA_NETWORK_CLOSING && f.network.terminal_status==(timeout?BALABALA_NETWORK_TIMEOUT:BALABALA_NETWORK_CANCELLED));
        unsigned refreshes=f.refreshes; uint32_t previous=hash(f.frame);
        f.net_restore_failure=!release_failure; f.net_release_failure=release_failure;
        balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f);
        assert(f.network.fault && f.network.status==(release_failure?BALABALA_NETWORK_CLEANUP_FAILED:BALABALA_NETWORK_FAILED) && f.net_restores==1 && f.net_releases==1);
        assert(f.network.error==(release_failure?-72:-71));
        assert(f.refreshes==refreshes+1 && hash(f.frame)!=previous);
        assert(!!(balabala_app_status(f.app)&BALABALA_NETWORK_OWNED)==!!release_failure);
        assert(balabala_app_network_status(f.app,BALABALA_NETWORK_SUCCESS)==-1);
        drop(&f);
    }
}
static void test_rejected_start_after_restore_fault_releases_model_ownership(void) {
    const BalabalaKey exits[]={BALABALA_FRONT3,BALABALA_HOME};
    for(unsigned i=0;i<2;++i) {
        Fake f; init(&f,true); enable_network(&f); open_network(&f); activate_network(&f);
        f.net_restore_failure=true;
        tap(&f,exits[i]); step(&f);
        balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f);
        assert(f.network.fault && f.network.released && !f.network.active && f.network.status==BALABALA_NETWORK_FAILED);
        assert(!(balabala_app_status(f.app)&BALABALA_NETWORK_OWNED));
        tap(&f,exits[i]); step(&f); open_network(&f);
        assert(balabala_app_status(f.app)&BALABALA_NETWORK_OWNED);
        unsigned revision=f.network.revision;
        step(&f);
        assert(f.network.revision==revision+1 && f.runtime.network_revision==f.network.revision);
        assert(!(balabala_app_status(f.app)&BALABALA_NETWORK_OWNED));
        assert(f.network.fault && f.network.released && !f.network.active);
        assert(f.net_prepares==1 && f.net_starts==1 && f.net_restores==1 && f.net_releases==1);
        tap(&f,exits[i]); step(&f);
        unsigned refreshes=f.refreshes;
        for(unsigned tick=0;tick<10;++tick) step(&f);
        assert(f.refreshes==refreshes && f.network.revision==revision+1);
        open_network(&f); /* Home的设置入口仍可达，拒绝后也不残留owned。 */
        step(&f); assert(!(balabala_app_status(f.app)&BALABALA_NETWORK_OWNED));
        assert(f.net_prepares==1 && f.net_starts==1); drop(&f);
    }
}
static void test_rejected_batched_start_cancel_twice_from_failed_exits(void) {
    const BalabalaKey exits[]={BALABALA_FRONT3,BALABALA_HOME};
    for(unsigned i=0;i<2;++i) {
        Fake f; init(&f,true); enable_network(&f);
        for(unsigned attempt=0;attempt<3;++attempt) {
            open_network(&f); tap(&f,exits[i]);
            balabala_runtime_service(&f.runtime);
            assert((f.runtime.requests&(BALABALA_REQUEST_NETWORK_START|BALABALA_REQUEST_NETWORK_CANCEL))==
                   (BALABALA_REQUEST_NETWORK_START|BALABALA_REQUEST_NETWORK_CANCEL));
            unsigned revision=f.network.revision;
            step(&f);
            assert(f.network.status==BALABALA_NETWORK_FAILED && f.network.released && !f.network.active && !f.network.fault);
            assert(f.network.revision==revision+1 && f.runtime.network_revision==f.network.revision);
            assert(!(balabala_app_status(f.app)&BALABALA_NETWORK_OWNED));
            assert(!f.net_prepares && !f.net_starts && !f.net_releases);
            unsigned refreshes=f.refreshes;
            for(unsigned tick=0;tick<10;++tick) step(&f);
            assert(f.refreshes==refreshes && f.network.revision==revision+1);
        }
        drop(&f);
    }
}
static void test_rejected_start_keeps_unreleased_fault_owned(void) {
    Fake f; init(&f,true); enable_network(&f); open_network(&f); activate_network(&f);
    f.net_release_failure=true;
    tap(&f,BALABALA_HOME); step(&f);
    balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f);
    assert(f.network.fault && !f.network.released && f.network.active);
    unsigned revision=f.network.revision;
    f.runtime.requests|=BALABALA_REQUEST_NETWORK_START;
    step(&f);
    assert(f.network.revision==revision+1 && f.runtime.network_revision==f.network.revision);
    assert(balabala_app_status(f.app)&BALABALA_NETWORK_OWNED);
    assert(f.network.status==BALABALA_NETWORK_CLEANUP_FAILED && !f.network.released);
    assert(f.net_prepares==1 && f.net_starts==1 && f.net_releases==1);
    drop(&f);
}
static void test_cleanup_fault_cancels_shutdown_keeps_panel_and_blocks_aot_until_release(void) {
    for(unsigned deinit_failure=0;deinit_failure<2;++deinit_failure) for(unsigned success=0;success<2;++success) {
        Fake f; init(&f,true); enable_network(&f); open_network(&f); activate_network(&f);
        if(deinit_failure) f.net_deinit_error=-74; else f.net_cleanup_error=-73;
        if(success) balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_SUCCESS);
        balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END);
        tap(&f,BALABALA_SHUTDOWN); step(&f);
        assert(f.network.active && f.network.manager && f.network.cleanup_error && f.network.fault);
        assert(f.network.success==!!success && !f.runtime.shutdown && !f.runtime.shutdown_prepared);
        assert(!f.screen_sleeps && !f.sleeps && !f.net_restores && !f.net_releases);
        assert((balabala_app_status(f.app)&BALABALA_NETWORK_OWNED) && !f.network.name[0] && !f.network.pop[0]);
        uint32_t failed=hash(f.frame); unsigned saves=f.saves;
        assert(balabala_app_network_status(f.app,BALABALA_NETWORK_SUCCESS)==-1);
        unsigned deinits=f.net_deinits,refreshes=f.refreshes;
        for(unsigned i=0;i<25;++i) {
            balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f);
        }
        assert(f.refreshes==refreshes && f.net_deinits==deinits && !f.screen_sleeps && !f.sleeps);
        const BalabalaKey exits[]={BALABALA_HOME,BALABALA_FRONT3,BALABALA_FRONT4,BALABALA_SHUTDOWN};
        for(unsigned i=0;i<4;++i) { tap(&f,exits[i]); f.now+=10000; step(&f); }
        assert(hash(f.frame)==failed && f.net_deinits==deinits && f.saves==saves);
        assert((balabala_app_status(f.app)&BALABALA_NETWORK_OWNED) && !f.runtime.shutdown);
        f.net_cleanup_error=f.net_deinit_error=0;
        assert(!balabala_network_retry_cleanup(&f.network));
        assert(!f.network.active && f.network.fault && f.net_releases==1 && f.net_restores==!success);
        for(unsigned i=0;i<10;++i) step(&f);
        assert(!f.runtime.shutdown && !f.screen_sleeps && !f.sleeps); /* no replay of old request */
        tap(&f,BALABALA_SHUTDOWN); step(&f);
        assert(f.runtime.shutdown && f.runtime.shutdown_prepared && f.screen_sleeps==1);
        f.now+=30; step(&f); assert(f.sleeps==1); drop(&f);
    }
}

static void test_staged_save_failure_clean_no_write_and_prepare_faults(void) {
    for(unsigned failure=0;failure<3;++failure) {
        Fake f; init(&f,true); enable_network(&f); f.runtime.save_at=UINT64_MAX;
        open_network(&f); assert(balabala_app_status(f.app)&BALABALA_SAVE_DIRTY);
        char *before=snapshot(&f);
        f.save_failure=failure==0; f.unavailable=failure==1; f.save_without_ack=failure==2;
        step(&f);
        assert(f.saves==1 && !f.net_prepares && !f.net_starts && !f.network.active);
        assert(balabala_app_status(f.app)&BALABALA_SAVE_DIRTY);
        char *after=snapshot(&f); assert(!strcmp(before,after)); free(before); free(after);
        tap(&f,BALABALA_HOME); step(&f);
        assert(!(balabala_app_status(f.app)&BALABALA_NETWORK_OWNED)); drop(&f);
    }
    Fake f; init(&f,true); enable_network(&f); save(&f,f.app); f.unavailable=true;
    unsigned saves=f.saves; open_network(&f); activate_network(&f);
    assert(f.saves==saves); f.now=f.runtime.save_at+1; step(&f); assert(f.saves==saves);
    tap(&f,BALABALA_HOME); step(&f); balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f); drop(&f);
    for(unsigned retained=0;retained<2;++retained) {
        init(&f,true); enable_network(&f); open_network(&f);
        f.net_prepare_error=-92; f.net_release_failure=retained; step(&f); step(&f);
        assert(!f.net_starts && f.net_prepares==1 && f.net_releases==1);
        assert(f.network.released==!retained && f.network.active==!!retained);
        assert(!!(balabala_app_status(f.app)&BALABALA_NETWORK_OWNED)==!!retained);
        saves=f.saves;
        for(unsigned i=0;i<10;++i) { tap(&f,BALABALA_HOME); f.now+=10000; step(&f); }
        assert(f.net_releases==1 && f.saves==saves); drop(&f);
    }
}
static void test_staged_display_revision_retry_deadline_and_after_ack_cancel(void) {
    for(unsigned phase=1;phase<=2;++phase) {
        Fake f; init(&f,true); enable_network(&f); open_network(&f);
        if(phase==2) step(&f);
        unsigned prepares=f.net_prepares, starts=f.net_starts;
        f.fail_refreshes=f.refreshes+1; step(&f);
        assert(f.runtime.pending && f.net_prepares==prepares && f.net_starts==starts);
        uint64_t revision=f.runtime.revision; uint32_t frame_hash=hash(f.frame);
        f.now=f.runtime.retry_at; step(&f);
        assert(!f.runtime.pending && f.runtime.revision==revision && hash(f.frame)==frame_hash);
        assert((phase==1 && f.network.phase==2) || (phase==2 && f.net_starts==starts+1));
        tap(&f,BALABALA_HOME); step(&f);
        if(f.network.manager) { balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f); }
        drop(&f);
        init(&f,true); enable_network(&f); open_network(&f); if(phase==2) step(&f);
        f.cancel_after_ack=true; /* poll只在pending清除后触发，含20次连按。 */
        step(&f); assert(!f.net_starts && !f.network.active && f.network.released); drop(&f);
        init(&f,true); enable_network(&f); open_network(&f); if(phase==2) step(&f);
        f.fail_refreshes=f.refreshes+3; step(&f);
        for(unsigned i=1;i<3;++i) { f.now=f.runtime.retry_at; step(&f); }
        assert(f.runtime.display_fault && f.runtime.pending && !f.net_starts && f.network.released);
        assert(balabala_app_status(f.app)&BALABALA_DISPLAY_PENDING); drop(&f);
    }
    Fake f; init(&f,true); enable_network(&f); open_network(&f);
    uint64_t old; assert(!balabala_app_render(f.app,f.frame,BALABALA_FRAMEBUFFER_BYTES,f.stripe,BALABALA_STRIPE_PIXELS,&old));
    f.runtime.partial=balabala_app_refresh_partial(f.app)!=0;
    f.runtime.pending=true; f.runtime.revision=old; step(&f);
    assert(!f.net_prepares && f.network.phase==1 && f.runtime.network_frame_revision!=old);
    step(&f); assert(f.net_prepares==1 && !f.net_starts); f.now=f.network.deadline; step(&f);
    assert(!f.network.active && f.network.released && !f.net_starts && f.network.status==BALABALA_NETWORK_TIMEOUT); drop(&f);
}

static void open_download(Fake *f) {
    step(f); tap(f,BALABALA_HOME); step(f);
    enter_parent(f); step(f);
    for(unsigned i=0;i<4;++i) tap(f,BALABALA_DOWN);
    step(f); tap(f,BALABALA_FRONT4); step(f);
    tap(f,BALABALA_DOWN); step(f); tap(f,BALABALA_FRONT4);
    assert(balabala_app_status(f->app)&BALABALA_DOWNLOAD_ACTIVE);
}
static void test_download_order_after_release_and_return_locks_boot(void) {
    Fake f; init(&f,true); enable_network(&f); open_network(&f); activate_network(&f);
    tap(&f,BALABALA_HOME); step(&f);
    assert(f.network.active && !f.network.released && f.net_stops==1);
    balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f);
    assert(f.network.released && !f.network.active && f.net_deinits==1 && f.net_releases==1);
    open_download(&f); step(&f);
    assert(f.checks==1 && f.saves && f.entries==1 && f.runtime.download_blocked);
    assert(f.net_stops==1 && f.check_order<f.save_order && f.save_order<f.screen_order);
    step(&f); open_download(&f); step(&f);
    assert(f.entries==1 && f.checks==1 && !(balabala_app_status(f.app)&BALABALA_DOWNLOAD_ACTIVE)); drop(&f);
}
static void test_download_precheck_save_nosd_and_dirty_failures_never_enter(void) {
    for(unsigned failure=1;failure<=9;++failure) {
        Fake f; init(&f,true); open_download(&f);
        unsigned saves=f.saves;
        if(failure<=6) f.check_result=failure;
        if(failure==7) f.save_failure=true;
        if(failure==8) f.unavailable=true;
        if(failure==9) f.save_without_ack=true;
        step(&f);
        assert(f.checks==1 && !f.entries && !f.screen_sleeps && !f.runtime.download);
        assert(!(balabala_app_status(f.app)&BALABALA_DOWNLOAD_ACTIVE));
        if(failure<=6) assert(f.saves==saves);
        for(unsigned i=0;i<4;++i) step(&f);
        assert(!f.entries); drop(&f);
    }
}
static void test_download_rejects_owned_resources_without_render_save_or_lifecycle(void) {
    for(unsigned inactive=0;inactive<2;++inactive) {
        Fake f; init(&f,true); enable_network(&f); open_download(&f); start_network_direct(&f);
        if(inactive) { f.network.active=false; f.network.fault=true; }
        unsigned saves=f.saves,refreshes=f.refreshes; uint32_t previous=hash(f.frame);
        step(&f);
        assert(f.runtime.download_result==BALABALA_DOWNLOAD_NETWORK_FAILED && !f.runtime.download);
        for(unsigned i=0;i<20;++i) { f.now+=10000; step(&f); }
        assert(!f.checks && !f.entries && !f.screen_sleeps && f.saves==saves && f.refreshes==refreshes);
        assert(hash(f.frame)==previous && !f.net_stops && !f.net_deinits && !f.net_releases);
        drop(&f);
    }
}
static void cancel_download_in_barrier(Fake *f) {
    if(f->command!=0x20 || (f->cancel_in_sleep && f->control!=3)) return;
    f->hook=NULL;
    tap(f,(BalabalaKey)f->cancel_key);
}
static void test_download_cancel_shutdown_in_prompt_refresh_and_sleep(void) {
    const BalabalaKey keys[]={BALABALA_HOME,BALABALA_FRONT3,BALABALA_SHUTDOWN};
    for(unsigned sleep_phase=0;sleep_phase<2;++sleep_phase) for(unsigned i=0;i<3;++i) {
        Fake f; init(&f,true); enable_network(&f); open_download(&f);
        // 提示全刷与sleep均通过真实SSD1677的service采样。
        f.cancel_key=keys[i]; f.hook=cancel_download_in_barrier;
        f.cancel_in_sleep=!!sleep_phase;
        step(&f);
        assert(!f.entries && !f.runtime.download && f.screen_sleeps==sleep_phase);
        if(keys[i]==BALABALA_SHUTDOWN) { assert(f.runtime.shutdown); step(&f); assert(f.runtime.shutdown_prepared); }
        else assert(!f.runtime.shutdown);
        drop(&f);
    }
}
static void test_download_old_revision_and_failed_ack_never_commit(void) {
    Fake f; init(&f,true); open_download(&f);
    // 先制造一个旧准备页帧；平台完成该revision不能代替提示页。
    uint64_t old;
    assert(!balabala_app_render(f.app,f.frame,BALABALA_FRAMEBUFFER_BYTES,f.stripe,BALABALA_STRIPE_PIXELS,&old));
    f.runtime.partial=balabala_app_refresh_partial(f.app)!=0;
    f.runtime.pending=true; f.runtime.revision=old; f.runtime.attempts=0;
    step(&f);
    assert(!f.entries && !f.screen_sleeps && f.runtime.download==2 && f.runtime.download_revision!=old);
    step(&f); assert(f.entries==1); drop(&f);
    init(&f,true); open_download(&f); f.invalidate_ack=true; step(&f);
    assert(!f.entries && f.runtime.pending && f.runtime.attempts==1);
    for(unsigned i=1;i<3;++i) { f.now=f.runtime.retry_at; step(&f); }
    assert(f.runtime.display_fault && f.runtime.pending && !f.entries && !f.screen_sleeps);
    drop(&f);
}
static void test_download_display_retry_exhaustion_and_sleep_failure(void) {
    Fake f; init(&f,true); open_download(&f); f.fail_refreshes=f.refreshes+3; step(&f);
    for(unsigned i=1;i<3;++i) { f.now=f.runtime.retry_at; step(&f); }
    assert(f.runtime.display_fault && f.runtime.pending && !f.entries && !f.runtime.download);
    unsigned refreshes=f.refreshes;
    for(unsigned i=0;i<10;++i) { f.now+=5000; step(&f); }
    assert(f.refreshes==refreshes && !f.entries); drop(&f);
    init(&f,true); open_download(&f); f.screen_failure=true; step(&f);
    assert(f.screen_sleeps==1 && !f.entries && !f.runtime.download);
    for(unsigned i=0;i<4;++i) step(&f);
    assert(f.screen_sleeps==1 && !f.entries); drop(&f);
}
static void test_download_preexisting_display_fault_render_failure_and_no_sd(void) {
    Fake f; init(&f,true); open_download(&f); f.runtime.display_fault=true; step(&f);
    assert(!f.checks && !f.entries && !f.runtime.download); drop(&f);
    init(&f,true); open_download(&f); f.runtime.stripe=NULL; step(&f);
    assert(f.runtime.display_fault && !f.entries && !f.runtime.pending && !f.runtime.download); drop(&f);
    init(&f,false); open_download(&f); step(&f);
    assert(f.runtime.save_result==2 && !f.entries && !f.screen_sleeps && !f.runtime.download); drop(&f);
}
static void test_download_rejection_does_not_replay_after_network_release(void) {
    Fake f; init(&f,true); enable_network(&f); open_download(&f); start_network_direct(&f);
    step(&f); assert(!f.runtime.download && f.network.active && !f.net_stops && !f.checks);
    balabala_network_step(&f.network,BALABALA_REQUEST_NETWORK_CANCEL,f.now);
    balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_END); step(&f);
    assert(!f.network.active && f.network.released && !f.entries && !f.screen_sleeps);
    for(unsigned i=0;i<10;++i) step(&f);
    assert(!f.entries && !f.checks);
    open_download(&f); step(&f); assert(f.entries==1 && f.checks==1); drop(&f);
}
static void test_auto_sleep_deadline_save_full_ack_and_release(void) {
    Fake f; init(&f,true); open_quiz(&f);
    tap(&f,BALABALA_FRONT1); step(&f);
    f.now=f.runtime.last_activity_ms+BALABALA_AUTO_SLEEP_MS-1; step(&f);
    assert(!f.runtime.shutdown && !f.screen_sleeps && !f.sleeps);
    unsigned saves=f.saves;
    f.now++; step(&f);
    assert(f.runtime.automatic_shutdown && f.runtime.shutdown_prepared && f.saves==saves+1);
    assert(!(balabala_app_status(f.app)&BALABALA_SAVE_DIRTY) && !f.requested_partial);
    assert(f.runtime.shutdown_revision==f.runtime.revision && !f.runtime.pending);
    assert(f.screen_sleeps==1 && !f.sleeps);
    f.now+=BALABALA_SLEEP_RELEASE_MS; step(&f);
    assert(f.sleeps==1 && f.save_order<f.screen_order && f.screen_order<f.sleep_order);
    for(unsigned i=0;i<5;++i) step(&f);
    assert(f.sleeps==1 && f.saves==saves+1);
    drop(&f);
}
static void test_external_power_pauses_auto_sleep_and_unplug_restarts_deadline(void) {
    Fake f; init(&f,true); f.external_power=true; step(&f);
    for(unsigned i=0;i<3;++i) {
        f.now+=BALABALA_AUTO_SLEEP_MS; step(&f);
        assert(!f.runtime.shutdown && !f.screen_sleeps && !f.sleeps);
    }
    uint64_t disconnected_at=f.now;
    f.external_power=false; step(&f);
    f.now=disconnected_at+BALABALA_AUTO_SLEEP_MS-1; step(&f);
    assert(!f.runtime.shutdown && !f.screen_sleeps);
    f.now++; step(&f);
    assert(f.runtime.automatic_shutdown && f.runtime.shutdown_prepared);
    f.now+=BALABALA_SLEEP_RELEASE_MS; step(&f); assert(f.sleeps==1);
    drop(&f);
}
static void connect_during_sleep_frame(Fake *f) {
    if(!f->runtime.automatic_shutdown || !f->runtime.shutdown_revision) return;
    f->usb_sleep_revision=f->runtime.shutdown_revision;
    f->external_power=true; f->hook=NULL;
}
static void connect_during_screen_sleep(Fake *f) {
    if(f->screen_sleeps) connect_during_sleep_frame(f);
}
static void test_external_power_cancels_auto_sleep_before_and_after_display(void) {
    for(unsigned stage=0;stage<4;++stage) {
        Fake f; init(&f,true); step(&f);
        if(stage==0) f.fail_refreshes=f.refreshes+1;
        if(stage==2) f.hook=connect_during_sleep_frame;
        if(stage==3) f.hook=connect_during_screen_sleep;
        f.now=f.runtime.last_activity_ms+BALABALA_AUTO_SLEEP_MS; step(&f);
        if(stage<2) {
            assert(f.runtime.automatic_shutdown);
            if(stage==0) assert(f.runtime.pending && !f.screen_sleeps);
            else assert(f.runtime.shutdown_prepared && f.screen_sleeps==1);
            f.usb_sleep_revision=f.runtime.shutdown_revision;
            f.external_power=true; step(&f);
        }
        assert(f.usb_sleep_revision && f.external_power && !f.sleeps);
        assert(!f.runtime.shutdown && !f.runtime.automatic_shutdown && !f.runtime.pending);
        assert(!f.runtime.shutdown_started && !f.runtime.shutdown_prepared && !f.runtime.shutdown_revision);
        assert(balabala_app_display_complete(f.app,f.usb_sleep_revision)==-1);
        assert(balabala_app_sleep_cancel(f.app)==0);
        step(&f); assert(!(balabala_app_status(f.app)&(BALABALA_NEEDS_REFRESH | BALABALA_DISPLAY_PENDING)));
        uint64_t home=f.runtime.revision;
        tap(&f,BALABALA_DOWN); step(&f); assert(f.runtime.revision>home);
        f.now+=BALABALA_AUTO_SLEEP_MS*2; step(&f); assert(!f.runtime.shutdown && !f.sleeps);
        drop(&f);
    }
}
static void test_external_power_allows_manual_shutdown(void) {
    Fake f; init(&f,true); f.external_power=true; step(&f);
    tap(&f,BALABALA_SHUTDOWN); step(&f);
    assert(f.runtime.shutdown && !f.runtime.automatic_shutdown && f.runtime.shutdown_prepared);
    f.now+=BALABALA_SLEEP_RELEASE_MS; step(&f);
    assert(f.sleeps==1 && f.save_order<f.screen_order && f.screen_order<f.sleep_order);
    drop(&f);
}
static void test_auto_sleep_activity_hold_release_and_candidates(void) {
    Fake f; init(&f,true); step(&f);
    f.now+=BALABALA_AUTO_SLEEP_MS-1;
    // 首页返回短按不改变画面，但必须延后自动休眠。
    unsigned refreshes=f.refreshes;
    tap(&f,BALABALA_FRONT3); step(&f);
    assert(f.refreshes==refreshes && !f.runtime.shutdown);
    f.now++; step(&f); assert(!f.runtime.shutdown);
    emit(&f,BALABALA_FRONT3,true);
    f.now+=BALABALA_AUTO_SLEEP_MS+1; step(&f);
    assert(!f.runtime.shutdown);
    emit(&f,BALABALA_FRONT3,false); step(&f);
    f.now+=BALABALA_AUTO_SLEEP_MS-1; step(&f);
    assert(!f.runtime.shutdown);
    f.input.adc[0].candidate=0; f.now++; step(&f);
    assert(!f.runtime.shutdown);
    f.input.adc[0].candidate=-1; step(&f);
    f.now+=BALABALA_AUTO_SLEEP_MS-1; step(&f);
    assert(!f.runtime.shutdown);
    f.now++; step(&f); assert(f.runtime.automatic_shutdown);
    drop(&f);
}
static void test_auto_sleep_save_failure_no_sd_and_missing_ack_stay_awake(void) {
    for(unsigned fault=0;fault<3;++fault) {
        Fake f; init(&f,fault!=1); open_quiz(&f);
        tap(&f,BALABALA_FRONT1); step(&f);
        f.save_failure=fault==0; f.save_without_ack=fault==2;
        f.now=f.runtime.last_activity_ms+BALABALA_AUTO_SLEEP_MS; step(&f);
        assert(!f.runtime.shutdown && !f.runtime.automatic_shutdown && !f.runtime.shutdown_started);
        assert(!f.screen_sleeps && !f.sleeps && (balabala_app_status(f.app)&BALABALA_SAVE_DIRTY));
        unsigned saves=f.saves;
        step(&f); step(&f); assert(f.saves==saves && !f.screen_sleeps);
        // 自动休眠失败不移走当前轮次，按键仍能回桌面，手动长按关机仍可用。
        tap(&f,BALABALA_HOME); step(&f);
        assert(!f.runtime.shutdown);
        tap(&f,BALABALA_SHUTDOWN); step(&f);
        assert(f.runtime.shutdown && !f.runtime.automatic_shutdown && f.runtime.shutdown_prepared);
        f.now+=BALABALA_SLEEP_RELEASE_MS; step(&f); assert(f.sleeps==1);
        drop(&f);
    }
}
static void test_auto_sleep_blocks_input_errors_faults_and_disabled_callback(void) {
    for(unsigned fault=0;fault<5;++fault) {
        Fake f; init(&f,true); step(&f);
        if(fault==0) f.input_error=true;
        if(fault==1) f.power_high=false;
        if(fault==2) f.runtime.display_fault=true;
        if(fault==3) f.runtime.ops.input_idle=NULL;
        if(fault==4) f.input.power.candidate=0;
        f.now+=BALABALA_AUTO_SLEEP_MS*2; step(&f);
        assert(!f.runtime.shutdown && !f.screen_sleeps && !f.sleeps);
        f.input_error=false; f.power_high=true; f.runtime.display_fault=false;
        f.runtime.ops.input_idle=input_idle; f.input.power.candidate=1;
        f.now+=BALABALA_AUTO_SLEEP_MS-1; step(&f); assert(!f.runtime.shutdown);
        f.now++; step(&f); assert(f.runtime.automatic_shutdown);
        drop(&f);
    }
}
static void test_auto_sleep_refresh_retry_keeps_one_saved_transaction(void) {
    Fake f; init(&f,true); step(&f);
    f.fail_refreshes=f.refreshes+1;
    f.now=f.runtime.last_activity_ms+BALABALA_AUTO_SLEEP_MS; step(&f);
    unsigned saves=f.saves;
    assert(f.runtime.automatic_shutdown && f.runtime.pending && !f.screen_sleeps);
    assert(f.runtime.shutdown_revision==f.runtime.revision);
    f.now=f.runtime.retry_at-1; step(&f);
    assert(f.runtime.pending && !f.screen_sleeps && f.saves==saves);
    f.now=f.runtime.retry_at; step(&f);
    assert(f.runtime.shutdown_prepared && !f.runtime.pending && f.screen_sleeps==1 && f.saves==saves);
    f.now+=BALABALA_SLEEP_RELEASE_MS; step(&f);
    assert(f.sleeps==1 && !f.requested_partial);
    drop(&f);
}
static void test_auto_sleep_pending_refresh_restarts_inactivity_period(void) {
    Fake f; init(&f,true); step(&f);
    tap(&f,BALABALA_DOWN); f.fail_refreshes=f.refreshes+1; step(&f);
    assert(f.runtime.pending);
    f.now+=BALABALA_AUTO_SLEEP_MS; step(&f);
    assert(!f.runtime.pending && !f.runtime.shutdown && !f.sleeps);
    f.now=f.runtime.last_activity_ms+BALABALA_AUTO_SLEEP_MS-1; step(&f);
    assert(!f.runtime.shutdown);
    f.now++; step(&f); assert(f.runtime.automatic_shutdown);
    drop(&f);
}
static void test_auto_sleep_never_preempts_network_or_download(void) {
    for(unsigned owned=0;owned<4;++owned) {
        Fake f; init(&f,true); step(&f);
        enable_network(&f);
        if(owned==0) {
            open_network(&f); activate_network(&f);
            balabala_network_publish(&f.network,f.network.generation,BALABALA_NET_EVENT_SUCCESS);
            step(&f);
            assert(f.network.active && f.network.success);
        }
        if(owned==1) { f.network.fault=true; f.network.released=false; }
        if(owned==2) { f.network.fault=true; f.network.released=true; }
        if(owned==3) open_download(&f);
        f.now+=BALABALA_AUTO_SLEEP_MS*2; step(&f);
        assert(!f.runtime.automatic_shutdown && !f.sleeps);
        if(owned<3) assert(!f.screen_sleeps);
        drop(&f);
    }
}
int main(void) {
    unsigned passed=0;
#define RUN(test) do { test(); printf("ok %u - %s\n",++passed,#test); } while(0)
    RUN(test_external_power_pauses_auto_sleep_and_unplug_restarts_deadline);
    RUN(test_external_power_cancels_auto_sleep_before_and_after_display);
    RUN(test_external_power_allows_manual_shutdown);
    RUN(test_auto_sleep_deadline_save_full_ack_and_release);
    RUN(test_auto_sleep_activity_hold_release_and_candidates);
    RUN(test_auto_sleep_save_failure_no_sd_and_missing_ack_stay_awake);
    RUN(test_auto_sleep_blocks_input_errors_faults_and_disabled_callback);
    RUN(test_auto_sleep_pending_refresh_restarts_inactivity_period);
    RUN(test_auto_sleep_refresh_retry_keeps_one_saved_transaction);
    RUN(test_auto_sleep_never_preempts_network_or_download);
    RUN(test_storage_restore_faults_preserve_files_through_runtime_and_shutdown);
    RUN(test_rejected_start_after_restore_fault_releases_model_ownership);
    RUN(test_rejected_batched_start_cancel_twice_from_failed_exits);
    RUN(test_rejected_start_keeps_unreleased_fault_owned);
    RUN(test_staged_save_failure_clean_no_write_and_prepare_faults);
    RUN(test_staged_display_revision_retry_deadline_and_after_ack_cancel);
    RUN(test_download_preexisting_display_fault_render_failure_and_no_sd);
    RUN(test_download_rejection_does_not_replay_after_network_release);
    RUN(test_download_order_after_release_and_return_locks_boot);
    RUN(test_download_precheck_save_nosd_and_dirty_failures_never_enter);
    RUN(test_download_rejects_owned_resources_without_render_save_or_lifecycle);
    RUN(test_download_cancel_shutdown_in_prompt_refresh_and_sleep);
    RUN(test_download_old_revision_and_failed_ack_never_commit);
    RUN(test_download_display_retry_exhaustion_and_sleep_failure);
    RUN(test_cleanup_fault_cancels_shutdown_keeps_panel_and_blocks_aot_until_release);
    RUN(test_rotation_corners_checkerboard_and_asymmetry);
    RUN(test_driver_full_bytes_first_partial_and_valid_baseline);
    RUN(test_driver_partial_failures_force_full_retry);
    RUN(test_driver_sleep_invalidates_baseline);
    RUN(test_driver_partial_limit_restarts_after_full);
    RUN(test_partial_sync_after_busy_before_ack);
    RUN(test_runtime_partial_value_survives_failed_display_retry);
    RUN(test_full_sequence_and_only_success_ack);
    RUN(test_busy_release_home_and_latest_state_no_queue);
    RUN(test_new_question_gate_release_and_single_score);
    RUN(test_spi_failure_retries_same_frame_and_ack_after_success);
    RUN(test_timeout_bounded_fault_still_home_and_shutdown);
    RUN(test_shutdown_during_busy_save_failure_and_release_before_sleep);
    RUN(test_shutdown_collected_inside_real_display_wait);
    RUN(test_shutdown_refresh_retries_before_sleep_and_saves_once);
    RUN(test_shutdown_waits_for_preexisting_pending_then_acks_sleep_frame);
    RUN(test_shutdown_retry_exhaustion_still_allows_power_off);
    RUN(test_no_sd_builtin_learning_and_save_throttle);
    RUN(test_failed_save_retries_after_interval);
    RUN(test_network_request_reports_failed_without_platform_service);
    RUN(test_network_service_only_collects_and_step_owns_lifecycle);
    RUN(test_network_status_storm_has_no_frame_queue_and_shutdown_waits_end);
    RUN(test_front4_success_exit_abi_closes_entry_before_end);
    RUN(test_cancel_timeout_cleanup_failures_reach_real_rust_abi_page);
    RUN(test_reenter_during_cleanup_never_shows_old_identity_or_starts_in_parallel);
    printf("runtime tests: %u passed; 0 failed (C ASan/UBSan, real Rust ABI/display/runtime)\n",passed);
}
