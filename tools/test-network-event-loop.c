/* 完整固定IDF事件实现+原样manager函数。仅RTOS、Wi-Fi、BLE、timer为宿主替身。 */
#define _POSIX_C_SOURCE 200809L
#include "mock-event-rtos.h"
#include "esp_event.h"
#include "esp_event_internal.h"
#include "balabala_network.h"
#include "../.tools/esp-idf/components/esp_event/esp_event.c"
#include "../.tools/esp-idf/components/esp_event/default_event_loop.c"

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
#define WIFI_MODE_STA 1
static esp_event_base_t const NETWORK_PROV_EVENT="NETWORK_PROV_EVENT";
static esp_event_base_t const NETWORK_PROV_MGR_PVT_EVENT="NETWORK_PROV_MGR_PVT_EVENT";
static esp_event_base_t const WIFI_EVENT="WIFI_EVENT", IP_EVENT="IP_EVENT", TEST_EVENT="TEST_EVENT";
typedef int network_prov_cb_event_t,network_prov_security_t;
typedef int network_prov_mode_t,network_prov_wifi_sta_state_t,network_prov_wifi_sta_fail_reason_t;
enum { NETWORK_PROV_MODE_FULL,NETWORK_PROV_MODE_SESSION_ONLY };
enum { NETWORK_PROV_INIT=1,NETWORK_PROV_END,NETWORK_PROV_DEINIT,NETWORK_PROV_WIFI_CRED_SUCCESS,NETWORK_PROV_WIFI_CRED_FAIL,NETWORK_PROV_START,NETWORK_PROV_SET_WIFI_STA_CONFIG,NETWORK_PROV_WIFI_CRED_RECV };
enum { WIFI_EVENT_SCAN_DONE=1,WIFI_EVENT_STA_START,WIFI_EVENT_STA_DISCONNECTED,IP_EVENT_STA_GOT_IP };
enum { NETWORK_PROV_WIFI_STA_CONNECTING,NETWORK_PROV_WIFI_STA_CONNECTED,NETWORK_PROV_WIFI_STA_CONN_ATTEMPT_FAILED,NETWORK_PROV_WIFI_STA_DISCONNECTED,NETWORK_PROV_WIFI_STA_AUTH_ERROR,NETWORK_PROV_WIFI_STA_AP_NOT_FOUND };
enum { WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT=1,WIFI_REASON_AUTH_FAIL,WIFI_REASON_HANDSHAKE_TIMEOUT,WIFI_REASON_MIC_FAILURE,WIFI_REASON_NO_AP_FOUND };
typedef struct { int reason; } wifi_event_sta_disconnected_t;
typedef struct { int value; } wifi_ap_record_t,wifi_scan_config_t;
typedef struct { void *ctx; } network_prov_config_handlers_t,network_prov_scan_handlers_t,network_ctrl_handlers_t;
/* Actual endpoint list, priv_data, request routing and Security1 parameter copy. */
#define TAG protocomm_tag
#define protocomm_delete raw_protocomm_delete
#define protocomm_add_endpoint raw_protocomm_add_endpoint
#include "../.tools/esp-idf/components/protocomm/src/common/protocomm.c"
#undef protocomm_add_endpoint
#undef protocomm_delete
#undef TAG

