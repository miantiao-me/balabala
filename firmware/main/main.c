#include <stdlib.h>
#include "sdkconfig.h"
#include "esp_rom_sys.h"
#ifdef CONFIG_BALABALA_AUTOTEST
#include "balabala_autotest.h"
#include "balabala_alloc_diag.h"
#include "esp_random.h"
#include "esp_attr.h"
#endif
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "balabala_probe.h"
#include "balabala_display.h"
#include "balabala_runtime.h"
#include "balabala_network_idf.h"
#include "balabala_storage.h"
#include "balabala_input_idf.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "balabala";
static BalabalaApp *app;
/* 符号保留在 ELF/map 中，便于核实真实静态空间。 */
static uint8_t framebuffer[BALABALA_FRAMEBUFFER_BYTES];
static uint16_t stripe[BALABALA_STRIPE_PIXELS];
_Static_assert(sizeof(framebuffer) == 48000, "1bit framebuffer size");
_Static_assert(sizeof(stripe) == 7680, "8-row RGB565 stripe size");

/* 阶段编号与Rust私有checkpoint一致；只记录固定标签，不含内容或凭据。 */
static uint32_t memory_stage;
static const char *memory_stage_name(uint32_t stage) {
    switch (stage) {
        case 1: return "fonts-end"; case 2: return "tree-mount-end";
        case 3: return "frame-layout-end"; case 4: return "draw-end";
        case 5: return "raster-end"; case 6: return "render-begin";
        case 10: return "main-begin"; case 11: return "app-created";
        case 12: return "sd-attempt-end"; case 13: return "import-end";
        case 14: return "restore-end"; case 15: return "input-ready";
        case 16: return "runtime-ready"; case 17: return "display-end";
        case 18: return "save-end"; default: return "unknown";
    }
}
static void memory_snapshot(void) {
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    /* largest-block和watermark会扫描；仅显式调试阶段或OOM调用，不在正常alloc调用。 */
    size_t free = heap_caps_get_free_size(caps);
    size_t minimum = heap_caps_get_minimum_free_size(caps);
    size_t largest = heap_caps_get_largest_free_block(caps);
    unsigned watermark = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    esp_rom_printf("memory stage=%s free=%u minimum=%u largest=%u stack_free=%u\n",
                   memory_stage_name(memory_stage), (unsigned)free, (unsigned)minimum,
                   (unsigned)largest, watermark);
}
void balabala_memory_checkpoint(uint32_t stage) {
    memory_stage = stage;
#ifdef CONFIG_BALABALA_MEMORY_DIAGNOSTICS
    memory_snapshot();
#endif
}

_Noreturn void balabala_fatal(const uint8_t *message, size_t length) {
    ESP_LOGE(TAG, "Rust fatal: %.*s", (int)length, (const char *)message);
    abort();
}

