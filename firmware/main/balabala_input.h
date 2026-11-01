#ifndef BALABALA_INPUT_H
#define BALABALA_INPUT_H
#include <stdbool.h>
#include <stdint.h>
#include "balabala_probe.h"

#define BALABALA_INPUT_DEBOUNCE_MS 30
#define BALABALA_INPUT_LONG_PRESS_MS 1500
#define BALABALA_INPUT_PARENT_PRESS_MS 2000
#define BALABALA_INPUT_ADC1_GPIO 1
#define BALABALA_INPUT_ADC2_GPIO 2
#define BALABALA_INPUT_POWER_GPIO 3

typedef struct {
    BalabalaKey adc1_keys[4];
    BalabalaKey adc2_keys[2];
} BalabalaInputConfig;
/* 区间顺序的临时映射，不代表物理左右已确认。上机核验后只改此配置。 */
extern const BalabalaInputConfig balabala_input_default_config;
typedef void (*BalabalaInputEmit)(void *context, BalabalaKey key, bool pressed);
typedef struct {
    int stable, candidate;
    uint64_t candidate_since;
} BalabalaInputDebounce;
typedef struct {
    BalabalaInputConfig config;
    BalabalaInputDebounce adc[2], power;
    uint64_t power_down_since, parent_down_since;
    bool shutdown_sent, parent_down, parent_sent;
} BalabalaInput;
/* 包含去抖候选，避免刚开始按键时启动自动休眠。采样错误由平台另外检查。 */
bool balabala_input_idle(const BalabalaInput *input);
/* 每路ADC的有效raw范围0..4095；返回区间索引、-1无键、-2读数无效。 */
int balabala_input_decode(unsigned bank, int raw);
int balabala_input_init(BalabalaInput *input, const BalabalaInputConfig *config);
/* 同一任务串行调用，now_ms单调递增。错误通道冻结已确认状态并取消候选去抖。
 * emit同步调用；普通键总是成对上报。Home/Shutdown各发一个按下/松开脉冲。
 * 电源长按计时从去抖确认按下开始；松开候选期间不提前发长按。 */
void balabala_input_sample(BalabalaInput *input, uint64_t now_ms,
                          int adc1_raw, bool adc1_valid, int adc2_raw, bool adc2_valid,
                          int power_level, BalabalaInputEmit emit, void *context);
void balabala_input_to_app(void *app, BalabalaKey key, bool pressed);
#endif
