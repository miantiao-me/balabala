#ifndef MOCK_NETWORK_IDF_H
#define MOCK_NETWORK_IDF_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef int esp_err_t;
enum { ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=101, ESP_ERR_INVALID_ARG=102,
       ESP_EVENT_ANY_ID=-1, ESP_LOG_NONE=0, WIFI_STORAGE_RAM=0, WIFI_STORAGE_FLASH=1, WIFI_MODE_STA=1,
       WIFI_IF_STA=0, ESP_MAC_WIFI_STA=0, NETWORK_PROV_SECURITY_1=1, NETWORK_PROV_MODE_FULL=0 };
enum { NETWORK_PROV_START=0, NETWORK_PROV_WIFI_CRED_RECV=1, NETWORK_PROV_WIFI_CRED_SUCCESS,
       NETWORK_PROV_WIFI_CRED_FAIL, NETWORK_PROV_END };
#define NETWORK_PROV_EVENT "network_prov"
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef void (*mock_event_handler)(void *,esp_event_base_t,int32_t,void *);
typedef struct { int value; } esp_netif_t;
typedef struct { uint8_t ssid[32], password[64]; } wifi_config_t;
typedef struct { int value; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) do { ++*(lock); } while(0)
#define portEXIT_CRITICAL(lock) do { --*(lock); } while(0)
typedef int network_prov_cb_event_t;
typedef void (*mock_app_callback)(void *,network_prov_cb_event_t,void *);
typedef struct { mock_app_callback event_cb; void *user_data; } mock_handler;
#define NETWORK_PROV_EVENT_HANDLER_NONE ((mock_handler){NULL,NULL})
#define network_prov_scheme_ble 7
typedef struct { int scheme; mock_handler scheme_event_handler,app_event_handler; int mode; } network_prov_mgr_config_t;
void esp_log_level_set(const char *,int);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_deinit(void);
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_loop_delete_default(void);
typedef struct { int value; } esp_netif_config_t;
#define ESP_NETIF_DEFAULT_WIFI_STA() ((esp_netif_config_t){0})
esp_netif_t *esp_netif_new(const esp_netif_config_t *);
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *);
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void);
void esp_netif_destroy(esp_netif_t *);
esp_err_t esp_event_handler_instance_register(esp_event_base_t,int32_t,mock_event_handler,void *,esp_event_handler_instance_t *);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t,int32_t,esp_event_handler_instance_t);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_get_config(int,wifi_config_t *);
esp_err_t esp_wifi_set_config(int,const wifi_config_t *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(esp_netif_t *);
esp_err_t esp_read_mac(uint8_t *,int);
void esp_fill_random(void *,size_t);
esp_err_t network_prov_mgr_init(network_prov_mgr_config_t);
esp_err_t network_prov_mgr_start_provisioning(int,const void *,const char *,const char *);
typedef void (*protocomm_ble_advertising_cb_t)(void *, int);
esp_err_t protocomm_ble_set_advertising_callback(protocomm_ble_advertising_cb_t, void *);
void network_prov_mgr_stop_provisioning(void);
esp_err_t network_prov_mgr_deinit(void);
esp_err_t network_prov_mgr_get_cleanup_error(void);
#endif
