/* ADC ranges/pins derived from Open X4 E-Paper community-sdk InputManager,
 * commit 7d86603ad27709a9a766bb5ad893cfc39e60777e.
 * Copyright (c) 2025 Open X4 E-Paper Contributors, MIT.
 * Full notice: licenses/LICENSE-community-sdk.txt. */
#include <limits.h>
#include <string.h>
#include "balabala_input.h"

const BalabalaInputConfig balabala_input_default_config = {
    .adc1_keys = {BALABALA_FRONT1, BALABALA_FRONT2, BALABALA_FRONT3, BALABALA_FRONT4},
    .adc2_keys = {BALABALA_UP, BALABALA_DOWN},
};
int balabala_input_decode(unsigned bank, int raw) {
    static const int ranges1[] = {3900, 3100, 2090, 750, INT_MIN};
    static const int ranges2[] = {3900, 1120, INT_MIN};
    if (bank > 1 || raw < 0 || raw > 4095) return -2;
    const int *ranges = bank == 0 ? ranges1 : ranges2;
    unsigned count = bank == 0 ? 4 : 2;
    for (unsigned i = 0; i < count; ++i) {
        if (ranges[i + 1] < raw && raw <= ranges[i]) return (int)i;
    }
    return -1;
}
int balabala_input_init(BalabalaInput *input, const BalabalaInputConfig *config) {
    if (!input) return -1;
    if (!config) config = &balabala_input_default_config;
    unsigned seen = 0;
    for (unsigned i = 0; i < 6; ++i) {
        unsigned key = i < 4 ? config->adc1_keys[i] : config->adc2_keys[i - 4];
        if (key > BALABALA_DOWN || (seen & (1u << key))) return -1;
        seen |= 1u << key;
    }
    memset(input, 0, sizeof(*input));
    input->config = *config;
    input->adc[0].stable = input->adc[0].candidate = -1;
    input->adc[1].stable = input->adc[1].candidate = -1;
    input->power.stable = input->power.candidate = 1;
    return 0;
}
bool balabala_input_idle(const BalabalaInput *input) {
    return input->adc[0].stable == -1 && input->adc[0].candidate == -1 &&
        input->adc[1].stable == -1 && input->adc[1].candidate == -1 &&
        input->power.stable == 1 && input->power.candidate == 1;
}
static bool debounce(BalabalaInputDebounce *d, int value, bool valid, uint64_t now) {
    if (!valid) {
        d->candidate = d->stable;
        d->candidate_since = now;
        return false;
    }
    if (value != d->candidate) {
        d->candidate = value;
        d->candidate_since = now;
    }
    return value != d->stable && now - d->candidate_since >= BALABALA_INPUT_DEBOUNCE_MS;
}
static void pulse(BalabalaInputEmit emit, void *context, BalabalaKey key) {
    emit(context, key, true);
    emit(context, key, false);
}
void balabala_input_sample(BalabalaInput *input, uint64_t now,
                          int raw1, bool valid1, int raw2, bool valid2, int power_level,
                          BalabalaInputEmit emit, void *context) {
    if (!input || !emit) return;
    int values[] = {balabala_input_decode(0, raw1), balabala_input_decode(1, raw2)};
    bool valid[] = {valid1 && values[0] != -2, valid2 && values[1] != -2};
    for (unsigned i = 0; i < 2; ++i) {
        BalabalaInputDebounce *d = &input->adc[i];
        if (!debounce(d, values[i], valid[i], now)) continue;
        const BalabalaKey *keys = i == 0 ? input->config.adc1_keys : input->config.adc2_keys;
        if (d->stable >= 0) {
            if (keys[d->stable] == BALABALA_FRONT3 && input->parent_down &&
                !input->parent_sent && d->candidate_since - input->parent_down_since >= BALABALA_INPUT_PARENT_PRESS_MS) {
                input->parent_sent = true;
                pulse(emit, context, BALABALA_PARENT);
            }
            emit(context, keys[d->stable], false);
        }
        d->stable = d->candidate;
        if (d->stable >= 0) emit(context, keys[d->stable], true);
    }
    bool back_down = false, back_valid = false;
    for (unsigned i = 0; i < 2; ++i) {
        BalabalaInputDebounce *d = &input->adc[i];
        const BalabalaKey *keys = i == 0 ? input->config.adc1_keys : input->config.adc2_keys;
        if (d->stable >= 0 && keys[d->stable] == BALABALA_FRONT3) {
            back_down = true;
            back_valid = valid[i] && d->candidate == d->stable;
        }
    }
    if (back_down && !input->parent_down) {
        input->parent_down_since = now;
        input->parent_sent = false;
    }
    input->parent_down = back_down;
    if (back_valid && !input->parent_sent && now - input->parent_down_since >= BALABALA_INPUT_PARENT_PRESS_MS) {
        input->parent_sent = true;
        pulse(emit, context, BALABALA_PARENT);
    }
    BalabalaInputDebounce *p = &input->power;
    bool power_valid = power_level == 0 || power_level == 1;
    if (debounce(p, power_level, power_valid, now)) {
        if (p->candidate == 0) {
            input->power_down_since = now;
            input->shutdown_sent = false;
        } else if (!input->shutdown_sent) {
            uint64_t held = p->candidate_since - input->power_down_since;
            pulse(emit, context, held >= BALABALA_INPUT_LONG_PRESS_MS ? BALABALA_SHUTDOWN : BALABALA_HOME);
        }
        p->stable = p->candidate;
    }
    if (power_valid && p->stable == 0 && p->candidate == 0 && !input->shutdown_sent &&
        now - input->power_down_since >= BALABALA_INPUT_LONG_PRESS_MS) {
        input->shutdown_sent = true;
        pulse(emit, context, BALABALA_SHUTDOWN);
    }
}
void balabala_input_to_app(void *app, BalabalaKey key, bool pressed) {
    (void)balabala_app_key(app, key, pressed ? 1 : 0);
}