typedef struct { uint8_t opaque[128]; } wifi_config_t;
typedef struct { void (*callback)(void *); void *arg; int dispatch_method; const char *name; } esp_timer_create_args_t;
typedef void (*network_prov_cb_func_t)(void *,network_prov_cb_event_t,void *);
typedef struct { network_prov_cb_func_t event_cb; void *user_data; } callback_t;
typedef struct {
    esp_err_t (*prov_stop)(protocomm_t *);
    esp_err_t (*prov_start)(protocomm_t *,void *);
    void *(*new_config)(void);
    void (*delete_config)(void *);
    esp_err_t (*set_config_service)(void *,const char *,const char *);
    esp_err_t (*set_config_endpoint)(void *,const char *,uint16_t);
} network_prov_scheme_t;
typedef struct {
    network_prov_scheme_t scheme;
    callback_t app_event_handler,scheme_event_handler;
    network_prov_mode_t mode;
    struct { unsigned wifi_conn_attempts; } network_prov_wifi_conn_cfg;
} network_prov_mgr_config_t;
typedef struct Timer { bool running,deleted; } *esp_timer_handle_t;
static struct Timer auto_timer,connect_timer,cleanup_timer;
static BalabalaNetwork network;
static unsigned ends,transport_stops,pc_deletes,callbacks,reopens,stop_errors,deinits;
static bool use_controller_bridge;
static bool reopen_on_end,fill_on_stop,deny_on_stop,ble_race,arm_lock_race;
static unsigned restart_failure;
static long registration_failure=-1;
static size_t allocations_before_unregister;
static pthread_mutex_t barrier_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t barrier_changed=PTHREAD_COND_INITIALIZER;
static bool event_has_loop,event_waiting,stop_done,endpoint_go,endpoint_done;
static _Thread_local bool event_actor;
static void signal_barrier(void) { assert(!pthread_cond_broadcast(&barrier_changed)); }
static void await_flag(bool *flag) {
    struct timespec until=deadline();
    while(!*flag) assert(pthread_cond_timedwait(&barrier_changed,&barrier_mutex,&until)==0);
}
static void acquire_manager(SemaphoreHandle_t p) {
    if(event_actor) {
        assert(!pthread_mutex_lock(&barrier_mutex)); event_waiting=true; signal_barrier();
        assert(!pthread_mutex_unlock(&barrier_mutex));
    }
    assert(!pthread_mutex_lock(p));
}
#define ACQUIRE_LOCK(p) acquire_manager(p)
#define RELEASE_LOCK(p) assert(!pthread_mutex_unlock(p))
static esp_err_t esp_timer_stop(esp_timer_handle_t t) { assert(t && !t->deleted); t->running=false; return ESP_OK; }
static esp_err_t esp_timer_delete(esp_timer_handle_t t) {
    assert(t && !t->deleted); t->deleted=true;
    if(arm_lock_race) {
        arm_lock_race=false; assert(!pthread_mutex_lock(&barrier_mutex));
        event_has_loop=true; signal_barrier(); await_flag(&event_waiting); assert(!pthread_mutex_unlock(&barrier_mutex));
    }
    return ESP_OK;
}
static esp_err_t esp_timer_start_periodic(esp_timer_handle_t t,uint64_t n) { assert(t && !t->deleted && n==1000000); t->running=true; return ESP_OK; }
static esp_err_t esp_timer_start_once(esp_timer_handle_t t,uint64_t n) { assert(t && !t->deleted && n); t->running=true; return ESP_OK; }
static esp_err_t esp_timer_create(const esp_timer_create_args_t *c,esp_timer_handle_t *t) {
    assert(c->callback && c->arg && c->dispatch_method==ESP_TIMER_TASK);
    *t=strstr(c->name,"autostop")?&auto_timer:strstr(c->name,"connect")?&connect_timer:&cleanup_timer;
    memset(*t,0,sizeof(**t)); return ESP_OK;
}
static esp_err_t esp_wifi_start(void) { return ESP_OK; }
static esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }
static esp_err_t esp_wifi_get_config(int i,wifi_config_t *c) { assert(!i && c); memset(c,0,sizeof(*c)); return ESP_OK; }
static esp_err_t esp_wifi_set_config(int i,wifi_config_t *c) { assert(!i && c); return ESP_OK; }
static esp_err_t esp_wifi_set_storage(int s) { assert(s==0 || s==1); return ESP_OK; }
static esp_err_t esp_wifi_connect(void) { return ESP_OK; }
static esp_err_t esp_wifi_set_mode(int s) { assert(s==WIFI_MODE_STA); return ESP_OK; }
static esp_err_t update_wifi_scan_results(void) { return ESP_OK; }
static void app_info_free_all(void *p) { assert(!p); }
static void protocomm_delete(protocomm_t *p) { assert(p); ++pc_deletes; raw_protocomm_delete(p); }
static const protocomm_security_t protocomm_security1={.ver=1}; /* crypto only is mocked */
static char *network_prov_get_info_json(void) { return strdup("{}"); }
static esp_err_t endpoint_handler(uint32_t,const uint8_t *,ssize_t,uint8_t **,ssize_t *,void *);
#define network_prov_config_data_handler endpoint_handler
#define network_prov_scan_handler endpoint_handler
#define network_ctrl_handler endpoint_handler
static esp_err_t get_network_prov_handlers(network_prov_config_handlers_t *p) { if(!p)return ESP_ERR_NO_MEM; p->ctx=NULL; return ESP_OK; }
static esp_err_t get_network_scan_handlers(network_prov_scan_handlers_t *p) { if(!p)return ESP_ERR_NO_MEM; p->ctx=NULL; return ESP_OK; }
static esp_err_t get_network_ctrl_handlers(network_ctrl_handlers_t *p) { if(!p)return ESP_ERR_NO_MEM; p->ctx=NULL; return ESP_OK; }
static bool rollback_race,endpoint_entered;
static protocomm_t *endpoint_pc;
static unsigned endpoint_visits,stop_failures;
static esp_err_t protocomm_add_endpoint(protocomm_t *p,const char *n,protocomm_req_handler_t handler,void *ctx) {
    esp_err_t ret=raw_protocomm_add_endpoint(p,n,handler,ctx);
    if(ret==ESP_OK && !strcmp(n,"prov-ctrl")) {
        assert(search_endpoint(p,n)->priv_data==ctx);
        if(rollback_race) {
            assert(!pthread_mutex_lock(&barrier_mutex)); endpoint_pc=p; signal_barrier();
            assert(!pthread_mutex_unlock(&barrier_mutex));
        }
        /* Inject AFTER real endpoint allocation, directly before real registration. */
        if(registration_failure>=0) { fail_after=registration_failure; registration_failure=-1; }
    }
    return ret;
}
#include "../.build/manager-event-loop-under-test.inc"

