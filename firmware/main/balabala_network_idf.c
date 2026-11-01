#include "balabala_network_idf.h"
#include <string.h>
#ifdef BALABALA_NETWORK_IDF_MOCK
#include "mock-network-idf.h"
#else
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#include "protocomm_ble.h"
#endif

#ifdef BALABALA_AUTOTEST_PHASE2
#define BALABALA_WIFI_STORAGE WIFI_STORAGE_RAM
#else
#define BALABALA_WIFI_STORAGE WIFI_STORAGE_FLASH
#endif
static BalabalaNetwork network;
static portMUX_TYPE channel_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    wifi_config_t old;
    esp_netif_t *sta;
    esp_event_handler_instance_t handler;
    uint32_t generation;
    bool nvs, loop, wifi, radio, netif_initialized, sta_attach_attempted;
} driver;
static void lock(void *context) { (void)context; portENTER_CRITICAL(&channel_lock); }
static void unlock(void *context) { (void)context; portEXIT_CRITICAL(&channel_lock); }
static void event(void *context, esp_event_base_t base, int32_t id, void *data) {
    (void)context; (void)base; (void)data; (void)id;
    /* 默认队列仅观察；所有状态从绑定generation的同步桥接提交。 */
}
static void terminal(void *context, network_prov_cb_event_t id, void *data) {
    (void)data;
    uint32_t bits;
    switch (id) {
        case NETWORK_PROV_WIFI_CRED_RECV: bits = BALABALA_NET_EVENT_RECV; break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS: bits = BALABALA_NET_EVENT_SUCCESS; break;
        case NETWORK_PROV_WIFI_CRED_FAIL: bits = BALABALA_NET_EVENT_FAIL; break;
        case NETWORK_PROV_END: bits = BALABALA_NET_EVENT_END; break;
        default: return;
    }
    /* 绑定本次generation；不读取event_data，不等队列、不调用生命周期/Rust。
     * manager补丁保证END仅在资源释放且IDLE后同步发出。 */
    balabala_network_publish(&network, (uint32_t)(uintptr_t)context, bits);
}
static void advertising(void *context, int result) {
    balabala_network_publish(&network, (uint32_t)(uintptr_t)context,
                            result ? BALABALA_NET_EVENT_ADVERTISING_FAILED : BALABALA_NET_EVENT_ADVERTISING);
}
static int prepare(void *context, uint32_t generation) {
    (void)context;
    driver.generation = generation;
    /* 上游debug可打印凭据；本组件编译禁用日志，闭源Wi-Fi标签也关闭。 */
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("wifi_init", ESP_LOG_NONE);
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) return result; /* 不自动erase NVS。 */
    driver.nvs = true;
    if (!driver.netif_initialized) {
        result = esp_netif_init();
        if (result != ESP_OK) return result;
        driver.netif_initialized = true; /* IDF的esp_netif_deinit尚未实现。 */
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK) return result;
    driver.loop = true;
    esp_netif_config_t sta_config = ESP_NETIF_DEFAULT_WIFI_STA();
    driver.sta = esp_netif_new(&sta_config);
    if (!driver.sta) return ESP_ERR_NO_MEM;
    /* attach失败也可能已登记全局netif/取得driver，必须走对应清理。 */
    driver.sta_attach_attempted = true;
    result = esp_netif_attach_wifi_station(driver.sta);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_default_wifi_sta_handlers();
    if (result != ESP_OK) return result;
    result = esp_event_handler_instance_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                  event, NULL, &driver.handler);
    if (result != ESP_OK) return result;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&config);
    if (result != ESP_OK) return result;
    driver.wifi = true;
    result = esp_wifi_set_storage(BALABALA_WIFI_STORAGE);
    if (result != ESP_OK) return result;
    return esp_wifi_set_mode(WIFI_MODE_STA);
}
static int backup(void *context) {
    (void)context;
    return esp_wifi_get_config(WIFI_IF_STA, &driver.old);
}
static int radio_start(void *context) {
    (void)context;
    esp_err_t result = esp_wifi_start();
    driver.radio = result == ESP_OK;
    return result;
}
static int identity(void *context, char name[12], char pop[17]) {
    (void)context;
    uint8_t mac[6] = {0};
    uint32_t random = 0;
    memset(name, 0, 12);
    memset(pop, 0, 17);
    const char upper[] = "0123456789ABCDEF";
    esp_err_t result = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (result == ESP_OK) {
        /* radio_start已经成功，RF提供熵；不碰已被按键占用的ADC。 */
        /* 丢弃不足一百万的末尾区间，避免取模偏差。 */
        do {
            esp_fill_random(&random, sizeof(random));
        } while (random >= UINT32_MAX - (UINT32_MAX % 1000000u));
        memcpy(name, "PROV_", 5);
        for (unsigned i = 0; i < 3; ++i) {
            name[5 + i*2] = upper[mac[3+i] >> 4];
            name[6 + i*2] = upper[mac[3+i] & 15];
        }
        name[11] = 0;
        random %= 1000000u;
        for (unsigned i = 6; i > 0; --i) {
            pop[i - 1] = '0' + random % 10;
            random /= 10;
        }
    }
    balabala_network_clear(&random, sizeof(random));
    balabala_network_clear(mac, sizeof(mac));
    return result;
}
static int manager_init(void *context) {
    (void)context;
    network_prov_mgr_config_t config = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
        .app_event_handler = {terminal, (void *)(uintptr_t)driver.generation},
        .mode = NETWORK_PROV_MODE_FULL,
    };
    return network_prov_mgr_init(config);
}
static int manager_start(void *context, const char *name, const char *pop) {
    (void)context;
    if (!pop[0]) return ESP_ERR_INVALID_ARG;
    esp_err_t result = protocomm_ble_set_advertising_callback(advertising, (void *)(uintptr_t)driver.generation);
    if (result != ESP_OK) return result;
    return network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, pop, name, NULL);
}
static void manager_stop(void *context) { (void)context; network_prov_mgr_stop_provisioning(); }
static int manager_deinit(void *context) { (void)context; return network_prov_mgr_deinit(); }
static int manager_cleanup_error(void *context) { (void)context; return network_prov_mgr_get_cleanup_error(); }
static int restore(void *context) {
    (void)context;
    esp_err_t result = ESP_OK;
    if (driver.radio) {
        result = esp_wifi_stop();
        if (result == ESP_OK) driver.radio = false;
    }
    if (result == ESP_OK) result = esp_wifi_set_storage(BALABALA_WIFI_STORAGE);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_STA, &driver.old);
    return result;
}
static int release(void *context) {
    (void)context;
    int result;
    /* 某步失败即保留该资源及它仍依赖的所有下层所有权，不自动retry。 */
    if (driver.wifi) {
        if (driver.radio) {
            result = esp_wifi_stop();
            if (result != ESP_OK) return result;
            driver.radio = false;
        }
        result = esp_wifi_deinit();
        if (result != ESP_OK) return result;
        driver.wifi = false;
    }
    if (driver.handler) {
        result = esp_event_handler_instance_unregister(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, driver.handler);
        if (result != ESP_OK) return result;
        driver.handler = NULL;
    }
    if (driver.sta) {
        if (driver.sta_attach_attempted) {
            result = esp_wifi_clear_default_wifi_driver_and_handlers(driver.sta);
            if (result != ESP_OK) return result;
            driver.sta_attach_attempted = false;
        }
        esp_netif_destroy(driver.sta);
        driver.sta = NULL;
    }
    if (driver.loop) {
        result = esp_event_loop_delete_default();
        if (result != ESP_OK) return result;
        driver.loop = false;
    }
    if (driver.nvs) {
        result = nvs_flash_deinit();
        if (result != ESP_OK) return result;
        driver.nvs = false;
    }
    balabala_network_clear(&driver.old, sizeof(driver.old));
    return ESP_OK;
}
BalabalaNetwork *balabala_network_idf_init(void) {
    const BalabalaNetworkOps ops = {prepare, backup, radio_start, identity, manager_init,
        manager_start, manager_stop, manager_deinit, manager_cleanup_error, restore, release, lock, unlock, NULL};
    balabala_network_init(&network, &ops);
    return &network;
}
