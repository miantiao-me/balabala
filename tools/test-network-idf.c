/* 执行同一份IDF适配源码，故障注入仅替换外部IDF调用。 */
#define BALABALA_NETWORK_IDF_MOCK
#include "../firmware/main/balabala_network_idf.c"
#include <assert.h>
#include <stdio.h>

enum { NVS=1, NETIF, LOOP, STA, ATTACH, STA_HANDLERS, HANDLER, WIFI, STORAGE, MODE, GET_CONFIG, RADIO,
       MAC, MANAGER, ADVERTISE, WIFI_STOP, SET_CONFIG, MANAGER_DEINIT, WIFI_DEINIT,
       UNREGISTER, CLEAR_STA, DELETE_LOOP, NVS_DEINIT, LAST };
static struct {
    unsigned fail, calls[LAST];
    int cleanup_error, storage;
    unsigned persistent_writes;
    uint32_t random_values[6];
    unsigned random_count, random_used;
    bool nvs,loop,sta,attached,sta_handlers,handler,wifi,radio,manager,advertising;
    wifi_config_t config;
    mock_event_handler callback;
    mock_handler app;
    void (*advertising_callback)(void *, int);
    void *advertising_context;
} mock;
static esp_netif_t sta;
static int invoked(unsigned which) { ++mock.calls[which]; return mock.fail==which ? -(int)which : 0; }
void esp_log_level_set(const char *name,int level) { assert(name && level==ESP_LOG_NONE); }
esp_err_t nvs_flash_init(void) { int r=invoked(NVS); mock.nvs=!r; return r; }
esp_err_t nvs_flash_deinit(void) { int r=invoked(NVS_DEINIT); if(!r)mock.nvs=false; return r; }
esp_err_t esp_netif_init(void) { return invoked(NETIF); }
esp_err_t esp_event_loop_create_default(void) { assert(!mock.loop); int r=invoked(LOOP); mock.loop=!r; return r; }
esp_err_t esp_event_loop_delete_default(void) { int r=invoked(DELETE_LOOP); if(!r){ mock.loop=false; mock.callback=NULL; } return r; }
esp_netif_t *esp_netif_new(const esp_netif_config_t *cfg) { assert(cfg); mock.sta=!invoked(STA); return mock.sta?&sta:NULL; }
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *p) {
    assert(p==&sta && mock.sta); mock.attached=true; /* IDF先登记，再执行可能失败的attach。 */
    return invoked(ATTACH);
}
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void) {
    assert(mock.attached); mock.sta_handlers=true; /* 注册部分成功后也可能失败。 */
    return invoked(STA_HANDLERS);
}
void esp_netif_destroy(esp_netif_t *p) { assert(p==&sta && !mock.attached && !mock.sta_handlers); mock.sta=false; }
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int32_t id,mock_event_handler cb,void *ctx,esp_event_handler_instance_t *instance) {
    assert(base && id==ESP_EVENT_ANY_ID && !ctx); int r=invoked(HANDLER);
    if(!r) { mock.handler=true; mock.callback=cb; *instance=&mock; } return r;
}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,int32_t id,esp_event_handler_instance_t instance) {
    assert(base && id==ESP_EVENT_ANY_ID && instance==&mock); int r=invoked(UNREGISTER); if(!r)mock.handler=false; return r;
}
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg) { assert(cfg); int r=invoked(WIFI); mock.wifi=!r; return r; }
esp_err_t esp_wifi_deinit(void) { int r=invoked(WIFI_DEINIT); if(!r)mock.wifi=mock.radio=false; return r; }
esp_err_t esp_wifi_set_storage(int storage) { assert(storage==BALABALA_WIFI_STORAGE); int r=invoked(STORAGE); if(!r)mock.storage=storage; return r; }
esp_err_t esp_wifi_set_mode(int mode) { assert(mode==WIFI_MODE_STA); return invoked(MODE); }
esp_err_t esp_wifi_get_config(int iface,wifi_config_t *cfg) { assert(iface==WIFI_IF_STA); int r=invoked(GET_CONFIG); if(!r)*cfg=mock.config; return r; }
esp_err_t esp_wifi_set_config(int iface,const wifi_config_t *cfg) { assert(iface==WIFI_IF_STA && !mock.radio); int r=invoked(SET_CONFIG); if(!r) { mock.config=*cfg; if(mock.storage==WIFI_STORAGE_FLASH) ++mock.persistent_writes; } return r; }
esp_err_t esp_wifi_start(void) { assert(mock.wifi); int r=invoked(RADIO); mock.radio=!r; return r; }
esp_err_t esp_wifi_stop(void) { int r=invoked(WIFI_STOP); if(!r)mock.radio=false; return r; }
esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(esp_netif_t *p) {
    assert(p==&sta && mock.attached); int r=invoked(CLEAR_STA); if(!r)mock.attached=mock.sta_handlers=false; return r;
}
esp_err_t esp_read_mac(uint8_t *mac,int iface) { assert(iface==ESP_MAC_WIFI_STA); memset(mac,0xab,6); return invoked(MAC); }
void esp_fill_random(void *bytes,size_t length) {
    assert(mock.radio && length==sizeof(uint32_t));
    uint32_t value=12345+mock.random_used;
    if(mock.random_count) {
        assert(mock.random_used<mock.random_count);
        value=mock.random_values[mock.random_used];
    }
    ++mock.random_used;
    memcpy(bytes,&value,length);
}
esp_err_t network_prov_mgr_init(network_prov_mgr_config_t config) {
    assert(config.scheme==network_prov_scheme_ble && config.mode==NETWORK_PROV_MODE_FULL);
    assert(!config.scheme_event_handler.event_cb && config.app_event_handler.event_cb);
    mock.app=config.app_event_handler;
    int r=invoked(MANAGER); mock.manager=!r; return r;
}
esp_err_t protocomm_ble_set_advertising_callback(void (*callback)(void *, int), void *context) {
    mock.advertising_callback=callback; mock.advertising_context=context; return 0;
}
esp_err_t network_prov_mgr_start_provisioning(int security,const void *pop,const char *name,const char *key) {
    assert(security==NETWORK_PROV_SECURITY_1 && strlen(pop)==6 && strlen(name)==11 && !key);
    assert(!strcmp(pop,network.pop) && !strcmp(name,network.name));
    int r=invoked(ADVERTISE); mock.advertising=!r; return r;
}
void network_prov_mgr_stop_provisioning(void) { assert(mock.manager); mock.advertising=false; }
esp_err_t network_prov_mgr_get_cleanup_error(void) { return mock.cleanup_error; }
esp_err_t network_prov_mgr_deinit(void) { int r=invoked(MANAGER_DEINIT); if(!r) mock.manager=mock.advertising=false; return r; }
static BalabalaNetwork *fresh(unsigned failure) {
    memset(&driver,0,sizeof(driver)); memset(&mock,0,sizeof(mock));
    mock.fail=failure; memcpy(mock.config.ssid,"old-test-network",17); memcpy(mock.config.password,"old-test-value",15);
    return balabala_network_idf_init();
}
static void start_staged(BalabalaNetwork *n, uint64_t now) {
    balabala_network_step(n,BALABALA_REQUEST_NETWORK_START,now);
    if(n->phase==1) (void)balabala_network_advance(n,n->generation,1,now);
    if(n->phase==2) (void)balabala_network_advance(n,n->generation,2,now);
}
static void assert_released(BalabalaNetwork *n) {
    assert(n->released && !n->active && !mock.nvs && !mock.loop && !mock.sta && !mock.attached && !mock.sta_handlers && !mock.handler);
    assert(!mock.wifi && !mock.radio && !mock.manager && !mock.advertising && !mock.callback);
    const unsigned char *bytes=(const unsigned char *)&driver.old;
    for(unsigned i=0;i<sizeof(driver.old);++i) assert(!bytes[i]);
    for(unsigned i=0;i<sizeof(n->pop);++i) assert(!n->pop[i]);
    assert(!n->name[0] && !channel_lock);
}
static void terminal_event(BalabalaNetwork *n,int32_t id) {
    assert(mock.callback);
    /* 无效凭据指针证明回调既不读取也不复制event_data。 */
    unsigned deinits=mock.calls[MANAGER_DEINIT];
    mock.app.event_cb(mock.app.user_data,id,(void *)(uintptr_t)1);
    assert(n->active && mock.calls[MANAGER_DEINIT]==deinits);
}
static void finish_cancel(BalabalaNetwork *n) {
    balabala_network_step(n,BALABALA_REQUEST_NETWORK_CANCEL,20);
    terminal_event(n,NETWORK_PROV_END); balabala_network_step(n,0,21); assert_released(n);
}
static void test_startup_each_idf_failure_cleans_acquired_resources(void) {
    for(unsigned failure=NVS;failure<=ADVERTISE;++failure) {
        BalabalaNetwork *n=fresh(failure);
        start_staged(n,0);
        assert_released(n); assert(n->status==BALABALA_NETWORK_FAILED);
        assert(!memcmp(mock.config.ssid,"old-test-network",17));
        assert(!mock.calls[SET_CONFIG] || failure>GET_CONFIG);
        if (failure==ATTACH || failure==STA_HANDLERS) assert(mock.calls[CLEAR_STA]==1);
        if (failure<=STA) assert(!mock.calls[CLEAR_STA]);
        if(failure!=STORAGE) {
            assert(!n->fault); mock.fail=0;
            start_staged(n,100); assert(n->active); finish_cancel(n);
        }
    }
}
static void test_adapter_success_window_rollbacks_and_reopens(void) {
    BalabalaNetwork *n=fresh(0); start_staged(n,0);
    assert(!strcmp(n->name,"PROV_ABABAB") && !strcmp(n->pop,"012345"));
    memset(&mock.config,0xee,sizeof(mock.config));
    terminal_event(n,NETWORK_PROV_WIFI_CRED_SUCCESS); balabala_network_step(n,0,10);
    assert(n->success && mock.advertising && !mock.calls[MANAGER_DEINIT]);
    terminal_event(n,NETWORK_PROV_END); balabala_network_step(n,0,11); assert_released(n);
    assert(!mock.calls[SET_CONFIG] && mock.config.password[0]==0xee);
    memset(mock.calls,0,sizeof(mock.calls));
    start_staged(n,100);
    assert(n->active && mock.calls[NETIF]==0); /* lwIP基础层只初始化一次。 */
    memset(&mock.config,0xdd,sizeof(mock.config));
    finish_cancel(n); assert(mock.config.password[0]==0xee);
}
static void test_six_digit_identity_random_boundaries_clear_and_reopen(void) {
    BalabalaNetwork *n=fresh(0);
    const uint32_t values[]={0,999999,4293999999u,4294000000u,UINT32_MAX,1000000};
    const char *expected[]={"000000","999999","999999","000000"};
    const unsigned consumed[]={1,2,3,6};
    memcpy(mock.random_values,values,sizeof(values)); mock.random_count=6;
    uint32_t generation=0;
    for(unsigned round=0;round<4;++round) {
        balabala_network_step(n,BALABALA_REQUEST_NETWORK_START,100*round);
        assert(n->phase==1 && n->generation!=generation && !n->name[0] && !n->pop[0]);
        generation=n->generation;
        memset(n->pop+6,0xa5,sizeof(n->pop)-6);
        assert(!balabala_network_advance(n,generation,1,100*round));
        assert(n->phase==2 && !strcmp(n->name,"PROV_ABABAB"));
        assert(!strcmp(n->pop,expected[round]) && mock.random_used==consumed[round]);
        for(unsigned i=6;i<sizeof(n->pop);++i) assert(!n->pop[i]);
        assert(!balabala_network_advance(n,generation,2,100*round));
        assert(n->started && mock.advertising);
        finish_cancel(n);
    }
}
static void test_adapter_cleanup_failure_is_reported_and_blocks_restart(void) {
    for(unsigned failure=WIFI_STOP;failure<LAST;++failure) {
        if(failure==MANAGER_DEINIT || failure==WIFI_DEINIT || failure==UNREGISTER ||
           failure==CLEAR_STA || failure==DELETE_LOOP || failure==NVS_DEINIT ||
           failure==WIFI_STOP || failure==SET_CONFIG) {
            BalabalaNetwork *n=fresh(0); start_staged(n,0);
            mock.fail=failure;
            if(failure==MANAGER_DEINIT) {
                balabala_network_step(n,BALABALA_REQUEST_NETWORK_CANCEL,20);
                terminal_event(n,NETWORK_PROV_END); balabala_network_step(n,0,21);
                assert(n->active && n->manager && n->cleanup_error && mock.manager && mock.wifi);
                assert(!mock.calls[SET_CONFIG] && !mock.calls[WIFI_DEINIT] && !mock.calls[DELETE_LOOP]);
                assert(balabala_network_retry_cleanup(n)==-(int)failure && n->active);
                mock.fail=0; assert(!balabala_network_retry_cleanup(n)); assert_released(n);
            } else {
                balabala_network_step(n,BALABALA_REQUEST_NETWORK_CANCEL,20);
                terminal_event(n,NETWORK_PROV_END); balabala_network_step(n,0,21);
                if(failure==SET_CONFIG) assert_released(n);
                else {
                    assert(n->active && !n->released && n->cleanup_error && !n->manager);
                    assert(n->status==BALABALA_NETWORK_CLEANUP_FAILED);
                    unsigned calls[LAST]; memcpy(calls,mock.calls,sizeof(calls));
                    for(unsigned i=0;i<10;++i) balabala_network_step(n,BALABALA_REQUEST_NETWORK_CANCEL|BALABALA_REQUEST_SHUTDOWN,30+i);
                    assert(!memcmp(calls,mock.calls,sizeof(calls)));
                }
            }
            assert(n->fault && n->error==-(int)failure);
            start_staged(n,100); assert(mock.calls[ADVERTISE]==1);
        }
    }
}
static void test_only_generation_bound_sync_terminals_are_authoritative(void) {
    BalabalaNetwork *n=fresh(0); start_staged(n,0);
    mock_handler old=mock.app;
    /* 默认队列可迟到或全丢；终态仅从同步桥接消费。 */
    mock.callback(NULL,NETWORK_PROV_EVENT,NETWORK_PROV_WIFI_CRED_FAIL,(void *)(uintptr_t)1);
    mock.callback(NULL,NETWORK_PROV_EVENT,NETWORK_PROV_END,(void *)(uintptr_t)1);
    balabala_network_step(n,0,1); assert(n->active && !n->stopping);
    memset(&mock.config,0xee,sizeof(mock.config));
    terminal_event(n,NETWORK_PROV_WIFI_CRED_SUCCESS);
    terminal_event(n,NETWORK_PROV_END);
    balabala_network_step(n,BALABALA_REQUEST_NETWORK_CANCEL,2);
    assert_released(n); assert(n->success && !mock.calls[SET_CONFIG]);
    start_staged(n,100);
    assert(mock.app.user_data!=old.user_data);
    old.event_cb(old.user_data,NETWORK_PROV_WIFI_CRED_SUCCESS,(void *)(uintptr_t)1);
    old.event_cb(old.user_data,NETWORK_PROV_END,(void *)(uintptr_t)1);
    mock.callback(NULL,NETWORK_PROV_EVENT,NETWORK_PROV_WIFI_CRED_SUCCESS,NULL);
    mock.callback(NULL,NETWORK_PROV_EVENT,NETWORK_PROV_WIFI_CRED_FAIL,NULL);
    mock.callback(NULL,NETWORK_PROV_EVENT,NETWORK_PROV_END,NULL);
    balabala_network_step(n,0,101); assert(n->active && !n->stopping && !n->success);
    finish_cancel(n);
}
static void test_cleanup_query_retains_idf_resources_until_manual_deinit(void) {
    BalabalaNetwork *n=fresh(0); start_staged(n,0);
    mock.cleanup_error=-81; balabala_network_step(n,BALABALA_REQUEST_SHUTDOWN,1);
    assert(n->active && n->manager && n->cleanup_error==-81 && n->fault);
    assert(mock.manager && mock.wifi && mock.nvs && mock.loop && mock.sta);
    assert(!mock.calls[MANAGER_DEINIT] && !mock.calls[SET_CONFIG] && !mock.calls[WIFI_DEINIT]);
    assert(!n->pop[0] && !n->name[0]);
    for(unsigned i=0;i<10;++i) balabala_network_step(n,BALABALA_REQUEST_SHUTDOWN|BALABALA_REQUEST_NETWORK_CANCEL,2+i);
    assert(!mock.calls[MANAGER_DEINIT] && !mock.calls[SET_CONFIG]);
    mock.cleanup_error=0; assert(!balabala_network_retry_cleanup(n)); assert_released(n);
    assert(mock.calls[MANAGER_DEINIT]==1 && mock.calls[SET_CONFIG]==1 && n->fault);
}
static void test_storage_policy(void) {
    BalabalaNetwork *n=fresh(0); start_staged(n,0);
    assert(mock.calls[STORAGE]==1 && mock.storage==BALABALA_WIFI_STORAGE);
    memset(&mock.config,0,sizeof(mock.config)); /* FULL internally clears RAM configuration. */
    finish_cancel(n);
    assert(mock.calls[STORAGE]==2 && mock.calls[SET_CONFIG]==1);
    assert(!memcmp(mock.config.ssid,"old-test-network",17));
#ifdef BALABALA_AUTOTEST_PHASE2
    assert(mock.persistent_writes==0);
    BalabalaNetworkObservation o; balabala_network_observe(n,&o);
    assert(o.generation==n->generation && o.ends==1 && o.starts==1 && o.stops==1 && o.deinits==1 && o.releases==1);
    uint32_t generation=n->generation;
    balabala_network_publish(n,generation-1,BALABALA_NET_EVENT_END);
    balabala_network_observe(n,&o); assert(o.ends==1);
    balabala_network_publish(n,generation,BALABALA_NET_EVENT_END);
    balabala_network_observe(n,&o); assert(o.ends==2);
    n->observation.ends=UINT32_MAX;
    balabala_network_publish(n,generation,BALABALA_NET_EVENT_END);
    balabala_network_observe(n,&o); assert(o.ends==UINT32_MAX);
#else
    assert(mock.persistent_writes==1);
#endif
}
static void test_advertising_and_recv_are_synchronous_generation_bound(void) {
    BalabalaNetwork *n=fresh(0); start_staged(n,0);
    assert(n->status==BALABALA_NETWORK_PREPARING && n->started);
    void (*old_cb)(void *,int)=mock.advertising_callback; void *old_ctx=mock.advertising_context;
    terminal_event(n,NETWORK_PROV_START); balabala_network_step(n,0,1);
    assert(n->status==BALABALA_NETWORK_PREPARING);
    mock.advertising_callback(mock.advertising_context,0); balabala_network_step(n,0,2);
    assert(n->status==BALABALA_NETWORK_WAITING);
    terminal_event(n,NETWORK_PROV_WIFI_CRED_RECV); balabala_network_step(n,0,3);
    assert(n->status==BALABALA_NETWORK_CONNECTING); finish_cancel(n);
    start_staged(n,100);
    old_cb(old_ctx,0); old_cb(old_ctx,-1); balabala_network_step(n,0,101);
    assert(n->status==BALABALA_NETWORK_PREPARING && !n->stopping);
    mock.advertising_callback(mock.advertising_context,-1); balabala_network_step(n,0,102);
    assert(n->stopping && n->terminal_status==BALABALA_NETWORK_FAILED);
    terminal_event(n,NETWORK_PROV_END); balabala_network_step(n,0,103); assert_released(n);
}
int main(void) {
    test_six_digit_identity_random_boundaries_clear_and_reopen();
    test_advertising_and_recv_are_synchronous_generation_bound();
    test_storage_policy();
    test_cleanup_query_retains_idf_resources_until_manual_deinit();
    test_startup_each_idf_failure_cleans_acquired_resources();
    test_adapter_success_window_rollbacks_and_reopens();
    test_adapter_cleanup_failure_is_reported_and_blocks_restart();
    test_only_generation_bound_sync_terminals_are_authoritative();
    puts("network IDF adapter tests: 8 passed; 0 failed (C ASan/UBSan, 15 startup + 8 cleanup fault points)");
    return 0;
}
