#include "oled_dashboard.h"

#include <stdio.h>
#include <string.h>

/* 公共领域 5×7 ASCII 字模，范围 0x20 至 0x5F。 */
static const uint8_t s_font5x7[64][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
    {0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},
    {0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40}
};

static void pixel(uint8_t *fb, unsigned x, unsigned y, bool on)
{
    if ((x < OLED_DASHBOARD_WIDTH) && (y < OLED_DASHBOARD_HEIGHT))
    {
        size_t index = (y / 8U) * OLED_DASHBOARD_WIDTH + x;
        uint8_t mask = (uint8_t)(1U << (y & 7U));
        if (on)
        {
            fb[index] |= mask;
        }
        else
        {
            fb[index] &= (uint8_t)~mask;
        }
    }
}

static void rectangle(uint8_t *fb, unsigned x, unsigned y,
                      unsigned width, unsigned height)
{
    for (unsigned row = 0U; row < height; ++row)
    {
        for (unsigned column = 0U; column < width; ++column)
        {
            pixel(fb, x + column, y + row, true);
        }
    }
}

static unsigned text_width(const char *text, unsigned scale)
{
    size_t count = strlen(text);
    return count == 0U ? 0U : (unsigned)(count * 6U - 1U) * scale;
}

static void text(uint8_t *fb, unsigned x, unsigned y, const char *value,
                 unsigned scale, bool on)
{
    while ((*value != '\0') && (x < OLED_DASHBOARD_WIDTH))
    {
        unsigned char character = (unsigned char)*value++;
        if ((character >= 'a') && (character <= 'z'))
        {
            character = (unsigned char)(character - 'a' + 'A');
        }
        if ((character < ' ') || (character > '_'))
        {
            character = '?';
        }
        for (unsigned column = 0U; column < 5U; ++column)
        {
            for (unsigned row = 0U; row < 7U; ++row)
            {
                if ((s_font5x7[character - ' '][column] & (1U << row)) != 0U)
                {
                    for (unsigned dx = 0U; dx < scale; ++dx)
                    {
                        for (unsigned dy = 0U; dy < scale; ++dy)
                        {
                            pixel(fb, x + column * scale + dx,
                                  y + row * scale + dy, on);
                        }
                    }
                }
            }
        }
        x += 6U * scale;
    }
}

static void right_text(uint8_t *fb, unsigned y, const char *value)
{
    unsigned width = text_width(value, 1U);
    text(fb, width < OLED_DASHBOARD_WIDTH ? OLED_DASHBOARD_WIDTH - width : 0U,
         y, value, 1U, true);
}

/* 用勾和叉标记就绪状态。 */
static void status_mark(uint8_t *fb, unsigned x, bool ready)
{
    if (ready)
    {
        pixel(fb, x, 4U, false);
        pixel(fb, x + 1U, 5U, false);
        for (unsigned i = 0U; i < 5U; ++i)
        {
            pixel(fb, x + 2U + i, 6U - i, false);
        }
    }
    else
    {
        for (unsigned i = 0U; i < 5U; ++i)
        {
            pixel(fb, x + i, 2U + i, false);
            pixel(fb, x + i, 6U - i, false);
        }
    }
}

static void header(uint8_t *fb, const oled_dashboard_view_t *view)
{
    static const uint8_t cloud[13] = {
        0x18,0x24,0x24,0x26,0x21,0x21,0x21,0x22,0x24,0x24,0x24,0x18,0x00
    };
    rectangle(fb, 0U, 0U, 128U, 9U);
    text(fb, 2U, 1U, "AIR NODE", 1U, false);
    text(fb, 66U, 1U, "RF", 1U, false);
    if (view->radio_ready &&
        !(view->telemetry_valid && view->telemetry_fresh))
    {
        /* 只有新鲜帧才能证明链路在线。 */
        text(fb, 81U, 1U, "!", 1U, false);
    }
    else
    {
        status_mark(fb, 81U, view->radio_ready);
    }
    for (unsigned x = 0U; x < 13U; ++x)
    {
        for (unsigned y = 0U; y < 6U; ++y)
        {
            if ((cloud[x] & (1U << y)) != 0U)
            {
                pixel(fb, 101U + x, 1U + y, false);
            }
        }
    }
    status_mark(fb, 120U, view->cloud_ready);
}

