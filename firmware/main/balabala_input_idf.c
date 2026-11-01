#include <string.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "balabala_input_idf.h"

esp_err_t balabala_input_idf_init(BalabalaInputIdf *input, const BalabalaInputConfig *config) {
    if (!input) return ESP_ERR_INVALID_ARG;
    memset(input, 0, sizeof(*input));
    if (balabala_input_init(&input->input, config) != 0) return ESP_ERR_INVALID_ARG;
    adc_unit_t units[2];
    esp_err_t error = adc_oneshot_io_to_channel(BALABALA_INPUT_ADC1_GPIO, &units[0], &input->channels[0]);
    if (error != ESP_OK) return error;
    error = adc_oneshot_io_to_channel(BALABALA_INPUT_ADC2_GPIO, &units[1], &input->channels[1]);
    if (error != ESP_OK) return error;
    if (units[0] != ADC_UNIT_1 || units[1] != units[0]) return ESP_ERR_INVALID_ARG;
    const adc_oneshot_unit_init_cfg_t unit_config = {.unit_id = units[0]};
    error = adc_oneshot_new_unit(&unit_config, &input->adc);
    if (error != ESP_OK) return error;
    const adc_oneshot_chan_cfg_t channel_config = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
    for (unsigned i = 0; i < 2 && error == ESP_OK; ++i) {
        error = adc_oneshot_config_channel(input->adc, input->channels[i], &channel_config);
    }
    if (error == ESP_OK) {
        const gpio_config_t power_config = {
            .pin_bit_mask = 1ULL << BALABALA_INPUT_POWER_GPIO,
            .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE,
        };
        error = gpio_config(&power_config);
    }
    if (error != ESP_OK) balabala_input_idf_deinit(input);
    return error;
}
esp_err_t balabala_input_idf_poll(BalabalaInputIdf *input, BalabalaInputEmit emit, void *context) {
    if (!input || !input->adc || !emit) return ESP_ERR_INVALID_ARG;
    uint64_t now = (uint64_t)(esp_timer_get_time() / 1000);
    if (input->sampled && now - input->last_poll_ms < BALABALA_INPUT_POLL_MS) return ESP_OK;
    input->sampled = true;
    input->last_poll_ms = now;
    int raw[2] = {-1, -1};
    for (unsigned i = 0; i < 2; ++i) {
        input->adc_errors[i] = adc_oneshot_read(input->adc, input->channels[i], &raw[i]);
    }
    balabala_input_sample(&input->input, now, raw[0], input->adc_errors[0] == ESP_OK,
                          raw[1], input->adc_errors[1] == ESP_OK,
                          gpio_get_level(BALABALA_INPUT_POWER_GPIO), emit, context);
    return input->adc_errors[0] != ESP_OK ? input->adc_errors[0] : input->adc_errors[1];
}
void balabala_input_idf_deinit(BalabalaInputIdf *input) {
    if (input && input->adc) {
        (void)adc_oneshot_del_unit(input->adc);
        input->adc = NULL;
    }
}
