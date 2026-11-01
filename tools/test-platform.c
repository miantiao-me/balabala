#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "balabala_input.h"
#include "balabala_storage.h"

typedef struct { BalabalaKey key; bool pressed; } Event;
typedef struct { Event events[128]; unsigned count; } Events;
static void capture(void *context, BalabalaKey key, bool pressed) {
    Events *e = context;
    assert(e->count < 128);
    e->events[e->count++] = (Event){key, pressed};
}
static void event_is(const Events *e, unsigned n, BalabalaKey key, bool pressed) {
    assert(n < e->count && e->events[n].key == key && e->events[n].pressed == pressed);
}
static void sample(BalabalaInput *i, Events *e, uint64_t now, int a, int b, int power) {
    balabala_input_sample(i, now, a, true, b, true, power, capture, e);
}
static void test_input_idle_waits_for_debounce_and_all_key_releases(void) {
    BalabalaInput i; Events e={0}; balabala_input_init(&i,NULL);
    assert(balabala_input_idle(&i));
    sample(&i,&e,0,3500,4095,1); assert(!balabala_input_idle(&i));
    sample(&i,&e,30,3500,4095,1); assert(!balabala_input_idle(&i));
    sample(&i,&e,40,4095,4095,1); assert(!balabala_input_idle(&i));
    sample(&i,&e,70,4095,4095,1); assert(balabala_input_idle(&i));
    sample(&i,&e,80,4095,2000,1); assert(!balabala_input_idle(&i));
    sample(&i,&e,110,4095,2000,1); assert(!balabala_input_idle(&i));
    sample(&i,&e,120,4095,4095,1); assert(!balabala_input_idle(&i));
    sample(&i,&e,150,4095,4095,1); assert(balabala_input_idle(&i));
    sample(&i,&e,160,4095,4095,0); assert(!balabala_input_idle(&i));
    sample(&i,&e,190,4095,4095,0); assert(!balabala_input_idle(&i));
    sample(&i,&e,200,4095,4095,1); assert(!balabala_input_idle(&i));
    sample(&i,&e,230,4095,4095,1); assert(balabala_input_idle(&i));
}
static void test_thresholds(void) {
    const int first[][2] = {{4095,-1},{3901,-1},{3900,0},{3101,0},{3100,1},{2091,1},{2090,2},{751,2},{750,3},{0,3},{-1,-2},{4096,-2}};
    const int second[][2] = {{4095,-1},{3901,-1},{3900,0},{1121,0},{1120,1},{0,1},{-1,-2},{4096,-2}};
    for (unsigned n = 0; n < sizeof(first)/sizeof(first[0]); ++n) assert(balabala_input_decode(0, first[n][0]) == first[n][1]);
    for (unsigned n = 0; n < sizeof(second)/sizeof(second[0]); ++n) assert(balabala_input_decode(1, second[n][0]) == second[n][1]);
    assert(balabala_input_decode(2, 0) == -2);
}
static void test_debounce_release_and_bank_independence(void) {
    BalabalaInput i; Events e = {0};
    assert(balabala_input_init(&i, NULL) == 0);
    sample(&i,&e,0,3500,4095,1);
    sample(&i,&e,10,4095,4095,1);
    sample(&i,&e,20,3500,4095,1);
    sample(&i,&e,49,3500,4095,1); assert(e.count == 0);
    sample(&i,&e,50,3500,4095,1); assert(e.count == 1);
    sample(&i,&e,60,3500,2000,1);
    sample(&i,&e,90,3500,2000,1); assert(e.count == 2);
    sample(&i,&e,100,2500,2000,1);
    sample(&i,&e,130,2500,2000,1); assert(e.count == 4);
    sample(&i,&e,140,4095,4095,1);
    sample(&i,&e,169,4095,4095,1); assert(e.count == 4);
    sample(&i,&e,170,4095,4095,1); assert(e.count == 6);
    event_is(&e,0,BALABALA_FRONT1,true); event_is(&e,1,BALABALA_UP,true);
    event_is(&e,2,BALABALA_FRONT1,false); event_is(&e,3,BALABALA_FRONT2,true);
    event_is(&e,4,BALABALA_FRONT2,false); event_is(&e,5,BALABALA_UP,false);
}
static void test_adc_errors_never_invent_press_or_release(void) {
    BalabalaInput i; Events e = {0}; balabala_input_init(&i,NULL);
    balabala_input_sample(&i,0,0,false,0,false,1,capture,&e);
    balabala_input_sample(&i,1000,-1,true,4096,true,1,capture,&e);
    assert(e.count == 0);
    sample(&i,&e,1010,0,4095,1);
    balabala_input_sample(&i,1040,0,false,4095,true,1,capture,&e);
    sample(&i,&e,1100,0,4095,1); assert(e.count == 0);
    sample(&i,&e,1130,0,4095,1); assert(e.count == 1);
    balabala_input_sample(&i,1200,4095,false,4095,true,1,capture,&e);
    balabala_input_sample(&i,2200,-20,true,4095,true,1,capture,&e); assert(e.count == 1);
    sample(&i,&e,2210,4095,4095,1);
    sample(&i,&e,2240,4095,4095,1); assert(e.count == 2);
    event_is(&e,0,BALABALA_FRONT4,true); event_is(&e,1,BALABALA_FRONT4,false);
}
static void test_power_short_and_bounce(void) {
    BalabalaInput i; Events e = {0}; balabala_input_init(&i,NULL);
    sample(&i,&e,0,4095,4095,0); sample(&i,&e,20,4095,4095,1);
    sample(&i,&e,60,4095,4095,1); assert(e.count == 0);
    sample(&i,&e,100,4095,4095,0); sample(&i,&e,130,4095,4095,0);
    sample(&i,&e,1000,4095,4095,1); sample(&i,&e,1029,4095,4095,1); assert(e.count == 0);
    sample(&i,&e,1030,4095,4095,1); assert(e.count == 2);
    event_is(&e,0,BALABALA_HOME,true); event_is(&e,1,BALABALA_HOME,false);
}
static void test_power_long_exactly_once_without_home(void) {
    BalabalaInput i; Events e = {0}; balabala_input_init(&i,NULL);
    sample(&i,&e,0,4095,4095,0); sample(&i,&e,30,4095,4095,0);
    sample(&i,&e,1529,4095,4095,0); assert(e.count == 0);
    sample(&i,&e,1530,4095,4095,0); assert(e.count == 2);
    sample(&i,&e,2000,4095,4095,0); sample(&i,&e,2100,4095,4095,1);
    sample(&i,&e,2130,4095,4095,1); assert(e.count == 2);
    event_is(&e,0,BALABALA_SHUTDOWN,true); event_is(&e,1,BALABALA_SHUTDOWN,false);
}
static void test_power_release_threshold_and_sparse_sampling(void) {
    for (unsigned long_press = 0; long_press < 2; ++long_press) {
        BalabalaInput i; Events e = {0}; balabala_input_init(&i,NULL);
        sample(&i,&e,0,4095,4095,0); sample(&i,&e,30,4095,4095,0);
        uint64_t release = long_press ? 1530 : 1529;
        sample(&i,&e,release,4095,4095,1); assert(e.count == 0);
        sample(&i,&e,release+30,4095,4095,1); assert(e.count == 2);
        event_is(&e,0,long_press ? BALABALA_SHUTDOWN : BALABALA_HOME,true);
    }
}
static void test_parent_hold_threshold_once_and_sparse_release(void) {
    BalabalaInput i; Events e={0}; balabala_input_init(&i,NULL);
    sample(&i,&e,0,1700,4095,1); sample(&i,&e,30,1700,4095,1);
    sample(&i,&e,2029,1700,4095,1); assert(e.count==1);
    balabala_input_sample(&i,2030,1700,false,4095,true,1,capture,&e);
    assert(e.count==1);
    sample(&i,&e,2040,1700,4095,1); assert(e.count==3);
    sample(&i,&e,5000,1700,4095,1); assert(e.count==3);
    sample(&i,&e,5010,4095,4095,1); sample(&i,&e,5040,4095,4095,1);
    assert(e.count==4);
    event_is(&e,0,BALABALA_FRONT3,true); event_is(&e,1,BALABALA_PARENT,true);
    event_is(&e,2,BALABALA_PARENT,false); event_is(&e,3,BALABALA_FRONT3,false);
    for (unsigned reached=0;reached<2;++reached) {
        balabala_input_init(&i,NULL); e.count=0;
        sample(&i,&e,0,1700,4095,1); sample(&i,&e,30,1700,4095,1);
        uint64_t release=reached?2030:2029;
        sample(&i,&e,release,4095,4095,1); sample(&i,&e,release+30,4095,4095,1);
        assert(e.count==(reached?4u:2u));
        if(reached) event_is(&e,1,BALABALA_PARENT,true);
        event_is(&e,e.count-1,BALABALA_FRONT3,false);
    }
}
static void test_mapping_is_configurable(void) {
    BalabalaInput i; Events e = {0};
    BalabalaInputConfig c = balabala_input_default_config;
    c.adc1_keys[0] = BALABALA_FRONT4; c.adc1_keys[3] = BALABALA_FRONT1;
    assert(balabala_input_init(&i,&c) == 0);
    sample(&i,&e,0,3500,4095,1); sample(&i,&e,30,3500,4095,1);
    event_is(&e,0,BALABALA_FRONT4,true);
    c.adc1_keys[0] = c.adc1_keys[1]; assert(balabala_input_init(&i,&c) == -1);
}
static void ready(BalabalaApp *app) {
    uint8_t *fb = malloc(BALABALA_FRAMEBUFFER_BYTES);
    uint16_t *stripe = malloc(BALABALA_STRIPE_PIXELS*sizeof(*stripe));
    uint64_t revision = 0; assert(fb && stripe);
    int result = balabala_app_render(app,fb,BALABALA_FRAMEBUFFER_BYTES,stripe,BALABALA_STRIPE_PIXELS,&revision);
    assert(result == 0 || result == 1);
    if (result == 0) assert(balabala_app_display_complete(app,revision) == 0);
    free(fb); free(stripe);
}
static void tap(BalabalaApp *app, BalabalaKey key) {
    ready(app);
    assert(balabala_app_key(app,key,1) == 0); assert(balabala_app_key(app,key,0) == 0);
    ready(app);
}
static char *snapshot(BalabalaApp *app, uint32_t *target, uint64_t *gen) {
    uint8_t *bytes = malloc(BALABALA_MAX_SAVE_BYTES); assert(bytes);
    int n = balabala_app_save_export(app,bytes,BALABALA_MAX_SAVE_BYTES,target,gen); assert(n > 24);
    size_t start = bytes[4] == 2 ? 28 : 24;
    size_t length = bytes[4] == 2 ? (size_t)bytes[24] | (size_t)bytes[25]<<8 | (size_t)bytes[26]<<16 | (size_t)bytes[27]<<24 : (size_t)n-24;
    assert(start+length <= (size_t)n);
    char *json = malloc(length+1); assert(json);
    memcpy(json,bytes+start,length); json[length] = 0;
    assert(balabala_app_save_ack(app,*gen,0) == 0); free(bytes);
    return json;
}
static void test_input_bridges_real_app_and_releases_while_busy(void) {
    BalabalaApp *app = balabala_app_create(); ready(app);
    BalabalaInput i; balabala_input_init(&i,NULL);
    balabala_input_sample(&i,0,4095,true,0,true,1,balabala_input_to_app,app);
    balabala_input_sample(&i,30,4095,true,0,true,1,balabala_input_to_app,app);
    assert(balabala_app_status(app)&BALABALA_NEEDS_REFRESH);
    balabala_input_sample(&i,40,4095,true,4095,true,1,balabala_input_to_app,app);
    balabala_input_sample(&i,70,4095,true,4095,true,1,balabala_input_to_app,app);
    ready(app);
    for (unsigned n=0;n<2;++n) {
        uint64_t t=100+n*100;
        balabala_input_sample(&i,t,0,true,4095,true,1,balabala_input_to_app,app);
        balabala_input_sample(&i,t+30,0,true,4095,true,1,balabala_input_to_app,app);
        balabala_input_sample(&i,t+40,4095,true,4095,true,1,balabala_input_to_app,app);
        balabala_input_sample(&i,t+70,4095,true,4095,true,1,balabala_input_to_app,app);
        ready(app);
    }
    uint32_t slot; uint64_t gen; char *json = snapshot(app,&slot,&gen);
    assert(strstr(json,"\"mode\":\"Read\"")); free(json); balabala_app_destroy(app);
}