static esp_err_t endpoint_handler(uint32_t session,const uint8_t *in,ssize_t len,uint8_t **out,ssize_t *outlen,void *data) {
    (void)session; (void)in; (void)len; (void)out; (void)outlen;
    if(rollback_race && !endpoint_entered) {
        assert(!pthread_mutex_lock(&barrier_mutex)); endpoint_entered=true; signal_barrier();
        await_flag(&endpoint_go); assert(!pthread_mutex_unlock(&barrier_mutex));
    }
    /* Decoder is a stub; the table is allocated/freed by real start_service,
     * reached through real protocomm endpoint priv_data and request routing. */
    assert(data && *(void **)data==NULL); ++endpoint_visits;
    assert(prov_ctx->protocomm_sec_params && prov_ctx->pc->sec_params);
    assert(((protocomm_security1_params_t *)prov_ctx->pc->sec_params)->data==
           ((protocomm_security1_params_t *)prov_ctx->protocomm_sec_params)->data);
    return ESP_OK;
}
static void visit_endpoints(protocomm_t *pc) {
    for(unsigned i=0;i<3;++i) {
        const char *name=(const char *[]){"prov-config","prov-scan","prov-ctrl"}[i];
        uint8_t *out=NULL; ssize_t outlen=0;
        assert(protocomm_req_handle(pc,name,1,NULL,0,&out,&outlen)==ESP_OK && !out && !outlen);
    }
}
static void *rollback_endpoint_thread(void *arg) {
    (void)arg; assert(!pthread_mutex_lock(&barrier_mutex));
    struct timespec until=deadline();
    while(!endpoint_pc) assert(pthread_cond_timedwait(&barrier_changed,&barrier_mutex,&until)==0);
    protocomm_t *pc=endpoint_pc; assert(!pthread_mutex_unlock(&barrier_mutex));
    visit_endpoints(pc);
    assert(!pthread_mutex_lock(&barrier_mutex)); endpoint_done=true; signal_barrier();
    assert(!pthread_mutex_unlock(&barrier_mutex)); return NULL;
}
static esp_event_loop_instance_t *loop(void) { assert(s_default_loop); return s_default_loop; }
static void dispatch(TickType_t n) {
    TaskHandle_t previous=actor_task; actor_task=loop()->task;
    assert(esp_event_loop_run(s_default_loop,n)==ESP_OK); actor_task=previous;
}
static size_t node_count(bool retired_only) {
    size_t n=0; esp_event_loop_node_t *l; esp_event_base_node_t *b; esp_event_id_node_t *i; esp_event_handler_node_t *h;
    SLIST_FOREACH(l,&loop()->loop_nodes,next) {
        SLIST_FOREACH(h,&l->handlers,next) n+=!retired_only || h->deferred_instance_remove;
        SLIST_FOREACH(b,&l->base_nodes,next) {
            SLIST_FOREACH(h,&b->handlers,next) n+=!retired_only || h->deferred_instance_remove;
            SLIST_FOREACH(i,&b->id_nodes,next) SLIST_FOREACH(h,&i->handlers,next) n+=!retired_only || h->deferred_instance_remove;
        }
    }
    return n;
}
static void fill_queue(void) {
    while(esp_event_post(TEST_EVENT,99,NULL,0,0)==ESP_OK) {}
    assert(loop()->queue->count==CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE);
}
static network_prov_mgr_config_t config(void);
static uint32_t start(void) {
    assert(network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1,"012345","PROV_ABCDEF",NULL)==ESP_OK);
    assert(prov_ctx->wifi_event_instance && prov_ctx->ip_event_instance && prov_ctx->stop_event_instance);
    prov_ctx->prov_state=NETWORK_PROV_STATE_SUCCESS; return prov_ctx->cleanup_token;
}
static void app_callback(void *p,network_prov_cb_event_t id,void *data) {
    (void)p; (void)data; ++callbacks;
    if(use_controller_bridge) terminal((void *)(uintptr_t)network.generation,id,NULL);
    if(id==NETWORK_PROV_DEINIT) ++deinits;
    if(id!=NETWORK_PROV_END) return;
    ++ends;
    if(prov_ctx) {
    assert(prov_ctx->prov_state==NETWORK_PROV_STATE_IDLE);
    assert(!prov_ctx->pc && !prov_ctx->session_pc && !prov_ctx->protocomm_sec_params);
    assert(!prov_ctx->wifi_event_instance && !prov_ctx->ip_event_instance && !prov_ctx->stop_event_instance);
    assert(!prov_ctx->cleanup_delay_timer);
    } else { assert(s_deinitializing && !reopen_on_end); }
    if(deny_on_stop) {
        assert(deny_allocations && allocation_calls==allocations_before_unregister);
        deny_allocations=false; deny_on_stop=false;
    }
    if(reopen_on_end) {
        reopen_on_end=false; ++reopens;
        assert(node_count(true)>=1);
        assert(network_prov_mgr_deinit()==ESP_OK && !prov_ctx);
        assert(network_prov_mgr_init(config())==ESP_OK);
        if(restart_failure) {
            /* 旧私有节点仍在树上：Wi-Fi为3次分配、IP为4次、私有为2次。 */
            registration_failure=restart_failure==1?0:restart_failure==2?3:7;
            size_t failures=allocation_failures;
            assert(network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1,"012345","PROV_ABCDEF",NULL)==ESP_ERR_NO_MEM);
            assert(allocation_failures==failures+1 && prov_ctx->prov_state==NETWORK_PROV_STATE_IDLE);
            assert(!prov_ctx->wifi_event_instance && !prov_ctx->ip_event_instance && !prov_ctx->stop_event_instance);
            assert(!prov_ctx->pc);
            restart_failure=0;
            /* Successful drainage now releases the failed-start PoP immediately. */
            assert(!prov_ctx->protocomm_sec_params);
            size_t with_failed_context=live_allocations;
            assert(network_prov_mgr_deinit()==ESP_OK);
            assert(live_allocations+2==with_failed_context); /* ctx/config */
            assert(network_prov_mgr_init(config())==ESP_OK);
        }
        start();
    }
}
static esp_err_t transport_stop(protocomm_t *p) {
    assert(p); ++transport_stops;
    if(stop_failures) { --stop_failures; ++stop_errors; return ESP_FAIL; }
    if(rollback_race) {
        assert(!pthread_mutex_lock(&barrier_mutex)); await_flag(&endpoint_entered);
        endpoint_go=true; signal_barrier(); await_flag(&endpoint_done); assert(!pthread_mutex_unlock(&barrier_mutex));
    }
    if(ble_race) {
        assert(!pthread_mutex_lock(&barrier_mutex)); endpoint_go=true; signal_barrier();
        await_flag(&endpoint_done); assert(!pthread_mutex_unlock(&barrier_mutex));
    }
    assert(network_prov_mgr_deinit()==ESP_ERR_INVALID_STATE);
    if(fill_on_stop) { fill_on_stop=false; fill_queue(); }
    if(deny_on_stop) { allocations_before_unregister=allocation_calls; deny_allocations=true; }
    return ESP_OK;
}
static esp_err_t scheme_start(protocomm_t *p,void *c) { assert(p && c); return ESP_OK; }
static void *new_config(void) { return malloc(1); }
static void delete_config(void *p) { free(p); }
static esp_err_t service_config(void *p,const char *name,const char *key) { assert(p && name && !key); return ESP_OK; }
static esp_err_t endpoint_config(void *p,const char *name,uint16_t id) { assert(p && name && id); return ESP_OK; }
static network_prov_mgr_config_t config(void) {
    return (network_prov_mgr_config_t){.scheme={transport_stop,scheme_start,new_config,delete_config,service_config,endpoint_config},.app_event_handler={app_callback,NULL},.mode=NETWORK_PROV_MODE_FULL};
}
static uint32_t fresh(void) {
    assert(!prov_ctx && !s_default_loop); assert(esp_event_loop_create_default()==ESP_OK);
    assert(network_prov_mgr_init(config())==ESP_OK); return start();
}
static void stop_and_dispatch(uint32_t token) {
    network_prov_mgr_stop_provisioning(); assert(prov_ctx->prov_state==NETWORK_PROV_STATE_STOPPING);
    cleanup_delay_timer_cb((void *)(uintptr_t)token); dispatch(portMAX_DELAY);
    assert(prov_ctx && prov_ctx->prov_state==NETWORK_PROV_STATE_IDLE);
}
static void teardown(void) {
    assert(network_prov_mgr_deinit()==ESP_OK && !prov_ctx); assert(esp_event_loop_delete_default()==ESP_OK);
}
static void test_stop_sync_reopen_second_stop_uses_real_registered_instance(void) {
    for(unsigned autostop=0;autostop<2;autostop++) {
        uint32_t old=fresh(); unsigned before=ends,stops=transport_stops;
        reopen_on_end=true;
        if(autostop) stop_prov_timer_cb((void *)(uintptr_t)old);
        else assert(network_prov_mgr_done()==ESP_OK);
        cleanup_delay_timer_cb((void *)(uintptr_t)old); dispatch(portMAX_DELAY);
        assert(prov_ctx->prov_state==NETWORK_PROV_STATE_SUCCESS && prov_ctx->cleanup_token!=old);
        assert(node_count(false)==3 && !node_count(true));
        assert(ends==before+1 && transport_stops==stops+1);
        cleanup_delay_timer_cb((void *)(uintptr_t)old); dispatch(portMAX_DELAY);
        assert(prov_ctx->prov_state==NETWORK_PROV_STATE_SUCCESS && ends==before+1);
        stop_and_dispatch(prov_ctx->cleanup_token);
        assert(ends==before+2 && transport_stops==stops+2 && node_count(false)==0);
        teardown();
    }
}
static void test_full_queue_no_alloc_unregister(void) {
    uint32_t token=fresh(); unsigned before=ends,stops=transport_stops;
    fill_on_stop=true; deny_on_stop=true;
    network_prov_mgr_stop_provisioning(); cleanup_delay_timer_cb((void *)(uintptr_t)token);
    unsigned waits=queue_waits; dispatch(portMAX_DELAY);
    assert(!deny_allocations && ends==before+1 && transport_stops==stops+1);
    assert(node_count(false)==0 && queue_waits==waits && queue_failures>0);
    teardown();
}
static esp_event_handler_instance_t old_instances[3],new_instances[3];
static unsigned old_calls,new_calls[3];
static void new_handler(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)base; (void)id; (void)data; ++new_calls[(uintptr_t)arg];
}
static void legacy_handler(void *arg,esp_event_base_t base,int32_t id,void *data) { (void)arg;(void)base;(void)id;(void)data;assert(false); }
static void old_handler(void *arg,esp_event_base_t base,int32_t id,void *data) {
    assert(!arg && base==TEST_EVENT && id==42 && !data); ++old_calls;
    esp_event_base_t bases[]={ESP_EVENT_ANY_BASE,TEST_EVENT,TEST_EVENT};
    int32_t ids[]={ESP_EVENT_ANY_ID,ESP_EVENT_ANY_ID,42};
    for(unsigned i=0;i<3;i++) assert(esp_event_handler_instance_unregister(bases[i],ids[i],old_instances[i])==ESP_OK);
    assert(node_count(true)==3);
    for(unsigned i=0;i<3;i++) {
        assert(esp_event_handler_instance_register(bases[i],ids[i],new_handler,(void *)(uintptr_t)i,&new_instances[i])==ESP_OK);
        assert(old_instances[i]!=new_instances[i]);
    }
    /* Legacy仍走原队列退休路径；本补丁不能提前回收该节点。 */
    assert(esp_event_handler_unregister(TEST_EVENT,88,legacy_handler)==ESP_OK);
    fill_queue(); size_t calls=allocation_calls; deny_allocations=true;
    for(unsigned repeat=0;repeat<2;repeat++) for(unsigned i=0;i<3;i++)
        assert(esp_event_handler_instance_unregister(bases[i],ids[i],old_instances[i])==ESP_OK);
    assert(allocation_calls==calls); atomic_fetch_add(&ticks,1000);
}
static void test_identity_idempotence_tick_profiling_and_legacy(void) {
    assert(esp_event_loop_create_default()==ESP_OK);
    assert(esp_event_handler_instance_register(ESP_EVENT_ANY_BASE,ESP_EVENT_ANY_ID,old_handler,NULL,&old_instances[0])==ESP_OK);
    assert(esp_event_handler_instance_register(TEST_EVENT,ESP_EVENT_ANY_ID,old_handler,NULL,&old_instances[1])==ESP_OK);
    assert(esp_event_handler_instance_register(TEST_EVENT,42,old_handler,NULL,&old_instances[2])==ESP_OK);
    assert(esp_event_handler_register(TEST_EVENT,88,legacy_handler,NULL)==ESP_OK);
    assert(esp_event_post(TEST_EVENT,42,NULL,0,0)==ESP_OK);
    dispatch(1); deny_allocations=false;
    assert(old_calls==1 && node_count(true)==0 && node_count(false)==4);
    assert(loop()->running_task==NULL && loop()->queue->count==8);
    /* Profiling在handler返回后访问旧节点；ASan保证回收发生在此之后。 */
    dispatch(portMAX_DELAY); assert(node_count(false)==3);
    memset(new_calls,0,sizeof(new_calls));
    assert(esp_event_post(TEST_EVENT,42,NULL,0,0)==ESP_OK); dispatch(portMAX_DELAY);
    assert(new_calls[0]==1 && new_calls[1]==1 && new_calls[2]==1);
    FILE *report=tmpfile(); assert(report); assert(esp_event_dump(report)==ESP_OK); assert(!fclose(report));
    for(unsigned i=0;i<3;i++) assert(esp_event_handler_instance_unregister(i?TEST_EVENT:ESP_EVENT_ANY_BASE,i==2?42:ESP_EVENT_ANY_ID,new_instances[i])==ESP_OK);
    assert(node_count(false)==0); assert(esp_event_loop_delete_default()==ESP_OK);
}
static void test_three_real_start_service_registration_failures_rollback_and_retry(void) {
    /* 默认loop在主线程存活，逐个失败后完整deinit，各次都回loop分配基线。
     * 然后成功启动/停止；同一loop的连续失败不积累PoP或订阅。 */
    assert(esp_event_loop_create_default()==ESP_OK); size_t loop_baseline=live_allocations;
    for(unsigned point=0;point<3;point++) {
        assert(network_prov_mgr_init(config())==ESP_OK);
        registration_failure=(long)point*4;
        size_t failures=allocation_failures;
        assert(network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1,"012345","PROV_ABCDEF",NULL)==ESP_ERR_NO_MEM);
        assert(allocation_failures==failures+1 && node_count(false)==0);
        assert(!prov_ctx->wifi_event_instance && !prov_ctx->ip_event_instance && !prov_ctx->stop_event_instance);
        assert(!prov_ctx->pc && !prov_ctx->protocomm_sec_params);
        assert(network_prov_mgr_deinit()==ESP_OK); dispatch(portMAX_DELAY);
        assert(live_allocations==loop_baseline);
    }
    assert(network_prov_mgr_init(config())==ESP_OK); stop_and_dispatch(start()); teardown();
    for(unsigned point=1;point<=3;point++) {
        uint32_t old=fresh(); unsigned before=ends;
        restart_failure=point; reopen_on_end=true;
        network_prov_mgr_stop_provisioning(); cleanup_delay_timer_cb((void *)(uintptr_t)old); dispatch(portMAX_DELAY);
        assert(prov_ctx->prov_state==NETWORK_PROV_STATE_SUCCESS && ends==before+1);
        assert(node_count(false)==3 && node_count(true)==0);
        stop_and_dispatch(prov_ctx->cleanup_token); teardown();
    }
}
static void *event_thread(void *arg) {
    (void)arg; assert(!pthread_mutex_lock(&barrier_mutex)); await_flag(&event_has_loop); assert(!pthread_mutex_unlock(&barrier_mutex));
    event_actor=true; dispatch(portMAX_DELAY); event_actor=false; return NULL;
}
static void *stop_thread(void *arg) {
    (void)arg; network_prov_mgr_stop_provisioning();
    assert(!pthread_mutex_lock(&barrier_mutex)); stop_done=true; signal_barrier(); assert(!pthread_mutex_unlock(&barrier_mutex)); return NULL;
}
static void *endpoint_thread(void *arg) {
    (void)arg; assert(!pthread_mutex_lock(&barrier_mutex)); await_flag(&endpoint_go); assert(!pthread_mutex_unlock(&barrier_mutex));
    network_prov_wifi_sta_state_t state; assert(network_prov_mgr_get_wifi_state(&state)==ESP_OK);
    assert(!pthread_mutex_lock(&barrier_mutex)); endpoint_done=true; signal_barrier(); assert(!pthread_mutex_unlock(&barrier_mutex)); return NULL;
}
static void test_existing_lock_barriers_with_actual_dispatch_and_unregister(void) {
    uint32_t token=fresh();
    wifi_event_sta_disconnected_t data={WIFI_REASON_AUTH_FAIL};
    assert(esp_event_post(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&data,sizeof(data),0)==ESP_OK);
    arm_lock_race=true; event_has_loop=false; event_waiting=false; stop_done=false;
    pthread_t event,stop; assert(!pthread_create(&event,NULL,event_thread,NULL)); assert(!pthread_create(&stop,NULL,stop_thread,NULL));
    assert(!pthread_mutex_lock(&barrier_mutex)); await_flag(&stop_done); assert(!pthread_mutex_unlock(&barrier_mutex));
    assert(!pthread_join(stop,NULL)); assert(!pthread_join(event,NULL));
    assert(prov_ctx->prov_state==NETWORK_PROV_STATE_STOPPING);
    ble_race=true; endpoint_go=false; endpoint_done=false; pthread_t endpoint;
    assert(!pthread_create(&endpoint,NULL,endpoint_thread,NULL));
    cleanup_delay_timer_cb((void *)(uintptr_t)token); dispatch(portMAX_DELAY);
    assert(!pthread_join(endpoint,NULL)); ble_race=false;
    assert(prov_ctx->prov_state==NETWORK_PROV_STATE_IDLE); teardown();
}
static void test_unregister_invariant_failure_preserves_handle_without_end_or_free(void) {
    uint32_t token=fresh(); (void)token;
    esp_event_handler_instance_t *fields[]={&prov_ctx->wifi_event_instance,&prov_ctx->ip_event_instance,&prov_ctx->stop_event_instance};
    esp_event_base_t bases[]={WIFI_EVENT,IP_EVENT,NETWORK_PROV_MGR_PVT_EVENT};
    int32_t ids[]={ESP_EVENT_ANY_ID,IP_EVENT_STA_GOT_IP,NETWORK_PROV_MGR_STOP};
    for(unsigned i=0;i<3;i++) {
        esp_event_handler_instance_t saved=*fields[i]; *fields[i]=(void *)(uintptr_t)0xbad;
        unsigned before=ends,deleted=pc_deletes; size_t live=live_allocations;
        fatal_expected=true;
        if(!setjmp(fatal_jump)) { unregister_service_instance(bases[i],ids[i],fields[i]); assert(false); }
        fatal_expected=false;
        assert(*fields[i]==(void *)(uintptr_t)0xbad && ends==before && pc_deletes==deleted && live_allocations==live);
        *fields[i]=saved;
    }
    esp_event_loop_handle_t saved=s_default_loop; s_default_loop=NULL;
    esp_event_handler_instance_t handle=prov_ctx->stop_event_instance;
    fatal_expected=true;
    if(!setjmp(fatal_jump)) { unregister_service_instance(NETWORK_PROV_MGR_PVT_EVENT,NETWORK_PROV_MGR_STOP,&prov_ctx->stop_event_instance); assert(false); }
    fatal_expected=false; assert(prov_ctx->stop_event_instance==handle); s_default_loop=saved;
    stop_and_dispatch(prov_ctx->cleanup_token); teardown(); assert(fatal_count==4);
}
static void test_repeated_loop_delete_recreate_returns_to_allocation_baseline(void) {
    size_t baseline=live_allocations;
    for(unsigned i=0;i<40;i++) {
        uint32_t token=fresh();
        if(i%2) { reopen_on_end=true; network_prov_mgr_stop_provisioning(); cleanup_delay_timer_cb((void *)(uintptr_t)token); dispatch(portMAX_DELAY); token=prov_ctx->cleanup_token; }
        stop_and_dispatch(token); teardown(); assert(live_allocations==baseline);
    }
}
static void test_registration_oom_drains_live_endpoint_tables_before_free(void) {
    for(unsigned point=0;point<3;++point) {
        assert(esp_event_loop_create_default()==ESP_OK);
        assert(network_prov_mgr_init(config())==ESP_OK);
        rollback_race=true; endpoint_entered=endpoint_go=endpoint_done=false; endpoint_pc=NULL;
        unsigned visits=endpoint_visits,deleted=pc_deletes,before=ends;
        pthread_t endpoint; assert(!pthread_create(&endpoint,NULL,rollback_endpoint_thread,NULL));
        registration_failure=(long)point*4;
        assert(network_prov_mgr_start_provisioning(1,"012345","PROV_ABCDEF",NULL)==ESP_ERR_NO_MEM);
        assert(!pthread_join(endpoint,NULL)); rollback_race=false; endpoint_pc=NULL;
        assert(endpoint_visits==visits+3 && pc_deletes==deleted+1 && ends==before);
        assert(prov_ctx->prov_state==NETWORK_PROV_STATE_IDLE && !prov_ctx->pc && !prov_ctx->protocomm_sec_params);
        assert(!prov_ctx->autostop_timer && !prov_ctx->wifi_connect_timer && !prov_ctx->cleanup_delay_timer);
        assert(network_prov_mgr_get_cleanup_error()==ESP_OK && !s_deinitializing);
        teardown();
    }
}
static void test_async_stop_error_retains_dependencies_and_blocks_automatic_retries(void) {
    uint32_t token=fresh(); unsigned before=ends,deleted=pc_deletes,stops=transport_stops;
    protocomm_t *pc=prov_ctx->pc; const void *pop=prov_ctx->protocomm_sec_params;
    void *table=prov_ctx->network_prov_handlers;
    stop_failures=1; network_prov_mgr_stop_provisioning();
    cleanup_delay_timer_cb((void *)(uintptr_t)token); dispatch(portMAX_DELAY);
    assert(network_prov_mgr_get_cleanup_error()==ESP_FAIL && prov_ctx->cleanup_error==ESP_FAIL);
    assert(prov_ctx->pc==pc && prov_ctx->protocomm_sec_params==pop && prov_ctx->network_prov_handlers==table);
    assert(!prov_ctx->cleanup_running && !prov_ctx->cleanup_done && prov_ctx->end_pending);
    assert(prov_ctx->prov_state==NETWORK_PROV_STATE_STOPPING && ends==before && pc_deletes==deleted);
    visit_endpoints(pc); /* real endpoint route still references the retained tables and PoP */
    for(unsigned i=0;i<3;++i) {
        stop_prov_timer_cb((void *)(uintptr_t)token); cleanup_delay_timer_cb((void *)(uintptr_t)token);
        network_prov_mgr_stop_provisioning(); dispatch(portMAX_DELAY);
    }
    assert(transport_stops==stops+1 && ends==before);
    assert(network_prov_mgr_start_provisioning(1,"012345","PROV_ABCDEF",NULL)==ESP_ERR_INVALID_STATE);
    stop_failures=1;
    assert(network_prov_mgr_deinit()==ESP_FAIL && !s_deinitializing && prov_ctx->pc==pc);
    assert(transport_stops==stops+2 && pc_deletes==deleted && ends==before);
    assert(network_prov_mgr_deinit()==ESP_OK && !s_deinitializing && !prov_ctx);
    assert(transport_stops==stops+3 && pc_deletes==deleted+1 && ends==before+1);
    assert(network_prov_mgr_deinit()==ESP_OK && ends==before+1);
    assert(esp_event_loop_delete_default()==ESP_OK);
}
static void test_session_only_stop_failure_keeps_session_pc_and_pop_until_explicit_success(void) {
    assert(esp_event_loop_create_default()==ESP_OK);
    network_prov_mgr_config_t cfg=config(); cfg.mode=NETWORK_PROV_MODE_SESSION_ONLY;
    assert(network_prov_mgr_init(cfg)==ESP_OK);
    assert(network_prov_mgr_start_provisioning(1,"012345","PROV_ABCDEF",NULL)==ESP_OK);
    protocomm_t *pc=prov_ctx->session_pc; const void *pop=prov_ctx->protocomm_sec_params;
    unsigned before=ends,deleted=pc_deletes;
    assert(pc && pop && !prov_ctx->pc && !prov_ctx->end_pending);
    stop_failures=2;
    for(unsigned i=0;i<2;++i) {
        assert(network_prov_mgr_deinit()==ESP_FAIL && !s_deinitializing);
        assert(prov_ctx->session_pc==pc && prov_ctx->protocomm_sec_params==pop);
        assert(prov_ctx->prov_state==NETWORK_PROV_STATE_STOPPING && !prov_ctx->cleanup_running);
        assert(network_prov_mgr_get_cleanup_error()==ESP_FAIL && ends==before && pc_deletes==deleted);
        const protocomm_security1_params_t *params=pc->sec_params;
        assert(params && !memcmp(params->data,"012345",6));
    }
    assert(network_prov_mgr_deinit()==ESP_OK && !prov_ctx && !s_deinitializing);
    assert(ends==before && pc_deletes==deleted+1 && network_prov_mgr_get_cleanup_error()==ESP_OK);
    assert(esp_event_loop_delete_default()==ESP_OK);
}
static void test_blocking_deinit_error_and_full_observation_queue_return_ownership_truth(void) {
    fresh(); dispatch(portMAX_DELAY); unsigned before=ends,after_deinit=deinits;
    stop_failures=1;
    assert(network_prov_mgr_deinit()==ESP_FAIL && !s_deinitializing && prov_ctx);
    assert(prov_ctx->end_pending && ends==before && deinits==after_deinit);
    fill_queue(); unsigned drops=queue_failures;
    assert(network_prov_mgr_deinit()==ESP_OK && !prov_ctx && !s_deinitializing);
    assert(ends==before+1 && deinits==after_deinit+1 && queue_failures==drops+2);
    assert(esp_event_loop_delete_default()==ESP_OK);
    uint32_t token=fresh(); stop_and_dispatch(token); before=ends;
    fill_queue(); drops=queue_failures;
    assert(network_prov_mgr_deinit()==ESP_OK && !prov_ctx && ends==before && queue_failures==drops+1);
    assert(esp_event_loop_delete_default()==ESP_OK);
}
static void test_deinit_early_rejection_does_not_steal_or_strand_owner(void) {
    fresh();
    prov_ctx->prov_state=NETWORK_PROV_STATE_STARTING;
    assert(network_prov_mgr_deinit()==ESP_ERR_INVALID_STATE && !s_deinitializing);
    prov_ctx->prov_state=NETWORK_PROV_STATE_STOPPING; prov_ctx->cleanup_running=true;
    assert(network_prov_mgr_deinit()==ESP_ERR_INVALID_STATE && !s_deinitializing);
    prov_ctx->cleanup_running=false;
    assert(network_prov_mgr_deinit()==ESP_ERR_INVALID_STATE && !s_deinitializing);
    s_deinitializing=true;
    assert(network_prov_mgr_deinit()==ESP_ERR_INVALID_STATE && s_deinitializing);
    s_deinitializing=false; prov_ctx->prov_state=NETWORK_PROV_STATE_SUCCESS;
    assert(network_prov_mgr_deinit()==ESP_OK && !s_deinitializing && !prov_ctx);
    assert(network_prov_mgr_deinit()==ESP_OK && !s_deinitializing);
    assert(esp_event_loop_delete_default()==ESP_OK);
}
static unsigned controller_restores,controller_releases,controller_deinits;
static pthread_mutex_t controller_mutex=PTHREAD_MUTEX_INITIALIZER;
static int ctl_prepare(void *p,uint32_t generation) { (void)p; assert(generation); return esp_event_loop_create_default(); }
static int ctl_ok(void *p) { (void)p; return 0; }
static int ctl_identity(void *p,char name[12],char pop[17]) { (void)p; memcpy(name,"PROV_ABCDEF",12); memcpy(pop,"012345",7); return 0; }
static int ctl_init(void *p) { (void)p; return network_prov_mgr_init(config()); }
static int ctl_start(void *p,const char *name,const char *pop) { (void)p; return network_prov_mgr_start_provisioning(1,pop,name,NULL); }
static void ctl_stop(void *p) { (void)p; network_prov_mgr_stop_provisioning(); }
static int ctl_deinit(void *p) { (void)p; ++controller_deinits; return network_prov_mgr_deinit(); }
static int ctl_query(void *p) { (void)p; return network_prov_mgr_get_cleanup_error(); }
static int ctl_restore(void *p) { (void)p; ++controller_restores; return 0; }
static int ctl_release(void *p) { (void)p; ++controller_releases; return esp_event_loop_delete_default(); }
static void ctl_lock(void *p) { (void)p; assert(!pthread_mutex_lock(&controller_mutex)); }
static void ctl_unlock(void *p) { (void)p; assert(!pthread_mutex_unlock(&controller_mutex)); }
static void ctl_create(void) {
    BalabalaNetworkOps ops={ctl_prepare,ctl_ok,ctl_ok,ctl_identity,ctl_init,ctl_start,ctl_stop,ctl_deinit,ctl_query,ctl_restore,ctl_release,ctl_lock,ctl_unlock,NULL};
    balabala_network_init(&network,&ops); use_controller_bridge=true;
    controller_restores=controller_releases=controller_deinits=0;
}
static void ctl_ordinary_requests_cannot_retry(void) {
    unsigned stops=transport_stops;
    uint32_t requests[]={0,BALABALA_REQUEST_NETWORK_CANCEL,BALABALA_REQUEST_SHUTDOWN,BALABALA_REQUEST_NETWORK_START};
    for(unsigned i=0;i<4;++i) {
        balabala_network_publish(&network,network.generation,BALABALA_NET_EVENT_END|BALABALA_NET_EVENT_SUCCESS);
        balabala_network_step(&network,requests[i],100+i);
    }
    assert(transport_stops==stops && !controller_deinits && !controller_restores && !controller_releases);
}
static void test_real_start_rollback_stop_failure_controller_query_preserves_all_ownership(void) {
    ctl_create(); registration_failure=0; stop_failures=1; unsigned before=ends;
    balabala_network_step(&network,BALABALA_REQUEST_NETWORK_START,0);
        (void)balabala_network_advance(&network,network.generation,1,0);
        (void)balabala_network_advance(&network,network.generation,2,0);
    assert(network.active && network.manager && network.fault && network.saved);
    assert(network.error==ESP_ERR_NO_MEM && network.cleanup_error==ESP_FAIL && !network.success);
    assert(!network.name[0] && !network.pop[0] && network.status==BALABALA_NETWORK_CLEANUP_FAILED);
    assert(prov_ctx->pc && prov_ctx->protocomm_sec_params && !prov_ctx->end_pending);
    ctl_ordinary_requests_cannot_retry();
    stop_failures=1;
    assert(balabala_network_retry_cleanup(&network)==ESP_FAIL && network.active && !s_deinitializing);
    assert(!controller_restores && !controller_releases);
    assert(balabala_network_retry_cleanup(&network)==ESP_OK);
    assert(!network.active && !network.manager && !network.cleanup_error && network.fault);
    assert(controller_deinits==2 && controller_restores==1 && controller_releases==1 && ends==before);
    assert(balabala_network_retry_cleanup(&network)==-1 && controller_releases==1);
    use_controller_bridge=false;
}
static void test_same_batch_success_and_cleanup_error_cancel_shutdown_end_keep_new_credentials(void) {
    for(unsigned shut=0;shut<2;++shut) {
        ctl_create(); balabala_network_step(&network,BALABALA_REQUEST_NETWORK_START,0);
        (void)balabala_network_advance(&network,network.generation,1,0);
        (void)balabala_network_advance(&network,network.generation,2,0);
        uint32_t token=prov_ctx->cleanup_token;
        network_prov_mgr_stop_provisioning(); stop_failures=1;
        cleanup_delay_timer_cb((void *)(uintptr_t)token); dispatch(portMAX_DELAY);
        balabala_network_publish(&network,network.generation,BALABALA_NET_EVENT_SUCCESS|BALABALA_NET_EVENT_END);
        balabala_network_step(&network,shut?BALABALA_REQUEST_SHUTDOWN:BALABALA_REQUEST_NETWORK_CANCEL,1);
        assert(network.success && network.cleanup_error && network.active && network.manager);
        ctl_ordinary_requests_cannot_retry();
        assert(balabala_network_retry_cleanup(&network)==ESP_OK);
        assert(!controller_restores && controller_releases==1 && network.fault && !network.active);
        assert(balabala_network_retry_cleanup(&network)==-1);
        use_controller_bridge=false;
    }
}

