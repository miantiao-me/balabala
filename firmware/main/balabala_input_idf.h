#ifndef BALABALA_INPUT_IDF_H
#define BALABALA_INPUT_IDF_H
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "balabala_input.h"
#define BALABALA_INPUT_POLL_MS 5

typedef struct {
    adc_oneshot_unit_handle_t adc;
    adc_channel_t channels[2];
    BalabalaInput input;
    uint64_t last_poll_ms;
    bool sampled;
    esp_err_t adc_errors[2];
} BalabalaInputIdf;
/* 初始化/轮询由app_main平台任务串行调用。
 * GPIO1/2均在C3 ADC_UNIT_1。poll没有delay/等待循环，最多每5ms读各路一次；
 * adc_oneshot_read同步完成单次转换，超时作为该路无效样本处理。不要从ISR调用。 */
esp_err_t balabala_input_idf_init(BalabalaInputIdf *input, const BalabalaInputConfig *config);
esp_err_t balabala_input_idf_poll(BalabalaInputIdf *input, BalabalaInputEmit emit, void *context);
void balabala_input_idf_deinit(BalabalaInputIdf *input);
#endif
