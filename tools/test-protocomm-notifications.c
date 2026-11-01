/* 编译固定IDF补丁的真实四函数。PSA只模拟成功/错误输出，不验证密码学；
 * 真实mutex/cond固定consumer持loop锁等host且有界队列已满的停止交错。 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "session.pb-c.h"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define BLE_ATT_MTU_DFLT 23
#define PROTOCOMM_TRANSPORT_BLE_EVENT 1
#define PROTOCOMM_SECURITY_SESSION_EVENT 2
#define PROTOCOMM_TRANSPORT_BLE_CONNECTED 10
#define PROTOCOMM_TRANSPORT_BLE_DISCONNECTED 11
#define PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS 12
#define PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH 13
#define PROTOCOMM_SECURITY_SESSION_SETUP_OK 14
#define PSA_SUCCESS 0
#define PSA_KEY_ATTRIBUTES_INIT 0
#define PSA_HASH_OPERATION_INIT 0
#define PSA_ALG_CTR 0
#define PSA_KEY_USAGE_DECRYPT 1
#define PSA_KEY_USAGE_ENCRYPT 2
#define PSA_KEY_USAGE_DERIVE 1
#define PSA_KEY_USAGE_EXPORT 2
#define PSA_KEY_TYPE_AES 0
#define PSA_KEY_LIFETIME_VOLATILE 0
#define PSA_ECC_FAMILY_MONTGOMERY 0
#define PSA_KEY_TYPE_ECC_KEY_PAIR(x) 0
#define PSA_ALG_ECDH 0
#define PSA_ALG_SHA_256 0
#define PSA_CIPHER_DECRYPT_OUTPUT_SIZE(type,alg,len) (len)
#define psa_set_key_usage_flags(...) ((void)0)
#define psa_set_key_algorithm(...) ((void)0)
#define psa_set_key_type(...) ((void)0)
#define psa_set_key_lifetime(...) ((void)0)
#define psa_set_key_bits(...) ((void)0)
#define psa_reset_key_attributes(...) ((void)0)
typedef int esp_err_t,psa_status_t,psa_key_id_t,psa_cipher_operation_t,psa_key_attributes_t,psa_algorithm_t,psa_hash_operation_t;
typedef struct { const uint8_t *data; size_t len; } protocomm_security1_params_t;
static bool mismatch;
static int psa_import_key(psa_key_attributes_t *a,const void *in,size_t n,psa_key_id_t *key) { assert(a && in && n); *key=1; return 0; }
static int psa_generate_key(psa_key_attributes_t *a,psa_key_id_t *key) { assert(a); *key=1; return 0; }
static int psa_cipher_operation_init(void) { return 0; }
static int psa_cipher_encrypt_setup(psa_cipher_operation_t *o,psa_key_id_t k,psa_algorithm_t alg) { assert(o && k); (void)alg; return 0; }
static int psa_cipher_set_iv(psa_cipher_operation_t *o,const void *iv,size_t n) { assert(o && iv && n); return 0; }
static int psa_cipher_update(psa_cipher_operation_t *o,const void *in,size_t n,void *out,size_t capacity,size_t *actual) {
    assert(o && in && n<=capacity); memset(out,mismatch?1:0,n); *actual=n; return 0;
}
static int psa_cipher_abort(psa_cipher_operation_t *o) { assert(o); return 0; }
static int psa_destroy_key(psa_key_id_t k) { assert(k); return 0; }
static int psa_export_public_key(psa_key_id_t k,void *out,size_t n,size_t *actual) { assert(k); memset(out,0,n); *actual=n; return 0; }
static int psa_raw_key_agreement(int alg,psa_key_id_t k,const void *in,size_t len,void *out,size_t n,size_t *actual) {
    (void)alg; assert(k && in && len); memset(out,0,n); *actual=n; return 0;
}
static int psa_hash_setup(psa_hash_operation_t *o,int alg) { (void)alg; assert(o); return 0; }
static int psa_hash_update(psa_hash_operation_t *o,const void *in,size_t n) { assert(o && in && n); return 0; }
static int psa_hash_abort(psa_hash_operation_t *o) { assert(o); return 0; }
static int psa_hash_finish(psa_hash_operation_t *o,void *out,size_t n,size_t *actual) { assert(o); memset(out,0,n); *actual=n; return 0; }
static int psa_generate_random(void *out,size_t n) { memset(out,0,n); return 0; }
static int mbedtls_ct_memcmp(const void *a,const void *b,size_t n) { return memcmp(a,b,n); }
static void hexdump(const char *s,uint8_t *p,int n) { assert(s && p && n>0); }
static esp_err_t sec1_new_session(void *s,uint32_t id) { (void)id; assert(s); return 0; }
void sec1_payload__init(Sec1Payload *p) { memset(p,0,sizeof(*p)); }
void session_resp0__init(SessionResp0 *p) { memset(p,0,sizeof(*p)); }
void session_resp1__init(SessionResp1 *p) { memset(p,0,sizeof(*p)); }
struct ble_gap_event {
    struct { int conn_handle,status; } connect;
    struct { struct { int conn_handle; } conn; int reason; } disconnect;
};
typedef struct { int evt_type,conn_handle,conn_status,disconnect_reason; } protocomm_ble_event_t;
typedef struct { esp_err_t (*new_transport_session)(void *,int); esp_err_t (*close_transport_session)(void *,int); } sec_t;
typedef struct { sec_t *sec; void *sec_inst; } protocomm_t;
static struct { protocomm_t *pc_ble; int gatt_mtu; } internal;
static void *protoble_marker;
#define protoble_internal (&internal)
static bool protocomm_ble_transport_active(void) { return protoble_marker!=NULL; }
static unsigned cache_clears,new_sessions,closed_sessions;
static void simple_ble_gatts_clear_cached_values(void) { ++cache_clears; }
static pthread_mutex_t sync_mutex=PTHREAD_MUTEX_INITIALIZER,loop_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static bool loop_stopping,host_done;
static unsigned posts,dropped,event_counts[15],queue_count;
static void await(bool *flag) {
    struct timespec deadline; assert(!clock_gettime(CLOCK_REALTIME,&deadline)); deadline.tv_sec+=3;
    while(!*flag) assert(pthread_cond_timedwait(&changed,&sync_mutex,&deadline)==0);
}
static int esp_event_post(int base,int id,const void *data,size_t size,int wait) {
    assert(base==1 || base==2); assert(id>=10 && id<=14); ++posts; ++event_counts[id];
    if(base==1) assert(data && size==sizeof(protocomm_ble_event_t));
    else assert(!data && !size);
    assert(!pthread_mutex_lock(&sync_mutex)); assert(queue_count==2 && loop_stopping);
    /* 未补丁的portMAX_DELAY会等持loop锁的consumer释放队列，3秒内失败。 */
    if(wait!=0) await(&host_done);
    assert(wait==0); ++dropped;
    assert(!pthread_mutex_unlock(&sync_mutex)); return ESP_FAIL;
}
#include "../.build/protocomm-notifications-under-test.inc"
static int new_session(void *ctx,int handle) { assert(ctx && handle==7); ++new_sessions; return ESP_OK; }
static int close_session(void *ctx,int handle) { assert(ctx && handle==7); ++closed_sessions; return ESP_OK; }
static void *host_thread(void *unused) {
    (void)unused;
    assert(!pthread_mutex_lock(&sync_mutex)); await(&loop_stopping); assert(!pthread_mutex_unlock(&sync_mutex));
    struct ble_gap_event event={.connect={7,0},.disconnect={{7},0}};
    sec_t sec={new_session,close_session}; protocomm_t pc={&sec,&internal}; internal.pc_ble=&pc; protoble_marker=&pc;
    transport_simple_ble_connect(&event,NULL);
    session_t s={0}; Sec1Payload input={0}; SessionData request={.sec1=&input},response={0};
    SessionCmd0 cmd0={0}; uint8_t client[32]={0};
    assert(handle_session_command0(&s,7,&request,&response,NULL)==ESP_ERR_INVALID_ARG);
    input.payload_case=SEC1_PAYLOAD__PAYLOAD_SC0; input.sc0=&cmd0; cmd0.client_pubkey.data=client; cmd0.client_pubkey.len=1;
    assert(handle_session_command0(&s,7,&request,&response,NULL)==ESP_ERR_INVALID_ARG);
    cmd0.client_pubkey.len=32; protocomm_security1_params_t pop={(uint8_t *)"012345",6};
    assert(handle_session_command0(&s,7,&request,&response,&pop)==ESP_OK && s.state==SESSION_STATE_CMD1);
    free(response.sec1->sr0); free(response.sec1); response.sec1=NULL;
    input.payload_case=SEC1_PAYLOAD__PAYLOAD_SC1; input.sc1=NULL;
    assert(handle_session_command1(&s,7,&request,&response)==ESP_ERR_INVALID_ARG);
    SessionCmd1 cmd1={.client_verify_data={32,client}}; input.sc1=&cmd1; mismatch=true;
    assert(handle_session_command1(&s,7,&request,&response)==ESP_FAIL && s.state==SESSION_STATE_CMD1);
    mismatch=false; assert(handle_session_command1(&s,7,&request,&response)==ESP_OK && s.state==SESSION_STATE_DONE);
    assert(response.sec1->sr1->status==STATUS__Success && response.sec1->sr1->device_verify_data.len==32);
    free(response.sec1->sr1->device_verify_data.data); free(response.sec1->sr1); free(response.sec1);
    transport_simple_ble_disconnect(&event,NULL); assert(internal.gatt_mtu==23);
    internal.pc_ble=NULL; protoble_marker=NULL;
    assert(!pthread_mutex_lock(&sync_mutex)); host_done=true; assert(!pthread_cond_broadcast(&changed));
    assert(!pthread_mutex_unlock(&sync_mutex)); return NULL;
}
static void *consumer_thread(void *unused) {
    (void)unused; assert(!pthread_mutex_lock(&loop_mutex));
    assert(!pthread_mutex_lock(&sync_mutex)); queue_count=2; loop_stopping=true;
    assert(!pthread_cond_broadcast(&changed)); await(&host_done);
    assert(!pthread_mutex_unlock(&sync_mutex)); assert(!pthread_mutex_unlock(&loop_mutex)); return NULL;
}
int main(void) {
    pthread_t consumer,host; assert(!pthread_create(&consumer,NULL,consumer_thread,NULL));
    assert(!pthread_create(&host,NULL,host_thread,NULL)); assert(!pthread_join(consumer,NULL)); assert(!pthread_join(host,NULL));
    assert(posts==7 && dropped==7 && event_counts[12]==3 && event_counts[13]==1 && event_counts[14]==1);
    assert(new_sessions==1 && closed_sessions==1 && cache_clears==1);
    puts("protocomm real patched functions: 1 passed; 0 failed (7 full-queue drops, Security1 control flow, mutex/condition 3s bound, ASan/UBSan; PSA mocked)");
}