void *balabala_alloc(size_t size, size_t align) {
    /* GlobalAlloc 要求的对齐可能大于 malloc 的基本对齐。IDF 支持 aligned_alloc。 */
    if (align < sizeof(void *)) align = sizeof(void *);
    void *result = heap_caps_aligned_alloc(align, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (result == NULL) {
        esp_rom_printf("Rust allocation failed: size=%u align=%u\n", (unsigned)size, (unsigned)align);
        memory_snapshot();
        abort();
    }
    return result;
}

void balabala_free(void *ptr) {
    heap_caps_free(ptr);
}

/* 原版X4接线事实：community-sdk 7d86603与X4Term bd2f028固定源码。 */
enum { X4_SCLK=8, X4_MOSI=10, X4_MISO=7, X4_EPD_CS=21, X4_EPD_DC=4,
       X4_EPD_RST=5, X4_EPD_BUSY=6, X4_SD_CS=12, X4_USB_POWER=20 };
static const char *SD_ROOT = "/sdcard";
static spi_device_handle_t epd_spi;
static BalabalaDisplay display;
static BalabalaStorage storage;
static BalabalaInputIdf input;
static BalabalaRuntime runtime;
static bool input_ready, sd_ready;
static bool usb_detect_ready;
static bool external_power(void *context) {
    (void)context;
    return !usb_detect_ready || gpio_get_level(X4_USB_POWER) != 0;
}
static bool input_idle(void *context) {
    (void)context;
#ifdef CONFIG_BALABALA_AUTOTEST
    bool released = balabala_autotest_input_idle(&input.input);
#else
    bool released = balabala_input_idle(&input.input);
#endif
    return input_ready && input.sampled && input.adc_errors[0] == ESP_OK &&
        input.adc_errors[1] == ESP_OK && released &&
        gpio_get_level(BALABALA_INPUT_POWER_GPIO) != 0;
}
#ifdef CONFIG_BALABALA_AUTOTEST
static BalabalaAutotest autotest;
static RTC_NOINIT_ATTR BalabalaAutotestRtc autotest_rtc;
static char autotest_root[BALABALA_AUTOTEST_ROOT_BYTES];
static bool autotest_ready;
#ifdef BALABALA_AUTOTEST_PHASE2
static unsigned alloc_emitted;
static bool alloc_overflow_emitted;
static void autotest_alloc_drain(void) {
    BalabalaAllocObservation o;
    balabala_alloc_diag_observe(&o);
    /* 先复制出临界区；所有heap查询和串口输出都在普通主任务中。 */
    while (alloc_emitted < o.count) {
        const BalabalaAllocFailure *f = &o.failures[alloc_emitted];
        esp_rom_printf("autotest alloc_fail=1 index=%u size=%u caps=%u diag_phase=%u generation=%u same_instant=0\n",
                       alloc_emitted, f->size, f->caps, f->phase, f->generation);
        unsigned free_bytes = heap_caps_get_free_size(f->caps);
        unsigned largest = heap_caps_get_largest_free_block(f->caps);
        esp_rom_printf("autotest alloc_post=1 index=%u caps=%u free=%u largest=%u same_instant=0\n",
                       alloc_emitted, f->caps, free_bytes, largest);
        ++alloc_emitted;
    }
    if (o.overflow && !alloc_overflow_emitted) {
        esp_rom_printf("autotest alloc_overflow=1 capacity=%u\n", BALABALA_ALLOC_DIAG_CAPACITY);
        alloc_overflow_emitted = true;
    }
}
static void autotest_alloc_sample(uint32_t phase, uint32_t generation, int result) {
    autotest_alloc_drain();
    const uint32_t caps[] = {MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
                             MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA, MALLOC_CAP_RETENTION};
    for (unsigned i = 0; i < sizeof(caps)/sizeof(caps[0]); ++i) {
        unsigned free_bytes = heap_caps_get_free_size(caps[i]);
        unsigned largest = heap_caps_get_largest_free_block(caps[i]);
        esp_rom_printf("autotest alloc_sample=1 diag_phase=%u generation=%u result=%d caps=%u free=%u largest=%u same_instant=0\n",
                       phase, generation, result, caps[i], free_bytes, largest);
    }
}
#endif
static uint32_t autotest_random(void *context) { (void)context; return esp_random(); }
static void autotest_log(void *context, unsigned step, unsigned event, uint64_t a, uint64_t b) {
    (void)context;
    /* ROM printf不支持long long；按高低32位保留完整数字证据。 */
    esp_rom_printf("autotest phase=%u step=%u event=%u a_hi=%u a_lo=%u b_hi=%u b_lo=%u elapsed=%u\n",
                   BALABALA_AUTOTEST_PHASE, step, event, (unsigned)(a >> 32), (unsigned)a,
                   (unsigned)(b >> 32), (unsigned)b,
                   (unsigned)((uint64_t)(esp_timer_get_time()/1000)-autotest.since));
}
static void autotest_memory(void *context, unsigned cycle, uint32_t *free_bytes, uint32_t *largest) {
    (void)context;
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    *free_bytes = heap_caps_get_free_size(caps); *largest = heap_caps_get_largest_free_block(caps);
#ifdef BALABALA_AUTOTEST_PHASE2
    unsigned point=cycle%4; cycle/=4;
#else
    unsigned point=0;
#endif
    esp_rom_printf("autotest phase=%u cycle=%u point=%u free=%u minimum=%u largest=%u stack_free=%u\n",
                   BALABALA_AUTOTEST_PHASE, cycle, point, (unsigned)*free_bytes, (unsigned)heap_caps_get_minimum_free_size(caps),
                   (unsigned)*largest, (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
static void autotest_restart(void *context) { (void)context; esp_restart(); }
#endif

static uint64_t now_ms(void *context) {
    (void)context;
    return (uint64_t)(esp_timer_get_time() / 1000);
}
static void emit(void *context, BalabalaKey key, bool pressed) {
    BalabalaRuntime *r = context;
    balabala_runtime_activity(r, key, pressed);
    /* 准备/关机存档后停止学习变更；BUSY/休眠等待仍接收返回、释放和电源。 */
    if ((!r->shutdown && !(balabala_app_status(r->app) & BALABALA_DOWNLOAD_ACTIVE)) ||
        !pressed || key == BALABALA_HOME || key == BALABALA_SHUTDOWN ||
        (!r->shutdown && key == BALABALA_FRONT3))
        (void)balabala_app_key(r->app, key, pressed);
}
#ifdef CONFIG_BALABALA_AUTOTEST
static void physical_emit(void *context, BalabalaKey key, bool pressed) {
    if (autotest_ready) balabala_autotest_user_input(&autotest);
    emit(context, key, pressed);
}
#else
#define physical_emit emit
#endif
static void poll(void *context, BalabalaApp *target) {
    (void)context; (void)target;
    if (input_ready) {
        (void)balabala_input_idf_poll(&input, physical_emit, &runtime);
    } else {
        /* ADC初始化失败也保留独立GPIO3长按关机能力。 */
        balabala_input_sample(&input.input, now_ms(NULL), 0, false, 0, false,
                              gpio_get_level(BALABALA_INPUT_POWER_GPIO), physical_emit, &runtime);
    }
}
static void service(void *context, uint32_t delay_ms) {
    (void)context;
    uint64_t start = now_ms(NULL);
    do {
        balabala_runtime_service(&runtime);
        if (delay_ms == 0) return;
        vTaskDelay(1); /* 配置200Hz，每tick 5ms；reset低脉冲至少2ms。 */
    } while (now_ms(NULL) - start < delay_ms);
}
static int write_epd(void *context, bool data, const uint8_t *bytes, size_t length) {
    (void)context;
    if (!epd_spi) return -1;
    if (gpio_set_level(X4_EPD_DC, data ? 1 : 0) != ESP_OK) return -1;
    spi_transaction_t transaction = {.length = length * 8, .tx_buffer = bytes};
    return spi_device_polling_transmit(epd_spi, &transaction) == ESP_OK ? 0 : -1;
}
static int reset_epd(void *context, bool high) {
    (void)context;
    return gpio_set_level(X4_EPD_RST, high ? 1 : 0) == ESP_OK ? 0 : -1;
}
static int busy_epd(void *context) { (void)context; return gpio_get_level(X4_EPD_BUSY); }
static int refresh(void *context, const uint8_t *frame, bool partial) {
    (void)context;
#ifdef BALABALA_AUTOTEST_PHASE2
    if (autotest_ready) balabala_autotest_before_display(&autotest, &runtime);
#endif
    display.partial_limit = (uint8_t)balabala_app_clear_interval(app);
    ESP_LOGI(TAG, "EPD refresh: mode=%s partial_count=%u limit=%u attempt=%u",
             partial && display.baseline_valid && display.partials < display.partial_limit ? "partial" : "full",
             display.partials, display.partial_limit, runtime.attempts);
    int result = balabala_display_refresh(&display, frame, partial);
    balabala_memory_checkpoint(17);
    if (result) ESP_LOGE(TAG, "EPD refresh failed: %d, attempt=%u", result, runtime.attempts);
    return result;
}
static int save(void *context, BalabalaApp *target) {
    (void)context;
    if (!sd_ready) { ESP_LOGW(TAG, "Storage unavailable; progress not persisted"); balabala_memory_checkpoint(18); return 2; }
#ifdef CONFIG_BALABALA_AUTOTEST
    BalabalaAutotestSnapshot before, after;
    balabala_app_autotest_snapshot(target, &before);
#endif
    int result = balabala_storage_save(&storage, target);
#ifdef CONFIG_BALABALA_AUTOTEST
    balabala_app_autotest_snapshot(target, &after);
    if (autotest_ready && before.dirty)
        balabala_autotest_saved(&autotest, &before, &after, result, storage.report.status);
#endif
    balabala_memory_checkpoint(18);
    if (result) ESP_LOGE(TAG, "Save failed: operation=%s status=%d errno=%d app=%ld",
                         storage.report.operation, storage.report.status, storage.report.last_errno,
                         (long)storage.report.app_result);
    return result == 0 ? 0 : 1;
}
static int screen_sleep(void *context) {
    (void)context;
    int result = balabala_display_sleep(&display);
    if (result) ESP_LOGE(TAG, "EPD sleep/power-down failed: %d", result);
    return result;
}
static bool power_released(void *context) {
    (void)context;
    return gpio_get_level(BALABALA_INPUT_POWER_GPIO) != 0;
}
static int sleep_mcu(void *context) {
    (void)context;
    esp_err_t result = esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(
        1ULL << BALABALA_INPUT_POWER_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "GPIO3 wake configuration failed: %s", esp_err_to_name(result));
        return -1;
    }
    ESP_LOGI(TAG, "Deep sleep; save result=%d (0=acknowledged, 1=failed, 2=unavailable)", runtime.save_result);
    esp_deep_sleep_start();
    return 0;
}
static int download_check(void *context) {
    (void)context;
#if CONFIG_IDF_TARGET_ESP32C3
    if (esp_efuse_read_field_bit(ESP_EFUSE_DIS_FORCE_DOWNLOAD)) return BALABALA_DOWNLOAD_FORCE_DISABLED;
    if (esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE)) return BALABALA_DOWNLOAD_DISABLED;
    if (esp_efuse_read_field_bit(ESP_EFUSE_DIS_USB_SERIAL_JTAG)) return BALABALA_DOWNLOAD_USB_DISABLED;
    if (esp_efuse_read_field_bit(ESP_EFUSE_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE)) return BALABALA_DOWNLOAD_USB_BOOT_DISABLED;
    if (esp_efuse_read_field_bit(ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD)) return BALABALA_DOWNLOAD_SECURITY;
    return 0;
#else
    return BALABALA_DOWNLOAD_UNSUPPORTED;
#endif
}
static void enter_download(void *context) {
    (void)context;
#if CONFIG_IDF_TARGET_ESP32C3
    /* C3 TRM v1.4 §7.2: FORCE_DOWNLOAD_BOOT + CPU reset进入Joint Download。 */
    REG_SET_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
#endif
}
static esp_err_t start_bus(void) {
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << X4_EPD_CS) | (1ULL << X4_SD_CS) |
                        (1ULL << X4_EPD_DC) | (1ULL << X4_EPD_RST),
        .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&outputs);
    if (result != ESP_OK) return result;
    gpio_set_level(X4_EPD_CS, 1); gpio_set_level(X4_SD_CS, 1);
    gpio_set_level(X4_EPD_DC, 1); gpio_set_level(X4_EPD_RST, 1);
    const gpio_config_t busy = {.pin_bit_mask = 1ULL << X4_EPD_BUSY, .mode = GPIO_MODE_INPUT};
    result = gpio_config(&busy);
    if (result != ESP_OK) return result;
    const spi_bus_config_t bus = {.mosi_io_num=X4_MOSI, .miso_io_num=X4_MISO,
        .sclk_io_num=X4_SCLK, .quadwp_io_num=-1, .quadhd_io_num=-1, .max_transfer_sz=4096};
    return spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
}
static esp_err_t mount_sd(void) {
    sdmmc_host_t host = SDSPI_HOST_DEFAULT(); /* IDF安全默认20MHz，上游40MHz未采用。 */
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST; slot.gpio_cs = X4_SD_CS;
    const esp_vfs_fat_sdmmc_mount_config_t config = {
        .format_if_mount_failed=false, .max_files=4, .allocation_unit_size=16*1024,
    };
    sdmmc_card_t *card = NULL;
    esp_err_t result = esp_vfs_fat_sdspi_mount(SD_ROOT, &host, &slot, &config, &card);
    sd_ready = result == ESP_OK;
    if (!sd_ready) {
        ESP_LOGW(TAG, "SD unavailable (%s); built-in learning continues", esp_err_to_name(result));
        /* IDF失败清理会把CS改成无上下拉输入；共享总线继续使用前恢复明确高电平。 */
        const gpio_config_t cs = {.pin_bit_mask=1ULL << X4_SD_CS, .mode=GPIO_MODE_OUTPUT};
        esp_err_t isolated = gpio_config(&cs);
        if (isolated == ESP_OK) isolated = gpio_set_level(X4_SD_CS, 1);
        if (isolated != ESP_OK) {
            ESP_LOGE(TAG, "SD CS isolation failed: %s; shared SPI disabled", esp_err_to_name(isolated));
            esp_err_t released = spi_bus_free(SPI2_HOST);
            if (released != ESP_OK) ESP_LOGW(TAG, "SPI2 cleanup failed: %s", esp_err_to_name(released));
            // 不允许继续驱动共享总线；EPD设备尚未添加。
            return isolated;
        }
    }
    else ESP_LOGI(TAG, "SD mounted; automatic formatting disabled");
    return ESP_OK;
}
void app_main(void) {
#ifdef BALABALA_AUTOTEST_PHASE2
    BalabalaAllocRtc retained;
    esp_reset_reason_t alloc_reset_reason = esp_reset_reason();
    if (balabala_alloc_diag_rtc_take(alloc_reset_reason, &retained)) {
        esp_rom_printf("autotest alloc_rtc=1 magic=%u size=%u caps=%u diag_phase=%u generation=%u checksum=%u reset_reason=%u same_instant=0\n",
                       retained.magic, retained.size, retained.caps, retained.phase,
                       retained.generation, retained.checksum, (unsigned)alloc_reset_reason);
    }
    balabala_alloc_diag_init(autotest_alloc_sample);
    int alloc_hook_result = heap_caps_register_failed_alloc_callback(balabala_alloc_diag_failed);
    balabala_alloc_diag_registered(alloc_hook_result);
    esp_rom_printf("autotest alloc_hook=1 result=%d capacity=%u same_instant=0\n",
                   alloc_hook_result, BALABALA_ALLOC_DIAG_CAPACITY);
#endif
    balabala_memory_checkpoint(10);
    /* ADC按键仍每5ms采样；RTOS/驱动锁在工作时保留CPU/APB性能。 */
    const esp_pm_config_t power_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 40,
        .light_sleep_enable = false,
    };
    esp_err_t power_result = esp_pm_configure(&power_config);
    if (power_result != ESP_OK) ESP_LOGE(TAG, "Power management unavailable: %s", esp_err_to_name(power_result));
    else ESP_LOGI(TAG, "Power management: idle=40MHz active=%dMHz; ADC polling=5ms",
                  power_config.max_freq_mhz);
    app = balabala_app_create();
    balabala_memory_checkpoint(11);
    esp_err_t bus_result = start_bus();
    if (bus_result == ESP_OK) bus_result = mount_sd();
    else ESP_LOGE(TAG, "SPI2 unavailable: %s", esp_err_to_name(bus_result));
    balabala_memory_checkpoint(12);