int main(void) {
    /* 先建立跨会话保留的唯一manager mutex，其余分配必须回基线。 */
    uint32_t warm=fresh(); stop_and_dispatch(warm); teardown(); size_t baseline=live_allocations; assert(baseline==1);
#define RUN(fn) do { fn(); assert(live_allocations==baseline); puts("ok - " #fn); } while(0)
    RUN(test_stop_sync_reopen_second_stop_uses_real_registered_instance);
    RUN(test_full_queue_no_alloc_unregister);
    RUN(test_identity_idempotence_tick_profiling_and_legacy);
    RUN(test_three_real_start_service_registration_failures_rollback_and_retry);
    RUN(test_existing_lock_barriers_with_actual_dispatch_and_unregister);
    RUN(test_unregister_invariant_failure_preserves_handle_without_end_or_free);
    RUN(test_repeated_loop_delete_recreate_returns_to_allocation_baseline);
    RUN(test_registration_oom_drains_live_endpoint_tables_before_free);
    RUN(test_async_stop_error_retains_dependencies_and_blocks_automatic_retries);
    RUN(test_session_only_stop_failure_keeps_session_pc_and_pop_until_explicit_success);
    RUN(test_blocking_deinit_error_and_full_observation_queue_return_ownership_truth);
    RUN(test_deinit_early_rejection_does_not_steal_or_strand_owner);
    RUN(test_real_start_rollback_stop_failure_controller_query_preserves_all_ownership);
    RUN(test_same_batch_success_and_cleanup_error_cancel_shutdown_end_keep_new_credentials);
    assert(queue_waits==0 && !prov_ctx && !s_default_loop && pc_deletes+stop_errors==transport_stops);
    printf("event integration tests passed: 14; profiling=1; real FIFO=8; retained manager mutex=%zu; allocations=%zu; injected failures=%zu; queue drops=%u; reopens=%u\n",baseline,allocation_calls,allocation_failures,(unsigned)queue_failures,reopens);
    vSemaphoreDelete(prov_ctx_lock); prov_ctx_lock=NULL; assert(live_allocations==0);
    return 0;
}
