#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
typedef void (*protocomm_ble_advertising_cb_t)(void *, int);
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define BLE_HS_ADV_F_DISC_GEN 1
#define BLE_HS_ADV_F_BREDR_UNSUP 2
#define BLE_GAP_CONN_MODE_UND 1
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_HS_FOREVER -1
#define BLE_HS_EDISABLED 99
struct ble_hs_adv_fields { void *uuids128; int flags, num_uuids128, uuids128_is_complete; };
struct ble_gap_adv_params { int conn_mode, disc_mode, itvl_min, itvl_max; };
struct ble_gap_event { int unused; };
typedef struct { protocomm_ble_advertising_cb_t advertising_cb; void *advertising_context; } simple_ble_cfg_t;
static simple_ble_cfg_t *ble_cfg_p;
static void *protoble_internal;
static protocomm_ble_advertising_cb_t next_advertising_cb;
static void *next_advertising_context;
static struct ble_hs_adv_fields adv_data, resp_data;
static struct ble_gap_adv_params adv_params;
static uint8_t own_addr_type, *protocomm_ble_addr;
static int failed_step, failure_code, calls, notifications, result_seen, started;
static uintptr_t generation_seen;
static int next_step(void) { return ++calls == failed_step ? failure_code : 0; }
static int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *fields) { assert(fields==&adv_data); return next_step(); }
static int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *fields) { assert(fields==&resp_data); return next_step(); }
static int simple_ble_gap_event(struct ble_gap_event *event, void *arg) { (void)event; (void)arg; return 0; }
static int ble_gap_adv_start(uint8_t addr, void *peer, int duration, struct ble_gap_adv_params *params,
                             int (*cb)(struct ble_gap_event *,void *), void *arg) {
    assert(addr==1 && !peer && duration==BLE_HS_FOREVER && params==&adv_params && cb==simple_ble_gap_event && !arg);
    int result=next_step(); if(!result) started++; return result;
}
static int ble_hs_id_set_rnd(uint8_t *addr) { assert(addr==protocomm_ble_addr); return next_step(); }
static int ble_hs_util_ensure_addr(int random) { assert(random==!!protocomm_ble_addr); return next_step(); }
static int ble_hs_id_infer_auto(int privacy, uint8_t *addr) { assert(!privacy); *addr=1; return next_step(); }
#include "../.build/advertising-under-test.inc"
static void observe(void *context, int result) {
    notifications++; generation_seen=(uintptr_t)context; result_seen=result;
    if(!result) assert(started==1); /* 只有实际 adv_start 返回成功后才能观察到就绪。 */
}
static void reset(simple_ble_cfg_t *config, bool random, int failure) {
    static uint8_t address[6];
    calls=notifications=started=0; failed_step=failure; failure_code=42;
    result_seen=-1; generation_seen=0;
    adv_data=(struct ble_hs_adv_fields){.uuids128=config};
    protocomm_ble_addr=random ? address : NULL;
    protoble_internal=NULL; ble_cfg_p=NULL;
    assert(!protocomm_ble_set_advertising_callback(observe,(void *)(uintptr_t)7));
    *config=(simple_ble_cfg_t){next_advertising_cb,next_advertising_context};
    ble_cfg_p=config;
}
int main(void) {
    simple_ble_cfg_t config;
    for(unsigned random=0;random<2;++random) {
        int steps=random ? 6 : 5;
        for(int failure=0;failure<=steps;++failure) {
            reset(&config,random,failure);
            assert(protocomm_ble_set_advertising_callback(observe,(void *)(uintptr_t)8)==ESP_ERR_INVALID_STATE);
            simple_ble_on_sync();
            assert(notifications==1 && generation_seen==7);
            assert(result_seen==(failure ? 42 : 0) && started==!failure);
            assert(calls==(failure ? failure : steps));
        }
    }
    reset(&config,false,5); failure_code=BLE_HS_EDISABLED; simple_ble_on_sync();
    assert(notifications==1 && result_seen==BLE_HS_EDISABLED && !started);
    reset(&config,false,0); adv_data.uuids128=NULL; simple_ble_advertise();
    assert(!notifications && !calls);
    ble_cfg_p=NULL; protoble_internal=&config;
    assert(protocomm_ble_set_advertising_callback(NULL,NULL)==ESP_ERR_INVALID_STATE);
    protoble_internal=NULL;
    assert(!protocomm_ble_set_advertising_callback(observe,(void *)(uintptr_t)8));
    ble_cfg_p=&config; advertising_result(51); /* next值不能改写旧会话上下文。 */
    assert(notifications==1 && generation_seen==7 && result_seen==51);
    ble_cfg_p=NULL; advertising_result(0); assert(notifications==1);
    puts("real IDF advertising: address/fields/response/start failures, success-after-start, disabled host, stopped UUID and immutable session context passed");
    return 0;
}