static void sensors(uint8_t *fb, const oled_dashboard_view_t *view)
{
    char value[24];
    const telemetry_packet_t *packet = &view->telemetry;
    bool current = view->telemetry_valid && view->telemetry_fresh;

    text(fb, 2U, 12U, "VOC INDEX", 1U, true);
    if (current && ((packet->flags & TELEMETRY_FLAG_SGP40_VALID) != 0U))
    {
        snprintf(value, sizeof(value), "%u", (unsigned)packet->voc_index);
    }
    else
    {
        snprintf(value, sizeof(value), "--");
    }
    text(fb, 2U, 24U, value, 2U, true);
    rectangle(fb, 68U, 12U, 1U, 27U);

    if (current && ((packet->flags & TELEMETRY_FLAG_BME680_VALID) != 0U))
    {
        int temperature = (int)packet->temperature_x10;
        unsigned magnitude = (unsigned)(temperature < 0 ? -temperature : temperature);
        /* 单独绘制符号，保留 -0.1℃ 之类的负温度。 */
        snprintf(value, sizeof(value), "T %s%u.%u C", temperature < 0 ? "-" : "",
                 magnitude / 10U, magnitude % 10U);
        text(fb, 74U, 11U, value, 1U, true);
        snprintf(value, sizeof(value), "H %u.%u %%",
                 (unsigned)packet->humidity_x10 / 10U,
                 (unsigned)packet->humidity_x10 % 10U);
        text(fb, 74U, 21U, value, 1U, true);
        snprintf(value, sizeof(value), "P %uHPA",
                 ((unsigned)packet->pressure_x10 + 5U) / 10U);
        text(fb, 74U, 31U, value, 1U, true);
    }
    else
    {
        text(fb, 74U, 11U, "T -- C", 1U, true);
        text(fb, 74U, 21U, "H -- %", 1U, true);
        text(fb, 74U, 31U, "P --HPA", 1U, true);
    }
}

const char *oled_dashboard_event_status(const oled_dashboard_view_t *view)
{
    const telemetry_packet_t *packet;
    uint32_t age;
    if (view == NULL || !view->telemetry_valid) return "WAIT";
    packet = &view->telemetry;
    if (packet->protocol_version != 2U) return "LEGACY";
    if (!view->telemetry_fresh || !view->radio_ready) return "STALE";
    if ((packet->flags & (TELEMETRY_FLAG_BME680_VALID | TELEMETRY_FLAG_SGP40_VALID)) !=
        (TELEMETRY_FLAG_BME680_VALID | TELEMETRY_FLAG_SGP40_VALID)) return "SENS ERR";
    if (packet->event_state == TELEMETRY_EVENT_WARMUP) return "WARMUP";
    if (packet->event_state == TELEMETRY_EVENT_COLLECT) return "INIT";
    age = packet->uptime_ms - packet->event_end_ms;
    if (age > 15000U || view->rx_age_ms > 15000U-age) return "STALE";
    if ((packet->flags & TELEMETRY_FLAG_EVENT_READY) == 0U ||
        packet->event_voc_x10 == 0U || packet->event_voc_x10 > 5000U) return "INVALID";
    switch (packet->event_state)
    {
        case TELEMETRY_EVENT_NORMAL: return "NORMAL";
        case TELEMETRY_EVENT_SUSPECTED: return "SUSPECT";
        case TELEMETRY_EVENT_ALARM: return "ALARM";
        case TELEMETRY_EVENT_RECOVERY: return "RECOVERY";
        default: return "INVALID";
    }
}

static void event_view(uint8_t *fb, const oled_dashboard_view_t *view)
{
    const telemetry_packet_t *packet = &view->telemetry;
    const char *status = oled_dashboard_event_status(view);
    char detail[24];
    rectangle(fb, 0U, 39U, 128U, 1U);
    text(fb, 2U, 41U, "EVENT", 1U, true);
    right_text(fb, 41U, status);
    if (packet->event_state >= TELEMETRY_EVENT_NORMAL &&
        packet->event_state <= TELEMETRY_EVENT_RECOVERY &&
        strcmp(status, "STALE") != 0 && strcmp(status, "INVALID") != 0 &&
        strcmp(status, "SENS ERR") != 0 && strcmp(status, "WAIT") != 0)
    {
        snprintf(detail, sizeof(detail), "10S MEAN %u.%u",
                 (unsigned)packet->event_voc_x10/10U,
                 (unsigned)packet->event_voc_x10%10U);
        text(fb, 2U, 49U, detail, 1U, true);
    }
    else text(fb, 2U, 49U, "NO CURRENT SCORE", 1U, true);
}