#ifdef CONFIG_BALABALA_AUTOTEST
    /* noinit跨软件重启保留；新运行/唯一恢复/异常拒绝与host共用同一策略。 */
    int resumed = balabala_autotest_start(autotest_root, sd_ready ? SD_ROOT : NULL,
                                          &autotest_rtc, esp_reset_reason(), autotest_random, NULL);
    if (resumed < 0 || balabala_storage_init(&storage, autotest_root, NULL)) {
        autotest_log(NULL, 0, 110, 0, 0);
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    esp_rom_printf("autotest phase=%u root=%s\n", BALABALA_AUTOTEST_PHASE, autotest_root);
    if (resumed) {
        int result = balabala_storage_restore(&storage, app);
        BalabalaAutotestSnapshot restored;
        balabala_app_autotest_snapshot(app, &restored);
        if (result || storage.report.status != BALABALA_STORAGE_RESTORED ||
            restored.digest != autotest_rtc.digest || restored.generation != autotest_rtc.generation ||
            restored.slot != autotest_rtc.slot) {
            autotest_log(NULL, 40, 111, result, restored.generation);
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }
        autotest_log(NULL, 40, 7, restored.generation, restored.slot);
    }
#else
    balabala_storage_init(&storage, SD_ROOT, NULL);
    if (sd_ready) {
        int imported = balabala_storage_import_decks(&storage, app);
        balabala_storage_show_import_error(&storage, app);
        balabala_memory_checkpoint(13);
        int restored = balabala_storage_restore(&storage, app);
        balabala_memory_checkpoint(14);
        ESP_LOGI(TAG, "Startup storage: import=%d restore=%d status=%d", imported, restored, storage.report.status);
    } else {
        (void)balabala_app_save_protect(app);
    }
#endif
    input_ready = balabala_input_idf_init(&input, NULL) == ESP_OK;
    if (!input_ready) {
        ESP_LOGE(TAG, "ADC input initialization failed; power input remains enabled");
        balabala_input_init(&input.input, NULL);
        const gpio_config_t power = {.pin_bit_mask=1ULL << BALABALA_INPUT_POWER_GPIO,
            .mode=GPIO_MODE_INPUT, .pull_up_en=GPIO_PULLUP_ENABLE};
        gpio_config(&power);
    }
    const gpio_config_t usb_power = {.pin_bit_mask=1ULL << X4_USB_POWER, .mode=GPIO_MODE_INPUT};
    esp_err_t usb_result = gpio_config(&usb_power);
    usb_detect_ready = usb_result == ESP_OK;
    if (!usb_detect_ready) ESP_LOGE(TAG, "USB power detection unavailable: %s; automatic sleep disabled", esp_err_to_name(usb_result));
    else ESP_LOGI(TAG, "USB power: %s; automatic sleep disabled while connected", external_power(NULL) ? "connected" : "disconnected");
    balabala_memory_checkpoint(15);
    if (bus_result == ESP_OK) {
        const spi_device_interface_config_t config = {
            .clock_speed_hz=10*1000*1000, .mode=0, .spics_io_num=X4_EPD_CS, .queue_size=1,
        };
        esp_err_t result = spi_bus_add_device(SPI2_HOST, &config, &epd_spi);
        if (result != ESP_OK) ESP_LOGE(TAG, "EPD SPI unavailable: %s", esp_err_to_name(result));
    }
    display = (BalabalaDisplay){.write=write_epd, .reset=reset_epd, .busy=busy_epd,
        .now_ms=now_ms, .service=service};
    const BalabalaRuntimeOps ops = {.now_ms=now_ms, .poll=poll, .display=refresh, .save=save,
        .display_sleep=screen_sleep, .power_released=power_released, .sleep=sleep_mcu,
        .download_check=download_check, .enter_download=enter_download,
        .network=balabala_network_idf_init(), .external_power=external_power,
#ifndef CONFIG_BALABALA_AUTOTEST
        .input_idle=input_idle,
#endif
    };
    balabala_runtime_init(&runtime, app, framebuffer, stripe, &ops, sd_ready);
    balabala_memory_checkpoint(16);
#ifdef CONFIG_BALABALA_AUTOTEST
    const BalabalaAutotestOps autotest_ops = {.emit=emit, .log=autotest_log,
        .memory=autotest_memory, .restart=autotest_restart, .context=&runtime};
    balabala_autotest_init(&autotest, &autotest_rtc, &autotest_ops, now_ms(NULL));
    autotest_ready = true;
#endif
    bool reported_fault = false;
    for (;;) {
#ifdef BALABALA_AUTOTEST_PHASE2
        autotest_alloc_drain();
#endif
#ifdef CONFIG_BALABALA_AUTOTEST
        balabala_autotest_tick(&autotest, &runtime, &storage, now_ms(NULL), input_idle(NULL));
#endif
        balabala_runtime_step(&runtime);
#ifdef BALABALA_AUTOTEST_PHASE2
        autotest_alloc_drain();
#endif
        if (runtime.display_fault && !reported_fault) {
            ESP_LOGE(TAG, "Display fault after %u attempts; frame/revision retained, automatic refresh stopped", runtime.attempts);
            reported_fault = true;
        }
        vTaskDelay(1);
    }
}