typedef enum { NONE, SHORT_WRITE, VERIFY_OPEN, VERIFY_READ, VERIFY_MISMATCH, VERIFY_EXTRA,
               FAIL_FLUSH, FAIL_SYNC, FAIL_CLOSE, SLOT0_OPEN, SLOT0_READ, SLOT1_OPEN } Fault;
typedef struct {
    Fault fault;
    bool written, writing, clobber_errno;
    FILE *bad_read;
    char order[256]; unsigned calls;
    size_t maximum_read;
} Faults;
static void log_call(Faults *f, char c) { assert(f->calls+1<sizeof(f->order)); f->order[f->calls++]=c; }
static FILE *fault_open(const char *path,const char *mode,void *context) {
    Faults *f=context; bool read=mode[0]=='r';
    if (read && ((f->fault==VERIFY_OPEN && f->written) || (f->fault==SLOT0_OPEN && strstr(path,"slot0.bin")) ||
                 (f->fault==SLOT1_OPEN && strstr(path,"slot1.bin")))) {
        errno=EACCES; return NULL;
    }
    FILE *file=fopen(path,mode);
    f->writing=!read;
    if (!read) log_call(f,'O'); else if(f->written) log_call(f,'V');
    if (read && f->fault==SLOT0_READ && strstr(path,"slot0.bin")) f->bad_read=file;
    return file;
}
static size_t fault_read(void *out,size_t bytes,FILE *file,void *context) {
    Faults *f=context;
    if(bytes>f->maximum_read) f->maximum_read=bytes;
    if(f->written) log_call(f,'R');
    if ((f->fault==VERIFY_READ && f->written) || file==f->bad_read) { errno=EIO; return 0; }
    size_t n=fread(out,1,bytes,file);
    if(f->fault==VERIFY_MISMATCH && f->written && n) ((uint8_t *)out)[0]^=1;
    return n;
}
static size_t fault_write(const void *data,size_t bytes,FILE *file,void *context) {
    Faults *f=context; log_call(f,'W'); f->written=true;
    size_t n=fwrite(data,1,f->fault==SHORT_WRITE ? bytes/2 : bytes,file);
    if(f->fault==VERIFY_EXTRA) assert(fputc('x',file)!=EOF);
    return n;
}
static int fault_flush(FILE *file,void *context) {
    Faults *f=context; log_call(f,'F');
    if(f->fault==FAIL_FLUSH) { errno=EIO; return EOF; } return fflush(file);
}
static int fault_sync(int fd,void *context) {
    Faults *f=context; log_call(f,'S');
    if(f->fault==FAIL_SYNC) { errno=EIO; return -1; } return fsync(fd);
}
static int fault_close(FILE *file,void *context) {
    Faults *f=context; bool writing=f->writing;
    if(f->written) log_call(f,'C');
    if(file==f->bad_read) f->bad_read=NULL;
    int result=fclose(file);
    if(f->clobber_errno && result==0) errno=EINVAL;
    if(f->fault==FAIL_CLOSE && writing) { errno=EIO; return EOF; } return result;
}
static BalabalaStorageIo fault_io(Faults *f) {
    return (BalabalaStorageIo){fault_open,fault_read,fault_write,fault_flush,fault_sync,fault_close,f};
}
static void root_new(char *root) {
    strcpy(root,".build/platform-test-XXXXXX"); assert(mkdtemp(root));
}
static void root_path(char *out,const char *root,const char *relative) {
    int n=snprintf(out,256,"%s/balabala/%s",root,relative); assert(n>0 && n<256);
}
static void dirs(const char *root) {
    char p[256]; root_path(p,root,""); assert(mkdir(p,0775)==0 || errno==EEXIST);
    root_path(p,root,"decks"); assert(mkdir(p,0775)==0 || errno==EEXIST);
    root_path(p,root,"saves"); assert(mkdir(p,0775)==0 || errno==EEXIST);
}
static void put(const char *root,const char *name,const void *data,size_t n) {
    char p[256]; root_path(p,root,name); FILE *f=fopen(p,"wb"); assert(f);
    assert(fwrite(data,1,n,f)==n); assert(fclose(f)==0);
}
static uint8_t *get(const char *root,const char *name,size_t *n) {
    char p[256]; root_path(p,root,name); FILE *f=fopen(p,"rb"); assert(f);
    assert(fseek(f,0,SEEK_END)==0); long length=ftell(f); assert(length>=0); rewind(f);
    *n=(size_t)length; uint8_t *data=malloc(*n+1); assert(data);
    assert(fread(data,1,*n,f)==*n); data[*n]=0; assert(fclose(f)==0); return data;
}
static void root_drop(const char *root) {
    static const char *names[]={"decks/hanzi.json","decks/pinyin.json","decks/english.json","decks/math.json","saves/slot0.bin","saves/slot1.bin","curriculum/course.bin"};
    char p[256];
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i) { root_path(p,root,names[i]); if(unlink(p)!=0) assert(errno==ENOENT); }
    root_path(p,root,"curriculum"); if(rmdir(p)!=0) assert(errno==ENOENT);
    root_path(p,root,"decks"); if(rmdir(p)!=0) assert(errno==ENOENT);
    root_path(p,root,"saves"); if(rmdir(p)!=0) assert(errno==ENOENT);
    root_path(p,root,""); if(rmdir(p)!=0) assert(errno==ENOENT);
    assert(rmdir(root)==0);
}
static void toggle_count(BalabalaApp *app) {
    tap(app,BALABALA_HOME);
    assert(balabala_app_key(app,BALABALA_FRONT3,1)==0);
    tap(app,BALABALA_PARENT);
    assert(balabala_app_key(app,BALABALA_FRONT3,0)==0);
    tap(app,BALABALA_FRONT4);
    tap(app,BALABALA_FRONT4);
}
static void export_protected(BalabalaApp *app) {
    uint8_t *data=malloc(BALABALA_MAX_SAVE_BYTES); assert(data);
    uint32_t slot=9; uint64_t generation=99;
    assert(balabala_app_save_export(app,data,BALABALA_MAX_SAVE_BYTES,&slot,&generation)==-3);
    assert(slot==9 && generation==99 && (balabala_app_status(app)&BALABALA_SAVE_DIRTY));
    free(data);
}
static void test_unavailable_and_first_boot_do_not_show_corruption(void) {
    char root[80]; root_new(root); char missing[128]; snprintf(missing,sizeof(missing),"%s/unmounted",root);
    BalabalaApp *app=balabala_app_create(); ready(app); uint32_t status=balabala_app_status(app);
    BalabalaStorage s; assert(balabala_storage_init(&s,missing,NULL)==0);
    assert(balabala_storage_import_decks(&s,app)==1 && s.report.status==BALABALA_STORAGE_UNAVAILABLE);
    assert(balabala_storage_show_import_error(&s,app)==0);
    assert(balabala_storage_restore(&s,app)==1 && s.report.status==BALABALA_STORAGE_UNAVAILABLE);
    assert(balabala_storage_save(&s,app)==1 && s.report.status==BALABALA_STORAGE_UNAVAILABLE);
    assert(balabala_app_status(app)==status);
    assert(balabala_storage_init(&s,root,NULL)==0);
    assert(balabala_storage_import_decks(&s,app)==0);
    assert(balabala_storage_show_import_error(&s,app)==0);
    for(unsigned n=0;n<4;++n) assert(s.report.decks[n]==BALABALA_FILE_ABSENT);
    assert(balabala_storage_restore(&s,app)==0 && s.report.status==BALABALA_STORAGE_FIRST_BOOT);
    assert(balabala_app_status(app)==status && s.buffer_bytes==0);
    export_protected(app); /* C重新init和双ENOENT不能解除先前未知root的保护。 */
    assert(balabala_storage_save(&s,app)==1 && s.report.app_result==-3);
    balabala_app_destroy(app);
    app=balabala_app_create();
    assert(balabala_storage_restore(&s,app)==0 && s.report.status==BALABALA_STORAGE_FIRST_BOOT);
    assert(balabala_storage_save(&s,app)==0);
    balabala_app_destroy(app); root_drop(root);
}
static void test_single_empty_or_bad_slot_is_recoverable_error(void) {
    for(unsigned empty=0;empty<2;++empty) {
        char root[80]; root_new(root); dirs(root);
        put(root,"saves/slot0.bin","broken",empty ? 0 : 6);
        BalabalaApp *app=balabala_app_create(); ready(app); BalabalaStorage s;
        balabala_storage_init(&s,root,NULL);
        assert(balabala_storage_restore(&s,app)==1 && s.report.status==BALABALA_STORAGE_DAMAGED);
        assert(s.report.slots[0]==BALABALA_FILE_READ && s.report.slots[1]==BALABALA_FILE_ABSENT);
        assert(balabala_app_status(app)&BALABALA_NEEDS_REFRESH);
        ready(app); tap(app,BALABALA_FRONT4); assert(!(balabala_app_status(app)&BALABALA_NEEDS_REFRESH));
        assert(s.buffer_bytes==0); balabala_app_destroy(app); root_drop(root);
    }
}
static void test_slot_io_error_is_not_first_boot(void) {
    char root[80]; root_new(root); dirs(root);
    Faults f={.fault=SLOT0_OPEN}; BalabalaStorageIo io=fault_io(&f); BalabalaStorage s;
    BalabalaApp *app=balabala_app_create(); ready(app); balabala_storage_init(&s,root,&io);
    assert(balabala_storage_restore(&s,app)==1 && s.report.status==BALABALA_STORAGE_DAMAGED);
    assert(s.report.slots[0]==BALABALA_FILE_IO_ERROR && s.report.slots[1]==BALABALA_FILE_ABSENT);
    assert(s.report.last_errno==EACCES); balabala_app_destroy(app); root_drop(root);
}
static void test_save_flush_sync_verify_ack_and_clean(void) {
    char root[80]; root_new(root); Faults f={0}; BalabalaStorageIo io=fault_io(&f);
    BalabalaStorage s; balabala_storage_init(&s,root,&io); BalabalaApp *app=balabala_app_create();
    assert(balabala_storage_save(&s,app)==0 && s.report.status==BALABALA_STORAGE_SAVED);
    assert(strncmp(f.order,"OWFSCV",6)==0); assert(f.maximum_read<=BALABALA_STORAGE_VERIFY_BYTES);
    assert(!(balabala_app_status(app)&BALABALA_SAVE_DIRTY));
    assert(s.report.peak_buffer_bytes==BALABALA_MAX_SAVE_BYTES && s.buffer_bytes==0);
    unsigned calls=f.calls; assert(balabala_storage_save(&s,app)==0 && s.report.status==BALABALA_STORAGE_CLEAN);
    assert(f.calls==calls); balabala_app_destroy(app); root_drop(root);
}
static void test_save_failures_preserve_dirty_and_old_slot(void) {
    const Fault failures[]={SHORT_WRITE,VERIFY_OPEN,VERIFY_READ,VERIFY_MISMATCH,VERIFY_EXTRA,FAIL_FLUSH,FAIL_SYNC,FAIL_CLOSE};
    const char *operations[]={"write","verify-open","verify-read","verify-read","verify-read","flush","sync","close"};
    for(unsigned n=0;n<sizeof(failures)/sizeof(failures[0]);++n) {
        char root[80]; root_new(root); BalabalaStorage s; balabala_storage_init(&s,root,NULL);
        BalabalaApp *app=balabala_app_create(); assert(balabala_storage_save(&s,app)==0);
        size_t old_n; uint8_t *old=get(root,"saves/slot0.bin",&old_n);
        toggle_count(app);
        Faults f={.fault=failures[n],.clobber_errno=true}; BalabalaStorageIo io=fault_io(&f); balabala_storage_init(&s,root,&io);
        assert(balabala_storage_save(&s,app)==1 && s.report.status==BALABALA_STORAGE_SAVE_FAILED);
        assert(strcmp(s.report.operation,operations[n])==0);
        assert(s.report.last_errno==(failures[n]==VERIFY_OPEN ? EACCES : EIO));
        assert(balabala_app_status(app)&BALABALA_SAVE_DIRTY); assert(s.buffer_bytes==0);
        size_t after_n; uint8_t *after=get(root,"saves/slot0.bin",&after_n);
        assert(old_n==after_n && memcmp(old,after,old_n)==0); free(old); free(after);
        balabala_storage_init(&s,root,NULL); assert(balabala_storage_save(&s,app)==0);
        assert(!(balabala_app_status(app)&BALABALA_SAVE_DIRTY));
        balabala_app_destroy(app); root_drop(root);
    }
}
static void test_restore_highest_generation_and_next_inactive_slot(void) {
    char root[80]; root_new(root); BalabalaStorage s; balabala_storage_init(&s,root,NULL);
    BalabalaApp *app=balabala_app_create(); assert(balabala_storage_save(&s,app)==0);
    toggle_count(app); assert(balabala_storage_save(&s,app)==0); balabala_app_destroy(app);
    app=balabala_app_create(); assert(balabala_storage_restore(&s,app)==0 && s.report.status==BALABALA_STORAGE_RESTORED);
    uint32_t slot; uint64_t gen; char *json=snapshot(app,&slot,&gen);
    assert(slot==0 && gen==3 && strstr(json,"\"count\":10")); free(json);
    assert(s.report.peak_buffer_bytes<=2*BALABALA_MAX_SAVE_BYTES && s.buffer_bytes==0);
    assert(balabala_storage_save(&s,app)==0);
    balabala_app_destroy(app); root_drop(root);
}
static void test_known_bad_slot_allows_fallback_but_unread_slot_protects(void) {
    for(unsigned failure=0;failure<5;++failure) {
        char root[80]; root_new(root); BalabalaStorage s; balabala_storage_init(&s,root,NULL);
        BalabalaApp *app=balabala_app_create(); assert(balabala_storage_save(&s,app)==0);
        toggle_count(app); assert(balabala_storage_save(&s,app)==0); balabala_app_destroy(app);
        Faults f={0}; BalabalaStorageIo io=fault_io(&f);
        if(failure==0) put(root,"saves/slot0.bin","bad",3);
        if(failure==1) {
            uint8_t *large=calloc(1,BALABALA_MAX_SAVE_BYTES+1); assert(large);
            put(root,"saves/slot0.bin",large,BALABALA_MAX_SAVE_BYTES+1); free(large);
        }
        if(failure==2) f.fault=SLOT0_OPEN;
        if(failure==3) f.fault=SLOT0_READ;
        if(failure==4) f.fault=SLOT1_OPEN;
        balabala_storage_init(&s,root,&io); app=balabala_app_create();
        int result=balabala_storage_restore(&s,app);
        BalabalaFileStatus expected=failure==0 ? BALABALA_FILE_READ : failure==1 ? BALABALA_FILE_TOO_LARGE : BALABALA_FILE_IO_ERROR;
        assert(s.report.slots[0]==(failure==4 ? BALABALA_FILE_READ : expected));
        assert(s.report.slots[1]==(failure==4 ? BALABALA_FILE_IO_ERROR : BALABALA_FILE_READ));
        if(failure<2) {
            assert(result==0 && s.report.status==BALABALA_STORAGE_RESTORED);
            uint32_t slot; uint64_t gen; char *json=snapshot(app,&slot,&gen);
            assert(slot==0 && gen==3 && strstr(json,"\"count\":10")); free(json);
        } else {
            // IO未读不等于CRC/大小已确认无效，另一槽不能证明最高代数。
            assert(result==1 && s.report.status==BALABALA_STORAGE_DAMAGED);
            export_protected(app);
            balabala_storage_init(&s,root,NULL);
            toggle_count(app); export_protected(app);
            assert(balabala_storage_save(&s,app)==1 && s.report.app_result==-3);
            assert(balabala_storage_restore(&s,app)==0);
            uint32_t slot; uint64_t gen; char *json=snapshot(app,&slot,&gen);
            assert(slot==0 && gen==3 && strstr(json,"\"count\":10")); free(json);
        }
        assert(s.buffer_bytes==0); balabala_app_destroy(app); root_drop(root);
    }
}
static void test_deck_files_are_bounded_sequential_and_atomic(void) {
    char root[80]; root_new(root); dirs(root);
    static const char *subjects[]={"hanzi","pinyin","english","math"}; size_t maximum=0;
    for(unsigned i=0;i<4;++i) {
        char source[80]; snprintf(source,sizeof(source),"content/%s.json",subjects[i]);
        FILE *f=fopen(source,"rb"); assert(f); assert(fseek(f,0,SEEK_END)==0); long n=ftell(f); assert(n>0); rewind(f);
        uint8_t *data=malloc((size_t)n); assert(data && fread(data,1,(size_t)n,f)==(size_t)n); fclose(f);
        char dest[80]; snprintf(dest,sizeof(dest),"decks/%s.json",subjects[i]); put(root,dest,data,(size_t)n); free(data);
        if((size_t)n>maximum) maximum=(size_t)n;
    }
    BalabalaStorage s; balabala_storage_init(&s,root,NULL); BalabalaApp *app=balabala_app_create();
    assert(balabala_storage_import_decks(&s,app)==0);
    assert(s.report.peak_buffer_bytes==maximum && s.report.deck_input_max_bytes==maximum && s.buffer_bytes==0);
    tap(app,BALABALA_DOWN); tap(app,BALABALA_FRONT4); tap(app,BALABALA_DOWN); tap(app,BALABALA_FRONT4); tap(app,BALABALA_FRONT1);
    uint32_t slot; uint64_t gen; char *before=snapshot(app,&slot,&gen); assert(strstr(before,"\"status\":2"));
    put(root,"decks/hanzi.json","{broken",7);
    uint8_t *large=calloc(1,BALABALA_MAX_DECK_BYTES+1); assert(large);
    put(root,"decks/pinyin.json",large,BALABALA_MAX_DECK_BYTES+1); free(large);
    assert(balabala_storage_import_decks(&s,app)==1);
    assert(s.report.decks[0]==BALABALA_FILE_CONTENT_REJECTED && s.report.decks[1]==BALABALA_FILE_TOO_LARGE);
    char *after=snapshot(app,&slot,&gen); assert(strcmp(before,after)==0); free(before); free(after);
    assert(s.buffer_bytes==0 && s.report.peak_buffer_bytes<=BALABALA_MAX_DECK_BYTES);
    ready(app); tap(app,BALABALA_FRONT4); balabala_app_destroy(app); root_drop(root);
    printf("  import input peak=%zu bytes; old/new Rust content overlap remains separately budgeted\n",maximum);
}
static void test_reboot_restores_round_answers_and_progress(void) {
    char root[80]; root_new(root); BalabalaStorage s; balabala_storage_init(&s,root,NULL);
    BalabalaApp *app=balabala_app_create();
    tap(app,BALABALA_DOWN); tap(app,BALABALA_FRONT4); tap(app,BALABALA_DOWN); tap(app,BALABALA_FRONT4); tap(app,BALABALA_FRONT1);
    uint32_t slot; uint64_t gen; char *before=snapshot(app,&slot,&gen);
    assert(strstr(before,"\"answers\":[1,") && strstr(before,"\"status\":2"));
    assert(balabala_storage_save(&s,app)==0); balabala_app_destroy(app);
    app=balabala_app_create(); assert(balabala_storage_restore(&s,app)==0);
    char *after=snapshot(app,&slot,&gen); assert(strcmp(before,after)==0); free(before); free(after);
    tap(app,BALABALA_FRONT4); tap(app,BALABALA_FRONT1);
    assert(balabala_storage_save(&s,app)==0);
    balabala_app_destroy(app); root_drop(root);
}
static FILE *deny_deck_open(const char *path, const char *mode, void *context) {
    (void)context;
    if (strstr(path,"/decks/hanzi.json")) { errno=EACCES; return NULL; }
    return fopen(path,mode);
}
static void test_startup_deck_read_error_survives_restore_and_exits_to_saved_round(void) {
    for(unsigned io_failure=0;io_failure<2;++io_failure) {
        char root[80]; root_new(root); dirs(root);
        BalabalaStorage s; balabala_storage_init(&s,root,NULL);
        BalabalaApp *app=balabala_app_create();
        tap(app,BALABALA_DOWN); tap(app,BALABALA_FRONT4); tap(app,BALABALA_DOWN);
        tap(app,BALABALA_FRONT4); tap(app,BALABALA_FRONT1);
        assert(balabala_storage_save(&s,app)==0); balabala_app_destroy(app);
        if(!io_failure) {
            uint8_t *large=calloc(1,BALABALA_MAX_DECK_BYTES+1); assert(large);
            put(root,"decks/hanzi.json",large,BALABALA_MAX_DECK_BYTES+1); free(large);
        }
        BalabalaStorageIo io={.open=deny_deck_open};
        balabala_storage_init(&s,root,io_failure ? &io : NULL);
        app=balabala_app_create(); ready(app);
        assert(balabala_storage_import_decks(&s,app)==1);
        assert(s.report.decks[0]==(io_failure ? BALABALA_FILE_IO_ERROR : BALABALA_FILE_TOO_LARGE));
        assert(!(balabala_app_status(app)&BALABALA_NEEDS_REFRESH));
        assert(balabala_storage_show_import_error(&s,app)==1);
        assert(balabala_app_status(app)&BALABALA_NEEDS_REFRESH);
        assert(balabala_storage_restore(&s,app)==0 && s.report.status==BALABALA_STORAGE_RESTORED);
        assert(balabala_app_status(app)&BALABALA_NEEDS_REFRESH);
        uint32_t slot; uint64_t gen; char *json=snapshot(app,&slot,&gen);
        assert(strstr(json,"\"answers\":[1,") && strstr(json,"\"status\":2")); free(json);
        tap(app,BALABALA_FRONT4); /* 可退出错误页，然后桌面继续原轮次。 */
        tap(app,BALABALA_FRONT4);
        json=snapshot(app,&slot,&gen); assert(strstr(json,"\"answers\":[1,")); free(json);
        balabala_app_destroy(app); root_drop(root);
    }
}
static void test_startup_content_rejected_then_valid_deck_keeps_error_and_restore(void) {
    char root[80];root_new(root);dirs(root);BalabalaStorage s;balabala_storage_init(&s,root,NULL);
    BalabalaApp *app=balabala_app_create();
    tap(app,BALABALA_DOWN);tap(app,BALABALA_FRONT4);tap(app,BALABALA_DOWN);tap(app,BALABALA_FRONT4);tap(app,BALABALA_FRONT1);
    assert(balabala_storage_save(&s,app)==0);balabala_app_destroy(app);
    put(root,"decks/hanzi.json","{broken",7);
    FILE *file=fopen("content/pinyin.json","rb");assert(file);assert(fseek(file,0,SEEK_END)==0);long n=ftell(file);assert(n>0);rewind(file);
    uint8_t *data=malloc((size_t)n);assert(data && fread(data,1,(size_t)n,file)==(size_t)n);fclose(file);
    put(root,"decks/pinyin.json",data,(size_t)n);free(data);
    app=balabala_app_create();ready(app);
    assert(balabala_storage_import_decks(&s,app)==1);
    assert(s.report.decks[0]==BALABALA_FILE_CONTENT_REJECTED && s.report.decks[1]==BALABALA_FILE_READ);
    assert(balabala_storage_show_import_error(&s,app)==0);
    assert(balabala_storage_restore(&s,app)==0);
    tap(app,BALABALA_FRONT4);tap(app,BALABALA_FRONT4);
    uint32_t slot;uint64_t gen;char *json=snapshot(app,&slot,&gen);
    assert(strstr(json,"\"answers\":[1,") && strstr(json,"\"status\":2"));free(json);
    balabala_app_destroy(app);root_drop(root);
}
static void test_curriculum_streams_verifies_and_preserves_save_when_missing(void) {
    size_t length; uint8_t *pack=get(".build/sdcard","curriculum/course.bin",&length);
    char root[80],path[256]; root_new(root); dirs(root);
    root_path(path,root,"curriculum"); assert(mkdir(path,0775)==0);
    put(root,"curriculum/course.bin",pack,length);
    Faults faults={0}; BalabalaStorageIo io=fault_io(&faults);
    BalabalaStorage storage; assert(balabala_storage_init(&storage,root,&io)==0);
    BalabalaApp *app=balabala_app_create();
    assert(balabala_storage_import_decks(&storage,app)==0 && storage.report.course==BALABALA_FILE_READ);
    assert(storage.report.peak_buffer_bytes==0 && faults.maximum_read<=BALABALA_STORAGE_VERIFY_BYTES);
    assert(balabala_storage_restore(&storage,app)==0);
    tap(app,BALABALA_DOWN); tap(app,BALABALA_FRONT4); tap(app,BALABALA_DOWN); tap(app,BALABALA_FRONT4);
    tap(app,BALABALA_FRONT1);
    assert(balabala_storage_save(&storage,app)==0);
    size_t saved_length; uint8_t *saved=get(root,"saves/slot0.bin",&saved_length);
    balabala_app_destroy(app);
    root_path(path,root,"curriculum/course.bin"); assert(unlink(path)==0);
    app=balabala_app_create(); assert(balabala_storage_init(&storage,root,NULL)==0);
    assert(balabala_storage_import_decks(&storage,app)==0 && storage.report.course==BALABALA_FILE_ABSENT);
    assert(balabala_storage_restore(&storage,app)==1); export_protected(app);
    assert(balabala_storage_save(&storage,app)==1);
    size_t after_length; uint8_t *after=get(root,"saves/slot0.bin",&after_length);
    assert(saved_length==after_length && memcmp(saved,after,saved_length)==0); free(after);
    balabala_app_destroy(app);
    put(root,"curriculum/course.bin",pack,length);
    app=balabala_app_create(); assert(balabala_storage_init(&storage,root,NULL)==0);
    assert(balabala_storage_import_decks(&storage,app)==0);
    assert(balabala_storage_restore(&storage,app)==0);
    memset(pack+16,'?',length-16); put(root,"curriculum/course.bin",pack,length);
    tap(app,BALABALA_HOME); tap(app,BALABALA_DOWN); tap(app,BALABALA_FRONT4); tap(app,BALABALA_FRONT4);
    export_protected(app); balabala_app_destroy(app);
    app=balabala_app_create(); assert(balabala_storage_init(&storage,root,NULL)==0);
    assert(balabala_storage_import_decks(&storage,app)==1 && storage.report.course==BALABALA_FILE_CONTENT_REJECTED);
    assert(balabala_storage_show_import_error(&storage,app)==1);
    assert(balabala_storage_restore(&storage,app)==1); export_protected(app);
    balabala_app_destroy(app); free(saved); free(pack); root_drop(root);
}
int main(void) {
    unsigned passed=0;
#define RUN(test) do { test(); ++passed; printf("ok %u - %s\n",passed,#test); } while(0)
    RUN(test_input_idle_waits_for_debounce_and_all_key_releases);
    RUN(test_thresholds);
    RUN(test_debounce_release_and_bank_independence);
    RUN(test_adc_errors_never_invent_press_or_release);
    RUN(test_power_short_and_bounce);
    RUN(test_power_long_exactly_once_without_home);
    RUN(test_power_release_threshold_and_sparse_sampling);
    RUN(test_parent_hold_threshold_once_and_sparse_release);
    RUN(test_mapping_is_configurable);
    RUN(test_input_bridges_real_app_and_releases_while_busy);
    RUN(test_unavailable_and_first_boot_do_not_show_corruption);
    RUN(test_single_empty_or_bad_slot_is_recoverable_error);
    RUN(test_slot_io_error_is_not_first_boot);
    RUN(test_save_flush_sync_verify_ack_and_clean);
    RUN(test_save_failures_preserve_dirty_and_old_slot);
    RUN(test_restore_highest_generation_and_next_inactive_slot);
    RUN(test_known_bad_slot_allows_fallback_but_unread_slot_protects);
    RUN(test_deck_files_are_bounded_sequential_and_atomic);
    RUN(test_reboot_restores_round_answers_and_progress);
    RUN(test_startup_deck_read_error_survives_restore_and_exits_to_saved_round);
    RUN(test_startup_content_rejected_then_valid_deck_keeps_error_and_restore);
    RUN(test_curriculum_streams_verifies_and_preserves_save_when_missing);
    printf("platform tests: %u passed; 0 failed (C ASan/UBSan, real Rust ABI)\n",passed);
    return 0;
}
