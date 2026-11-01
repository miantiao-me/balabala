/* SSD1677 X4序列参考community-sdk 7d86603 (MIT)，完整归属见licenses/README.md。 */
#include "balabala_display.h"
#include <string.h>

int balabala_display_rotate_row(const uint8_t *frame, unsigned row, uint8_t *out) {
    if (!frame || !out || row >= BALABALA_PHYSICAL_HEIGHT) return -1;
    memset(out, 0, BALABALA_PHYSICAL_ROW_BYTES);
    const unsigned logical_x = BALABALA_WIDTH - 1 - row;
    const unsigned byte = logical_x / 8;
    const uint8_t mask = (uint8_t)(0x80u >> (logical_x % 8));
    for (unsigned x = 0; x < BALABALA_PHYSICAL_WIDTH; ++x) {
        if (frame[x * (BALABALA_WIDTH / 8) + byte] & mask)
            out[x / 8] |= (uint8_t)(0x80u >> (x % 8));
    }
    return 0;
}
static int command(BalabalaDisplay *d, uint8_t cmd) {
    return d->write(d->context, false, &cmd, 1) ? BALABALA_EPD_IO : 0;
}
static int reg(BalabalaDisplay *d, uint8_t cmd, const uint8_t *bytes, size_t length) {
    if (command(d, cmd)) return BALABALA_EPD_IO;
    return d->write(d->context, true, bytes, length) ? BALABALA_EPD_IO : 0;
}
static int byte(BalabalaDisplay *d, uint8_t cmd, uint8_t value) {
    return reg(d, cmd, &value, 1);
}
static int wait_ready(BalabalaDisplay *d) {
    uint64_t start = d->now_ms(d->context);
    /* 给控制器机会拉高BUSY，避免命令刚传完就把尚未拉高当作完成。 */
    d->service(d->context, 5);
    for (;;) {
        int busy = d->busy(d->context);
        if (busy < 0) return BALABALA_EPD_IO;
        if (!busy) return 0;
        if (d->now_ms(d->context) - start >= BALABALA_EPD_BUSY_TIMEOUT_MS)
            return BALABALA_EPD_TIMEOUT;
        d->service(d->context, 5);
    }
}
static int area(BalabalaDisplay *d) {
    const uint8_t xs[] = {0, 0, 0x1f, 0x03}; /* 0..799，像素地址而非字节。 */
    const uint8_t ys[] = {0xdf, 0x01, 0, 0}; /* Gate 479..0 */
    const uint8_t xc[] = {0, 0}, yc[] = {0xdf, 0x01};
    if (byte(d, 0x11, 0x01) || reg(d, 0x44, xs, sizeof(xs)) ||
        reg(d, 0x45, ys, sizeof(ys)) || reg(d, 0x4e, xc, sizeof(xc)) ||
        reg(d, 0x4f, yc, sizeof(yc))) return BALABALA_EPD_IO;
    return 0;
}
static int initialize(BalabalaDisplay *d) {
    const uint8_t booster[] = {0xae, 0xc7, 0xc3, 0xc0, 0x40};
    const uint8_t driver[] = {0xdf, 0x01, 0x02};
    if (d->reset(d->context, true)) return BALABALA_EPD_IO;
    d->service(d->context, 20);
    if (d->reset(d->context, false)) return BALABALA_EPD_IO;
    d->service(d->context, 2);
    if (d->reset(d->context, true)) return BALABALA_EPD_IO;
    d->service(d->context, 20);
    if (command(d, 0x12)) return BALABALA_EPD_IO;
    int result = wait_ready(d);
    if (result) return result;
    if (byte(d, 0x18, 0x80) || reg(d, 0x0c, booster, sizeof(booster)) ||
        reg(d, 0x01, driver, sizeof(driver)) || byte(d, 0x3c, 0x01) || area(d))
        return BALABALA_EPD_IO;
    for (unsigned i = 0; i < 2; ++i) {
        if (byte(d, i == 0 ? 0x46 : 0x47, 0xf7)) return BALABALA_EPD_IO;
        result = wait_ready(d);
        if (result) return result;
    }
    return 0;
}
static int write_frame(BalabalaDisplay *d, const uint8_t *frame, uint8_t bank) {
    uint8_t row[BALABALA_PHYSICAL_ROW_BYTES];
    /* 重新设counter，避免依赖上一RAM写完后的自动回卷行为。 */
    if (area(d) || command(d, bank)) return BALABALA_EPD_IO;
    for (unsigned y = 0; y < BALABALA_PHYSICAL_HEIGHT; ++y) {
        balabala_display_rotate_row(frame, y, row);
        if (d->write(d->context, true, row, sizeof(row))) return BALABALA_EPD_IO;
        d->service(d->context, 0);
    }
    return 0;
}
int balabala_display_refresh(BalabalaDisplay *d, const uint8_t *frame, bool partial) {
    if (!d) return BALABALA_EPD_IO;
    uint8_t limit = d->partial_limit ? d->partial_limit : BALABALA_EPD_PARTIAL_LIMIT;
    bool fast = partial && d->baseline_valid && d->partials < limit;
    d->baseline_valid = false;
    if (!frame || !d->write || !d->reset || !d->busy || !d->now_ms || !d->service)
        return BALABALA_EPD_IO;
    int result;
    if (fast) {
        /* reset/init会清掉0x26里的旧帧，局刷必须保留控制器基准。 */
        if (write_frame(d, frame, 0x24) || byte(d, 0x21, 0x00) ||
            byte(d, 0x22, BALABALA_EPD_PARTIAL_UPDATE) || command(d, 0x20))
            return BALABALA_EPD_IO;
        result = wait_ready(d);
        if (result) return result;
        if (write_frame(d, frame, 0x26)) return BALABALA_EPD_IO;
    } else {
        result = initialize(d);
        if (result) return result;
        if (write_frame(d, frame, 0x24) || write_frame(d, frame, 0x26) ||
            byte(d, 0x21, 0x40) || byte(d, 0x22, 0xf7) || command(d, 0x20))
            return BALABALA_EPD_IO;
        result = wait_ready(d);
        if (result) return result;
    }
    d->partials = fast ? d->partials + 1 : 0;
    d->baseline_valid = true;
    return 0;
}
int balabala_display_sleep(BalabalaDisplay *d) {
    if (!d) return BALABALA_EPD_IO;
    d->baseline_valid = false;
    if (!d->write || !d->busy || !d->now_ms || !d->service) return BALABALA_EPD_IO;
    int result = 0;
    if (byte(d, 0x21, 0x40) || byte(d, 0x22, 0x03) || command(d, 0x20))
        result = BALABALA_EPD_IO;
    else result = wait_ready(d);
    if (byte(d, 0x10, 0x01)) result = BALABALA_EPD_IO;
    return result;
}