static void forecast(uint8_t *fb, const oled_dashboard_view_t *view)
{
    const telemetry_packet_t *packet = &view->telemetry;
    const char *state = "WAIT";
    bool direction = false;
    bool ai_valid = (packet->flags & TELEMETRY_FLAG_AI_VALID) != 0U;

    if (packet->protocol_version == 2U)
    {
        event_view(fb, view);
        return;
    }

    rectangle(fb, 0U, 39U, 128U, 1U);
    rectangle(fb, 0U, 44U, 23U, 9U);
    text(fb, 3U, 45U, "30M", 1U, false);
    if (!view->telemetry_valid)
    {
        state = "WAIT";
    }
    else if (!view->telemetry_fresh)
    {
        state = "STALE";
    }
    else
    {
        switch (packet->ai_state)
        {
            case TELEMETRY_AI_WARMUP: state = "WARMUP"; break;
            case TELEMETRY_AI_COLLECT: state = "COLLECT"; break;
            case TELEMETRY_AI_ERROR: state = "ERROR"; break;
            case TELEMETRY_AI_UNCERTAIN: state = "UNCERT"; break;
            case TELEMETRY_AI_DOWN:
            case TELEMETRY_AI_UP:
                direction = ai_valid;
                state = !direction ? "WAIT" :
                    (packet->ai_state == TELEMETRY_AI_UP ? "UP" : "DOWN");
                break;
            default: state = "ERROR"; break;
        }
    }

    if (packet->protocol_version == 1U)
    {
        char legacy[24];
        /* 旧帧可查看，不显示为 AQ2 事件。 */
        rectangle(fb, 0U, 39U, 128U, 1U);
        for (unsigned y = 40U; y < 56U; ++y)
            for (unsigned x = 0U; x < 128U; ++x) pixel(fb, x, y, false);
        snprintf(legacy, sizeof(legacy), "LEGACY 30M %s", state);
        text(fb, 2U, 41U, legacy, 1U, true);
        if (direction && packet->confidence_x1000 <= 1000U)
        {
            snprintf(legacy, sizeof(legacy), "CONF %u%%", (unsigned)packet->confidence_x1000/10U);
            text(fb, 2U, 49U, legacy, 1U, true);
        }
        return;
    }
    if (direction)
    {
        /* 箭头仅表示旧版模型的涨跌方向。 */
        bool up = packet->ai_state == TELEMETRY_AI_UP;
        rectangle(fb, 32U, 45U, 1U, 7U);
        for (unsigned i = 1U; i <= 3U; ++i)
        {
            unsigned y = up ? 45U + i : 51U - i;
            pixel(fb, 32U - i, y, true);
            pixel(fb, 32U + i, y, true);
        }
        text(fb, 40U, 41U, state, 2U, true);
        char confidence[16];
        /* 百分数是模型分数，取整仅影响显示。 */
        if (packet->confidence_x1000 <= 1000U)
        {
            snprintf(confidence, sizeof(confidence), "%u%%%s",
                     (unsigned)packet->confidence_x1000 / 10U,
                     packet->confidence_x1000 < TELEMETRY_LOW_CONFIDENCE_X1000
                         ? "LOW" : "");
        }
        else
        {
            snprintf(confidence, sizeof(confidence), "--%%");
        }
        right_text(fb, 45U, confidence);
    }
    else
    {
        text(fb, 29U, 41U, state, 2U, true);
    }
}

static void reception(uint8_t *fb, const oled_dashboard_view_t *view)
{
    char value[24];
    if (view->telemetry_valid)
    {
        /* 过期时仍显示上一帧 RSSI 和距接收的时间。 */
        snprintf(value, sizeof(value), "R%dDBM", (int)view->rssi_dbm);
        text(fb, 0U, 57U, value, 1U, true);
        snprintf(value, sizeof(value), "RX%luS", (unsigned long)(view->rx_age_ms / 1000U));
        right_text(fb, 57U, value);
    }
    else
    {
        text(fb, 0U, 57U, "R--DBM", 1U, true);
        right_text(fb, 57U, "RX--S");
    }
}

bool oled_dashboard_render(uint8_t *fb, size_t fb_size,
                           const oled_dashboard_view_t *view)
{
    if ((fb == NULL) || (view == NULL) ||
        (fb_size < OLED_DASHBOARD_FRAMEBUFFER_SIZE))
    {
        return false;
    }
    memset(fb, 0, OLED_DASHBOARD_FRAMEBUFFER_SIZE);
    header(fb, view);
    sensors(fb, view);
    forecast(fb, view);
    reception(fb, view);
    return true;
}
