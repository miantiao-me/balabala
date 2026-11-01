#ifndef BALABALA_DISPLAY_H
#define BALABALA_DISPLAY_H
#include <stdbool.h>
#include <stdint.h>
#include "balabala_probe.h"
#define BALABALA_PHYSICAL_WIDTH 800
#define BALABALA_PHYSICAL_HEIGHT 480
#define BALABALA_PHYSICAL_ROW_BYTES 100
#define BALABALA_EPD_BUSY_TIMEOUT_MS 30000
#define BALABALA_EPD_PARTIAL_UPDATE 0xdf
/* 默认连续局刷上限；家长可选8/16/32/64/128，残影仍需真机观察。 */
#define BALABALA_EPD_PARTIAL_LIMIT 8
/* 同一任务调用。service(ms)须采样输入/收集请求，并在ms>0时让出CPU；
 * 不得嵌套渲染、保存或SPI。transport返回0成功，非0失败；busy为1忙/0闲/<0错误。 */
typedef struct {
    int (*write)(void *context, bool data, const uint8_t *bytes, size_t length);
    int (*reset)(void *context, bool high);
    int (*busy)(void *context);
    uint64_t (*now_ms)(void *context);
    void (*service)(void *context, uint32_t delay_ms);
    void *context;
    bool baseline_valid;
    uint8_t partials;
    uint8_t partial_limit; /* 0使用默认值；更新此值不清零已累计次数。 */
} BalabalaDisplay;
enum { BALABALA_EPD_OK=0, BALABALA_EPD_IO=-1, BALABALA_EPD_TIMEOUT=-2 };
/* 逆时针90度：out(x,y)=logical(479-y,x)，相对旧传输帧旋转180度。
 * controller仍按Gate从RAM y=479递减发送；传输行0不代表机身顶部。
 * 旧版倒置已经用户实机确认；修复后物理朝向待验证。MSB左，1白0黑。
 * 调用方提供48000B只读帧与100B不重叠输出；无效row返回-1且不写输出。 */
int balabala_display_rotate_row(const uint8_t *frame, unsigned row, uint8_t *out);
/* 仅允许且基准有效、未到局刷上限时走DF；其他情况完整reset/init全刷F7。
 * 旧帧保存在控制器0x26，局刷成功后同步；任一步失败或休眠均使基准失效。
 * 每个BUSY等待最多30秒；失败不表示显示完成，重试由平台策略决定。 */
int balabala_display_refresh(BalabalaDisplay *display, const uint8_t *frame, bool partial);
/* 即使上一轮失败也尝试关analog/clock，等待有界；最后尝试0x10/0x01。 */
int balabala_display_sleep(BalabalaDisplay *display);
#endif
