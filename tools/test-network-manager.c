/* 生产补丁函数由extract-network-manager-test.py逐字提取；仅替换外部IDF API。
 * mutex/cond固定event loop→manager与stop→loop、BLE host→manager竞态。 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "balabala_network.h"
#define CONFIG_NETWORK_PROV_NETWORK_TYPE_WIFI 1
#define CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1 1
#define ESP_TIMER_TASK 1
#define WIFI_IF_STA 0
#define WIFI_STORAGE_RAM 0
#define WIFI_STORAGE_FLASH 1
#define WIFI_PROV_SETTING_BIT 1
#define WIFI_PROV_STORAGE_BIT 2
#define NETWORK_PROV_SECURITY_1 1
#define CONFIG_NETWORK_PROV_AUTOSTOP_TIMEOUT 30
#define MAX_SCAN_RESULTS 16
#define NETWORK_PROV_MGR_VERSION "test"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define portMAX_DELAY (-1)
#define portTICK_PERIOD_MS 1
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 101
#define ESP_ERR_INVALID_ARG 102
#define ESP_ERR_INVALID_STATE 103
#define ESP_EVENT_ANY_ID (-1)
#define WIFI_MODE_STA 1
#define NETWORK_PROV_EVENT 1
#define NETWORK_PROV_MGR_PVT_EVENT 2
#define WIFI_EVENT 3
#define IP_EVENT 4
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t, esp_event_base_t, network_prov_cb_event_t;
typedef int network_prov_security_t;
typedef void *esp_event_handler_instance_t;
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
typedef int network_prov_mode_t, network_prov_wifi_sta_state_t, network_prov_wifi_sta_fail_reason_t;
enum { NETWORK_PROV_MODE_FULL, NETWORK_PROV_MODE_SESSION_ONLY };
enum { NETWORK_PROV_INIT=1, NETWORK_PROV_END, NETWORK_PROV_DEINIT, NETWORK_PROV_WIFI_CRED_SUCCESS, NETWORK_PROV_WIFI_CRED_FAIL, NETWORK_PROV_START, NETWORK_PROV_SET_WIFI_STA_CONFIG, NETWORK_PROV_WIFI_CRED_RECV };
enum { WIFI_EVENT_SCAN_DONE=1, WIFI_EVENT_STA_START, WIFI_EVENT_STA_DISCONNECTED, IP_EVENT_STA_GOT_IP };
enum { NETWORK_PROV_WIFI_STA_CONNECTING, NETWORK_PROV_WIFI_STA_CONNECTED, NETWORK_PROV_WIFI_STA_CONN_ATTEMPT_FAILED,
       NETWORK_PROV_WIFI_STA_DISCONNECTED, NETWORK_PROV_WIFI_STA_AUTH_ERROR, NETWORK_PROV_WIFI_STA_AP_NOT_FOUND };
enum { WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT=1, WIFI_REASON_AUTH_FAIL, WIFI_REASON_HANDSHAKE_TIMEOUT, WIFI_REASON_MIC_FAILURE, WIFI_REASON_NO_AP_FOUND };
typedef struct { int reason; } wifi_event_sta_disconnected_t;
typedef struct { int value; } wifi_ap_record_t, wifi_scan_config_t, protocomm_t;
typedef struct { void *ctx; } network_prov_config_handlers_t, network_prov_scan_handlers_t, network_ctrl_handlers_t;
typedef struct { const uint8_t *data; size_t len; } protocomm_security1_params_t;
typedef struct { uint8_t opaque[128]; } wifi_config_t;
typedef struct { void (*callback)(void *); void *arg; int dispatch_method; const char *name; } esp_timer_create_args_t;
typedef void (*network_prov_cb_func_t)(void *,network_prov_cb_event_t,void *);
typedef struct { network_prov_cb_func_t event_cb; void *user_data; } callback_t;
typedef struct {
    esp_err_t (*prov_stop)(protocomm_t *);
    esp_err_t (*prov_start)(void);
    void *(*new_config)(void);
    void (*delete_config)(void *);
    esp_err_t (*set_config_service)(void);
    esp_err_t (*set_config_endpoint)(void *,const char *,uint16_t);
} network_prov_scheme_t;
typedef struct {
    network_prov_scheme_t scheme;
    callback_t app_event_handler,scheme_event_handler;
    network_prov_mode_t mode;
    struct { unsigned wifi_conn_attempts; } network_prov_wifi_conn_cfg;
} network_prov_mgr_config_t;
typedef struct Timer { bool running,deleted; unsigned period; } *esp_timer_handle_t;
typedef pthread_mutex_t *SemaphoreHandle_t;
static pthread_mutex_t manager_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t loop_mutex;
static pthread_mutex_t sync_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static _Thread_local bool event_actor;
static bool arm_lock_race,loop_ready,event_waiting,stop_done,ble_race,endpoint_go,endpoint_done;
static unsigned mutex_creates,transport_stops,deletes,scheme_deletes,end_posts,end_callbacks;
static unsigned pvt_failures,end_failures,pvt_posts,connects,init_failure;
static uint32_t queued_token;
static bool full_queue,wire_bridge,reopen_in_end,host_notifications;
static unsigned notification_callbacks;
static BalabalaNetwork network;
static pthread_mutex_t channel_mutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned full_drops,reentrant_deinits,wire_restores,wire_releases;
static void notify(void) { assert(!pthread_cond_broadcast(&changed)); }
static void await(bool *flag) {
    struct timespec until; assert(!clock_gettime(CLOCK_REALTIME,&until)); until.tv_sec+=3;
    while(!*flag) assert(pthread_cond_timedwait(&changed,&sync_mutex,&until)==0);
}
static void acquire(SemaphoreHandle_t mutex) {
    if(event_actor) {
        assert(!pthread_mutex_lock(&sync_mutex)); event_waiting=true; notify();
        assert(!pthread_mutex_unlock(&sync_mutex));
    }
    assert(!pthread_mutex_lock(mutex));
}
static void release_lock(SemaphoreHandle_t mutex) { assert(!pthread_mutex_unlock(mutex)); }
#define ACQUIRE_LOCK(mux) acquire(mux)
#define RELEASE_LOCK(mux) release_lock(mux)
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { ++mutex_creates; return &manager_mutex; }
static void vTaskDelay(unsigned delay) { assert(delay<=1000); }
static esp_err_t esp_timer_stop(esp_timer_handle_t t) { assert(t && !t->deleted); t->running=false; return ESP_OK; }
static esp_err_t esp_timer_delete(esp_timer_handle_t t) {
    assert(t && !t->deleted); t->deleted=true;
    if(arm_lock_race) {
        arm_lock_race=false;
        assert(!pthread_mutex_lock(&sync_mutex)); loop_ready=true; notify(); await(&event_waiting);
        assert(!pthread_mutex_unlock(&sync_mutex));
    }
    return ESP_OK;
}
static esp_err_t esp_timer_start_periodic(esp_timer_handle_t t,uint64_t period) {
    assert(t && !t->deleted && !t->running && period==1000000); t->running=true; t->period=(unsigned)period; return ESP_OK;
}
static esp_err_t esp_timer_start_once(esp_timer_handle_t t,uint64_t period) { assert(t && period); t->running=true; return ESP_OK; }
static esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,int32_t id,esp_event_handler_instance_t instance) {
    (void)instance;
    (void)base; (void)id; assert(instance); assert(!pthread_mutex_lock(&loop_mutex));
    assert(!pthread_mutex_unlock(&loop_mutex)); return ESP_OK;
}
static esp_err_t esp_event_post(esp_event_base_t base,int32_t id,const void *data,size_t size,int wait) {
    if(base==NETWORK_PROV_MGR_PVT_EVENT) {
        assert(wait==0 && data && size==4); ++pvt_posts;
        if(pvt_failures) { --pvt_failures; return ESP_FAIL; }
        memcpy(&queued_token,data,size); return ESP_OK;
    }
    if(full_queue && (id==NETWORK_PROV_END || id==NETWORK_PROV_WIFI_CRED_SUCCESS ||
        id==NETWORK_PROV_WIFI_CRED_FAIL || id==NETWORK_PROV_SET_WIFI_STA_CONFIG || id==NETWORK_PROV_WIFI_CRED_RECV)) {
        assert(wait==0 || event_actor); ++full_drops; return ESP_FAIL;
    }
    if(id==NETWORK_PROV_END) {
        if(end_failures) { --end_failures; return ESP_FAIL; }
        ++end_posts;
    }
    return ESP_OK;
}
static esp_err_t esp_wifi_start(void) { return ESP_OK; }
static esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }
static esp_err_t esp_wifi_get_config(int iface,wifi_config_t *cfg) { assert(iface==0); memset(cfg,0,sizeof(*cfg)); return ESP_OK; }
static esp_err_t esp_wifi_set_config(int iface,wifi_config_t *cfg) { assert(iface==0 && cfg); return ESP_OK; }
static esp_err_t esp_wifi_set_storage(int storage) { assert(storage==0 || storage==1); return ESP_OK; }
static esp_err_t esp_timer_create(const esp_timer_create_args_t *,esp_timer_handle_t *);
static esp_err_t network_prov_mgr_start_service(const char *,const char *);
static esp_err_t esp_wifi_connect(void) { ++connects; return ESP_OK; }
static esp_err_t esp_wifi_set_mode(int mode) { assert(mode==WIFI_MODE_STA); return ESP_OK; }
static esp_err_t update_wifi_scan_results(void) { return ESP_OK; }
static void app_info_free_all(void *p) { assert(!p); }
static void protocomm_remove_endpoint(protocomm_t *p,const char *name) { assert(p && name); }
static void protocomm_unset_security(protocomm_t *p,const char *name) { assert(p && name); }
static void protocomm_unset_version(protocomm_t *p,const char *name) { assert(p && name); }
static char *network_prov_get_info_json(void) { return strdup("{}"); }
static esp_err_t protocomm_set_version(protocomm_t *p,const char *name,const char *value) { assert(p && name && value); return ESP_OK; }
static void protocomm_delete(protocomm_t *p) { ++deletes; free(p); }
#include "../.build/manager-stop-under-test.inc"

static esp_err_t transport_stop(protocomm_t *pc) {
    assert(pc && prov_ctx && prov_ctx->protocomm_sec_params); ++transport_stops;
    if(ble_race) {
        assert(!pthread_mutex_lock(&sync_mutex)); endpoint_go=true; notify(); await(&endpoint_done);
        assert(!pthread_mutex_unlock(&sync_mutex));
    }
    /* 在途重复STOP不能抢占cleanup；END只发一次。 */
    uint32_t token=prov_ctx->cleanup_token;
    network_prov_mgr_event_handler_internal(NULL,NETWORK_PROV_MGR_PVT_EVENT,NETWORK_PROV_MGR_STOP,&token);
    assert(network_prov_mgr_deinit()==ESP_ERR_INVALID_STATE);
    return ESP_OK;
}
static esp_err_t stub_ok(void) { return ESP_OK; }
static void *new_config(void) { return init_failure==1?NULL:malloc(1); }
static void delete_config(void *p) { ++scheme_deletes; free(p); }
static esp_err_t endpoint_config(void *ctx,const char *name,uint16_t id) {
    assert(ctx && name && id); return init_failure==2?ESP_FAIL:ESP_OK;
}
static network_prov_mgr_config_t config(void);
static uint32_t start_session(void);
static void app_callback(void *ctx,network_prov_cb_event_t id,void *data) {
    if(wire_bridge) terminal(ctx,id,data);
    if(host_notifications && (id==NETWORK_PROV_SET_WIFI_STA_CONFIG || id==NETWORK_PROV_WIFI_CRED_RECV)) {
        ++notification_callbacks;
        if(id==NETWORK_PROV_SET_WIFI_STA_CONFIG) { assert(data); ++*(int *)data; }
    }
    if(id==NETWORK_PROV_END) {
        ++end_callbacks;
        if(s_deinitializing) assert(network_prov_mgr_deinit()==ESP_ERR_INVALID_STATE);
        else assert(prov_ctx && prov_ctx->prov_state==NETWORK_PROV_STATE_IDLE);
        if(reopen_in_end) {
            reopen_in_end=false; ++reentrant_deinits;
            assert(network_prov_mgr_deinit()==ESP_OK && !prov_ctx);
            assert(network_prov_mgr_init(config())==ESP_OK); start_session();
        }

    }
    if(id==NETWORK_PROV_DEINIT) assert(network_prov_mgr_deinit()==ESP_ERR_INVALID_STATE);
}
static network_prov_mgr_config_t config(void) {
    return (network_prov_mgr_config_t){.scheme={transport_stop,stub_ok,new_config,delete_config,stub_ok,endpoint_config},
        .app_event_handler={app_callback,NULL},.mode=NETWORK_PROV_MODE_FULL};
}
static struct Timer auto_timer,connect_timer,cleanup_timer;
static esp_err_t esp_timer_create(const esp_timer_create_args_t *config,esp_timer_handle_t *timer) {
    assert(config->arg && config->dispatch_method==ESP_TIMER_TASK);
    if(strstr(config->name,"autostop")) *timer=&auto_timer;
    else if(strstr(config->name,"connect")) *timer=&connect_timer;
    else *timer=&cleanup_timer;
    memset(*timer,0,sizeof(**timer)); return ESP_OK;
}
static esp_err_t network_prov_mgr_start_service(const char *name,const char *key) {
    assert(name && !key); prov_ctx->pc=calloc(1,sizeof(protocomm_t)); assert(prov_ctx->pc);
    prov_ctx->network_prov_handlers=calloc(1,sizeof(network_prov_config_handlers_t));
    prov_ctx->network_scan_handlers=calloc(1,sizeof(network_prov_scan_handlers_t));
    prov_ctx->network_ctrl_handlers=calloc(1,sizeof(network_ctrl_handlers_t)); return ESP_OK;
}
static uint32_t start_session(void) {
    assert(network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1,"012345","PROV_ABCDEF",NULL)==ESP_OK);
    assert(prov_ctx->prov_state==NETWORK_PROV_STATE_STARTED);
    prov_ctx->prov_state=NETWORK_PROV_STATE_SUCCESS;
    return prov_ctx->cleanup_token;
}
static uint32_t fresh(void) {
    assert(!prov_ctx); assert(network_prov_mgr_init(config())==ESP_OK); return start_session();
}
static void deliver(uint32_t token) {
    assert(!pthread_mutex_lock(&loop_mutex));
    network_prov_mgr_event_handler_internal(NULL,NETWORK_PROV_MGR_PVT_EVENT,NETWORK_PROV_MGR_STOP,&token);
    assert(!pthread_mutex_unlock(&loop_mutex));
}
static void finish(uint32_t token,unsigned before) {
    cleanup_delay_timer_cb((void *)(uintptr_t)token); assert(queued_token==token); deliver(queued_token);
    assert(prov_ctx->prov_state==NETWORK_PROV_STATE_IDLE && transport_stops==before+1);
    assert(cleanup_timer.deleted && !cleanup_timer.running);
    deliver(token); assert(transport_stops==before+1);
    assert(network_prov_mgr_deinit()==ESP_OK && !prov_ctx);
}
static void *event_thread(void *unused) {
    (void)unused; assert(!pthread_mutex_lock(&loop_mutex));
    assert(!pthread_mutex_lock(&sync_mutex)); await(&loop_ready); assert(!pthread_mutex_unlock(&sync_mutex));
    event_actor=true;
    network_prov_mgr_event_handler_internal(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
    event_actor=false; assert(!pthread_mutex_unlock(&loop_mutex)); return NULL;
}
static void *stop_thread(void *mode) {
    if(mode) stop_prov_timer_cb(mode); else assert(network_prov_mgr_done()==ESP_OK);
    assert(!pthread_mutex_lock(&sync_mutex)); stop_done=true; notify(); assert(!pthread_mutex_unlock(&sync_mutex)); return NULL;
}
static void test_phone_and_autostop_lock_inversion_is_bounded(void) {
    for(unsigned auto_stop=0;auto_stop<2;++auto_stop) {
        uint32_t token=fresh(); unsigned before=transport_stops,ends=end_posts;
        loop_ready=event_waiting=stop_done=false; arm_lock_race=true;
        pthread_t event_worker,stop_worker; assert(!pthread_create(&event_worker,NULL,event_thread,NULL));
        assert(!pthread_create(&stop_worker,NULL,stop_thread,auto_stop?(void *)(uintptr_t)token:NULL));
        assert(!pthread_mutex_lock(&sync_mutex)); await(&stop_done); assert(!pthread_mutex_unlock(&sync_mutex));
        assert(!pthread_join(event_worker,NULL)); assert(!pthread_join(stop_worker,NULL));
        assert(prov_ctx->prov_state==NETWORK_PROV_STATE_STOPPING && cleanup_timer.period==1000000);
        assert(network_prov_mgr_deinit()==ESP_ERR_INVALID_STATE);
        finish(token,before); assert(end_posts==ends+1);
    }
}
static void *endpoint_thread(void *unused) {
    (void)unused;
    assert(!pthread_mutex_lock(&sync_mutex)); await(&endpoint_go); assert(!pthread_mutex_unlock(&sync_mutex));
    network_prov_wifi_sta_state_t state; assert(network_prov_mgr_get_wifi_state(&state)==ESP_OK);
    assert(network_prov_mgr_deinit()==ESP_ERR_INVALID_STATE);
    network_prov_mgr_stop_provisioning();
    assert(!pthread_mutex_lock(&sync_mutex)); endpoint_done=true; notify(); assert(!pthread_mutex_unlock(&sync_mutex)); return NULL;
}
static void test_ble_endpoint_drain_and_reentrant_deinit(void) {
    uint32_t token=fresh(); unsigned before=transport_stops,ends=end_posts;
    network_prov_mgr_stop_provisioning(); endpoint_go=endpoint_done=false; ble_race=true;
    pthread_t endpoint; assert(!pthread_create(&endpoint,NULL,endpoint_thread,NULL));
    finish(token,before); assert(!pthread_join(endpoint,NULL)); ble_race=false;
    assert(end_posts==ends+1);
}
static void test_private_queue_retries_and_dropped_end_still_complete(void) {
    uint32_t token=fresh(); unsigned before=transport_stops,ends=end_posts,cbs=end_callbacks;
    network_prov_mgr_stop_provisioning(); pvt_failures=3;
    for(unsigned i=0;i<3;++i) { cleanup_delay_timer_cb((void *)(uintptr_t)token); assert(cleanup_timer.running && transport_stops==before); }
    end_failures=1;
    cleanup_delay_timer_cb((void *)(uintptr_t)token); deliver(queued_token);
    assert(prov_ctx->prov_state==NETWORK_PROV_STATE_IDLE && cleanup_timer.deleted && !cleanup_timer.running);
    assert(transport_stops==before+1 && end_posts==ends && end_callbacks==cbs+1);
    cleanup_delay_timer_cb((void *)(uintptr_t)token); deliver(queued_token);
    assert(transport_stops==before+1 && end_callbacks==cbs+1);
    assert(network_prov_mgr_deinit()==ESP_OK);
}
static void test_sync_end_can_deinit_and_start_before_old_stack_returns(void) {
    uint32_t old=fresh(); unsigned before=transport_stops,cbs=end_callbacks,ds=reentrant_deinits;
    network_prov_mgr_stop_provisioning(); reopen_in_end=true;
    cleanup_delay_timer_cb((void *)(uintptr_t)old); deliver(queued_token);
    assert(prov_ctx && prov_ctx->cleanup_token!=old && prov_ctx->prov_state==NETWORK_PROV_STATE_SUCCESS);
    assert(end_callbacks==cbs+1 && reentrant_deinits==ds+1 && transport_stops==before+1);
    deliver(old); stop_prov_timer_cb((void *)(uintptr_t)old); wifi_connect_timer_cb((void *)(uintptr_t)old);
    assert(prov_ctx->prov_state==NETWORK_PROV_STATE_SUCCESS && transport_stops==before+1);
    uint32_t token=prov_ctx->cleanup_token; network_prov_mgr_stop_provisioning(); finish(token,before+1);
}
static void test_late_timers_and_private_stop_cannot_touch_reopened_session(void) {
    uint32_t old=fresh(); unsigned before=transport_stops; network_prov_mgr_stop_provisioning(); finish(old,before);
    uint32_t token=fresh(); assert(token!=old); unsigned calls=connects,ends=end_posts;
    cleanup_delay_timer_cb((void *)(uintptr_t)old); deliver(queued_token);
    stop_prov_timer_cb((void *)(uintptr_t)old); wifi_connect_timer_cb((void *)(uintptr_t)old);
    assert(prov_ctx->prov_state==NETWORK_PROV_STATE_SUCCESS && connects==calls && end_posts==ends);
    wifi_connect_timer_cb((void *)(uintptr_t)token); assert(connects==calls);
    prov_ctx->prov_state=NETWORK_PROV_STATE_CRED_RECV;
    wifi_connect_timer_cb((void *)(uintptr_t)token); assert(connects==calls+1);
    network_prov_mgr_stop_provisioning(); deliver(old);
    assert(prov_ctx->prov_state==NETWORK_PROV_STATE_STOPPING); finish(token,before+1);
}
static void test_init_failures_reuse_singleton_mutex_and_blocking_deinit(void) {
    for(unsigned failure=1;failure<=2;++failure) {
        init_failure=failure; assert(network_prov_mgr_init(config())!=ESP_OK && !prov_ctx);
        assert(network_prov_mgr_deinit()==ESP_OK);
    }
    init_failure=0; fresh(); unsigned before=transport_stops;
    assert(network_prov_mgr_deinit()==ESP_OK && !prov_ctx && transport_stops==before+1);
    assert(mutex_creates==1);
    assert(network_prov_mgr_deinit()==ESP_OK);
}
static void *host_notification_thread(void *unused) {
    (void)unused;
    assert(!pthread_mutex_lock(&sync_mutex)); await(&loop_ready); assert(!pthread_mutex_unlock(&sync_mutex));
    ACQUIRE_LOCK(prov_ctx_lock); int mutable_config=0;
    execute_event_cb(NETWORK_PROV_SET_WIFI_STA_CONFIG,&mutable_config,sizeof(mutable_config));
    assert(mutable_config==1); /* 同步app callback仍可修改配置。 */
    execute_event_cb(NETWORK_PROV_WIFI_CRED_RECV,NULL,0); RELEASE_LOCK(prov_ctx_lock);
    assert(!pthread_mutex_lock(&sync_mutex)); endpoint_done=true; notify(); assert(!pthread_mutex_unlock(&sync_mutex)); return NULL;
}
static void *host_waiting_consumer(void *unused) {
    (void)unused; assert(!pthread_mutex_lock(&loop_mutex));
    assert(!pthread_mutex_lock(&sync_mutex)); loop_ready=true; notify(); await(&endpoint_done);
    assert(!pthread_mutex_unlock(&sync_mutex)); assert(!pthread_mutex_unlock(&loop_mutex)); return NULL;
}
static void test_host_manager_observers_do_not_wait_for_full_queue(void) {
    uint32_t token=fresh(); unsigned callbacks=notification_callbacks,drops=full_drops,before=transport_stops;
    host_notifications=full_queue=true; loop_ready=endpoint_done=false;
    pthread_t consumer,host; assert(!pthread_create(&consumer,NULL,host_waiting_consumer,NULL));
    assert(!pthread_create(&host,NULL,host_notification_thread,NULL));
    assert(!pthread_join(consumer,NULL)); assert(!pthread_join(host,NULL));
    assert(notification_callbacks==callbacks+2 && full_drops==drops+2);
    host_notifications=full_queue=false; network_prov_mgr_stop_provisioning(); finish(token,before);
}
static int wire_ok(void *ctx) { (void)ctx; return 0; }
static void wire_lock(void *ctx) { (void)ctx; assert(!pthread_mutex_lock(&channel_mutex)); }
static void wire_unlock(void *ctx) { (void)ctx; assert(!pthread_mutex_unlock(&channel_mutex)); }
static int wire_prepare(void *ctx,uint32_t generation) { (void)ctx; assert(generation==network.generation); return 0; }
static int wire_identity(void *ctx,char name[12],char pop[17]) {
    (void)ctx; memcpy(name,"PROV_ABCDEF",12); memcpy(pop,"012345",7); return 0;
}
static int wire_init(void *ctx) {
    (void)ctx; assert(!pthread_mutex_trylock(&channel_mutex)); assert(!pthread_mutex_unlock(&channel_mutex));
    network_prov_mgr_config_t cfg=config(); cfg.app_event_handler.user_data=(void *)(uintptr_t)network.generation;
    return network_prov_mgr_init(cfg);
}
static int wire_start(void *ctx,const char *name,const char *pop) {
    (void)ctx; int err=network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1,pop,name,NULL);
    if(!err) prov_ctx->prov_state=NETWORK_PROV_STATE_CRED_RECV;
    return err;
}
static void wire_stop(void *ctx) { (void)ctx; network_prov_mgr_stop_provisioning(); }
static int wire_deinit(void *ctx) { (void)ctx; return network_prov_mgr_deinit(); }
static int wire_cleanup_error(void *ctx) { (void)ctx; return network_prov_mgr_get_cleanup_error(); }
static int wire_restore(void *ctx) { (void)ctx; ++wire_restores; return 0; }
static int wire_release(void *ctx) { (void)ctx; ++wire_releases; return 0; }
static void wire_event(bool success) {
    wifi_event_sta_disconnected_t fail={.reason=WIFI_REASON_AUTH_FAIL};
    assert(!pthread_mutex_lock(&loop_mutex)); event_actor=true;
    network_prov_mgr_event_handler_internal(NULL,success?IP_EVENT:WIFI_EVENT,
        success?IP_EVENT_STA_GOT_IP:WIFI_EVENT_STA_DISCONNECTED,success?NULL:&fail);
    event_actor=false; assert(!pthread_mutex_unlock(&loop_mutex));
}
static void test_full_queue_sync_success_fail_end_through_real_controller(void) {
    for(unsigned mode=0;mode<8;++mode) {
        wire_bridge=true; full_queue=true;
        BalabalaNetworkOps ops={wire_prepare,wire_ok,wire_ok,wire_identity,wire_init,wire_start,
            wire_stop,wire_deinit,wire_cleanup_error,wire_restore,wire_release,wire_lock,wire_unlock,NULL};
        balabala_network_init(&network,&ops);
        unsigned restores=wire_restores,releases=wire_releases,cbs=end_callbacks,drops=full_drops;
        balabala_network_step(&network,BALABALA_REQUEST_NETWORK_START,0);
        (void)balabala_network_advance(&network,network.generation,1,0);
        (void)balabala_network_advance(&network,network.generation,2,0);
        uint32_t token=prov_ctx->cleanup_token;
        if(mode==6) wire_event(false);
        else if(mode==7) wire_event(false);
        if(mode!=6) wire_event(true);
        if(mode<6) wire_event(false); /* 同批两顺序均以已成功提交为准。 */
        uint32_t request=(mode&1)?BALABALA_REQUEST_SHUTDOWN:BALABALA_REQUEST_NETWORK_CANCEL;
        if(mode<2) {
            /* 先完成manager END，再由runtime安全点消费SUCCESS+退出+END。 */
            network_prov_mgr_stop_provisioning(); full_queue=false;
            cleanup_delay_timer_cb((void *)(uintptr_t)token); full_queue=true; deliver(queued_token);
        }
        if(mode==4 || mode==5) {
            balabala_network_step(&network,0,1); assert(network.success);
            wire_event(false); /* 后批失败不得覆盖已提交SUCCESS。 */
        }
        balabala_network_step(&network,request,2);
        if(network.active) {
            full_queue=false; cleanup_delay_timer_cb((void *)(uintptr_t)token); full_queue=true;
            deliver(queued_token); balabala_network_step(&network,0,3);
        }
        assert(!network.active && !prov_ctx && end_callbacks==cbs+1 && wire_releases==releases+1);
        assert(network.success==(mode!=6) && wire_restores==restores+(mode==6));
        assert(full_drops>drops); full_queue=false; wire_bridge=false;
    }
}
int main(void) {
    pthread_mutexattr_t attr; assert(!pthread_mutexattr_init(&attr)); assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(&loop_mutex,&attr)); assert(!pthread_mutexattr_destroy(&attr));
    unsigned passed=0;
#define RUN(test) do { test(); printf("ok %u - %s\n",++passed,#test); } while(0)
    RUN(test_phone_and_autostop_lock_inversion_is_bounded);
    RUN(test_ble_endpoint_drain_and_reentrant_deinit);
    RUN(test_private_queue_retries_and_dropped_end_still_complete);
    RUN(test_sync_end_can_deinit_and_start_before_old_stack_returns);
    RUN(test_late_timers_and_private_stop_cannot_touch_reopened_session);
    RUN(test_init_failures_reuse_singleton_mutex_and_blocking_deinit);
    RUN(test_full_queue_sync_success_fail_end_through_real_controller);
    RUN(test_host_manager_observers_do_not_wait_for_full_queue);
    assert(!pthread_mutex_destroy(&loop_mutex));
    printf("manager real patched functions: %u passed; 0 failed (mutex/condition barriers, 3s bound, ASan/UBSan)\n",passed);
}
