#include "oled_node.h"

#include <stdio.h>
#include <string.h>

/* 公有领域 5×7 ASCII 字形，范围为空格到下划线。 */
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

static void OLED_Node_Pixel(uint8_t *buffer, unsigned x, unsigned y, uint8_t on)
{
    if ((x < OLED_NODE_WIDTH) && (y < OLED_NODE_HEIGHT))
    {
        unsigned index = (y / 8U) * OLED_NODE_WIDTH + x;
        uint8_t mask = (uint8_t)(1U << (y & 7U));
        if (on != 0U) buffer[index] |= mask;
        else buffer[index] &= (uint8_t)~mask;
    }
}

static void OLED_Node_Rect(uint8_t *buffer, unsigned x, unsigned y,
                           unsigned width, unsigned height, uint8_t on)
{
    unsigned row, column;
    for (row = 0U; row < height; ++row)
        for (column = 0U; column < width; ++column)
            OLED_Node_Pixel(buffer, x + column, y + row, on);
}

static unsigned OLED_Node_TextWidth(const char *value)
{
    size_t count = strlen(value);
    return count != 0U ? (unsigned)(count * 6U - 1U) : 0U;
}

static void OLED_Node_Text(uint8_t *buffer, unsigned x, unsigned y,
                           const char *value, unsigned scale, uint8_t on)
{
    static const uint8_t lower_h[5] = {0x7F,0x08,0x04,0x04,0x78};
    static const uint8_t lower_a[5] = {0x20,0x54,0x54,0x54,0x78};
    unsigned column, row, dx, dy;
    while ((*value != '\0') && (x < OLED_NODE_WIDTH))
    {
        unsigned char character = (unsigned char)*value++;
        const uint8_t *glyph;
        if (character == 'h') glyph = lower_h;
        else if (character == 'a') glyph = lower_a;
        else
        {
            if ((character < ' ') || (character > '_')) character = '?';
            glyph = s_font5x7[character - ' '];
        }
        for (column = 0U; column < 5U; ++column)
            for (row = 0U; row < 7U; ++row)
                if ((glyph[column] & (1U << row)) != 0U)
                    for (dx = 0U; dx < scale; ++dx)
                        for (dy = 0U; dy < scale; ++dy)
                            OLED_Node_Pixel(buffer, x + column * scale + dx,
                                            y + row * scale + dy, on);
        x += 6U * scale;
    }
}

static uint8_t OLED_Node_InRange(float value, float minimum, float maximum)
{
    /* 比较时一并排除 NaN 与无穷大。 */
    return (uint8_t)((value >= minimum) && (value <= maximum));
}

static void OLED_Node_Bar(uint8_t *buffer, uint32_t count, uint32_t total)
{
    if (count > total) count = total;
    OLED_Node_Rect(buffer, 68U, 55U, 58U, 7U, 1U);
    OLED_Node_Rect(buffer, 69U, 56U, 56U, 5U, 0U);
    OLED_Node_Rect(buffer, 70U, 57U, (unsigned)((count * 54U) / total), 3U, 1U);
}

static void OLED_Node_Arrow(uint8_t *buffer, uint8_t up)
{
    unsigned row, column;
    for (row = 0U; row < 5U; ++row)
        for (column = 4U - row; column <= 4U + row; ++column)
            OLED_Node_Pixel(buffer, 26U + column,
                            up != 0U ? 53U + row : 62U - row, 1U);
    OLED_Node_Rect(buffer, 29U, up != 0U ? 57U : 53U, 3U, 6U, 1U);
}

const char *OLED_Node_EventStatus(const OLED_Node_View_t *view)
{
    if (view == NULL) return "INVALID";
    if (view->fresh == 0U) return "STALE";
    if (view->environment_valid == 0U || view->voc_valid == 0U ||
        !OLED_Node_InRange(view->temperature, -40.f, 85.f) ||
        !OLED_Node_InRange(view->humidity, 0.f, 100.f) ||
        !OLED_Node_InRange(view->pressure, 300.f, 1100.f) ||
        view->voc_index < 0 || view->voc_index > 500)
        return "SENS ERR";
    if (view->event_state == 0U) return "WARMUP";
    if (view->event_state == 1U) return "INIT";
    if (view->event_ready == 0U || view->voc_index == 0 ||
        !OLED_Node_InRange(view->event_voc_mean, 1.f, 500.f) ||
        (view->event_mode == 2U && !OLED_Node_InRange(view->event_score, 0.f, 1.f))) return "INVALID";
    switch (view->event_state)
    {
        case 2U: return "NORMAL";
        case 3U: return "SUSPECT";
        case 4U: return view->event_mode == 2U ? "AI EVENT" : "ALARM";
        case 5U: return "RECOVERY";
        default: return "INVALID";
    }
}

uint8_t OLED_Node_Render(const OLED_Node_View_t *view, uint8_t *buffer,
                         size_t buffer_size)
{
    char value[24];
    const char *status;
    uint8_t environment_ok, voc_ok, sensors_ok, confidence_ok, prediction_ok;
    uint32_t seconds, history, percent;

    if ((view == NULL) || (buffer == NULL) ||
        (buffer_size < OLED_NODE_BUFFER_SIZE)) return 0U;

    environment_ok = (uint8_t)((view->fresh != 0U) &&
                      (view->environment_valid != 0U) &&
                      OLED_Node_InRange(view->temperature, -40.0f, 85.0f) &&
                      OLED_Node_InRange(view->humidity, 0.0f, 100.0f) &&
                      OLED_Node_InRange(view->pressure, 300.0f, 1100.0f));
    voc_ok = (uint8_t)((view->fresh != 0U) && (view->voc_valid != 0U) &&
                      (view->voc_index >= 1) && (view->voc_index <= 500));
    sensors_ok = (uint8_t)(environment_ok && voc_ok);
    confidence_ok = OLED_Node_InRange(view->confidence, 0.0f, 1.0f);
    prediction_ok = (uint8_t)(sensors_ok && confidence_ok &&
                    (view->prediction_valid != 0U) &&
                    ((view->state == OLED_NODE_UP) ||
                     (view->state == OLED_NODE_DOWN)));
    if (view->event_mode != 0U) status = OLED_Node_EventStatus(view);
    else if (view->fresh == 0U) status = "STALE";
    else if ((view->environment_valid == 0U) || (view->voc_valid == 0U) ||
             (environment_ok == 0U) || ((voc_ok == 0U) && (view->voc_index != 0)))
        status = "SENS ERR";
    else if (view->state == OLED_NODE_WARMUP) status = "WARMUP";
    else if (view->state == OLED_NODE_COLLECT) status = "COLLECT";
    else if ((view->state == OLED_NODE_AI_ERROR) ||
             ((view->prediction_valid != 0U) && (confidence_ok == 0U)))
        status = "AI ERR";
    else if (view->state == OLED_NODE_UNCERTAIN) status = "UNCERT";
    else if ((prediction_ok != 0U) && (view->low_confidence != 0U)) status = "LOW";
    else if (prediction_ok != 0U) status = "READY";
    else status = "WAIT";

    memset(buffer, 0, OLED_NODE_BUFFER_SIZE);
    OLED_Node_Rect(buffer, 0U, 0U, 128U, 10U, 1U);
    OLED_Node_Text(buffer, 3U, 1U, view->event_mode == 2U ? "EDGE AI" : "AIR NODE", 1U, 0U);
    OLED_Node_Text(buffer, 125U - OLED_Node_TextWidth(status), 1U, status, 1U, 0U);
    OLED_Node_Text(buffer, 2U, 14U, "VOC INDEX", 1U, 1U);
    if (voc_ok != 0U)
        (void)snprintf(value, sizeof(value), "%ld", (long)view->voc_index);
    else
        (void)snprintf(value, sizeof(value), "---");
    OLED_Node_Text(buffer, 3U, 25U, value, 3U, 1U);
    OLED_Node_Rect(buffer, 65U, 14U, 1U, 31U, 1U);

    if (environment_ok != 0U)
        (void)snprintf(value, sizeof(value), "T%5.1fC", (double)view->temperature);
    else (void)snprintf(value, sizeof(value), "T --- C");
    OLED_Node_Text(buffer, 70U, 14U, value, 1U, 1U);
    if (environment_ok != 0U)
        (void)snprintf(value, sizeof(value), "H%5.1f%%", (double)view->humidity);
    else (void)snprintf(value, sizeof(value), "H --- %%");
    OLED_Node_Text(buffer, 70U, 26U, value, 1U, 1U);
    if (environment_ok != 0U)
        (void)snprintf(value, sizeof(value), "%4.0fhPa", (double)view->pressure);
    else (void)snprintf(value, sizeof(value), "--- hPa");
    OLED_Node_Text(buffer, 70U, 38U, value, 1U, 1U);
    OLED_Node_Rect(buffer, 0U, 49U, 128U, 1U, 1U);

    if (view->event_mode != 0U)
    {
        if (strcmp(status, "WARMUP") == 0)
        {
            seconds = view->warmup_seconds > 5999U ? 5999U : view->warmup_seconds;
            (void)snprintf(value, sizeof(value), "WAIT %02lu:%02lu",
                          (unsigned long)(seconds/60U), (unsigned long)(seconds%60U));
            OLED_Node_Text(buffer, 2U, 55U, value, 1U, 1U);
            OLED_Node_Bar(buffer, view->warmup_seconds >= 3600U ? 0U :
                          3600U-view->warmup_seconds, 3600U);
        }
        else if (view->event_mode == 2U)
        {
            if (sensors_ok && view->event_ready && view->event_state >= 2U &&
                view->event_state <= 5U && OLED_Node_InRange(view->event_voc_mean, 1.f, 500.f) &&
                OLED_Node_InRange(view->event_score, 0.f, 1.f))
            {
                uint32_t score = (uint32_t)(view->event_score * 1000.f + 0.5f);
                (void)snprintf(value, sizeof(value), "MLP %lu.%03lu",
                              (unsigned long)(score / 1000U), (unsigned long)(score % 1000U));
                OLED_Node_Text(buffer, 2U, 55U, value, 1U, 1U);
            }
            else OLED_Node_Text(buffer, 2U, 55U, strcmp(status, "INIT") == 0 ?
                                "MODEL INIT" : "NO AI SCORE", 1U, 1U);
        }
        else if (strcmp(status, "INIT") == 0)
        {
            history = view->bootstrap_bins > 32U ? 32U : view->bootstrap_bins;
            (void)snprintf(value, sizeof(value), "INIT %lu/32", (unsigned long)history);
            OLED_Node_Text(buffer, 2U, 55U, value, 1U, 1U);
            OLED_Node_Bar(buffer, history, 32U);
        }
        else if (sensors_ok && view->event_ready && view->event_state >= 2U &&
                 view->event_state <= 5U && OLED_Node_InRange(view->event_voc_mean, 1.f, 500.f))
        {
            (void)snprintf(value, sizeof(value), "10S MEAN %5.1f", (double)view->event_voc_mean);
            OLED_Node_Text(buffer, 2U, 55U, value, 1U, 1U);
        }
        else OLED_Node_Text(buffer, 2U, 55U, "EVENT UNAVAILABLE", 1U, 1U);
    }
    else if ((view->fresh != 0U) && (view->environment_valid != 0U) &&
        (view->voc_valid != 0U) && (environment_ok != 0U) &&
        ((voc_ok != 0U) || (view->voc_index == 0)) &&
        (view->state == OLED_NODE_WARMUP))
    {
        seconds = view->warmup_seconds;
        if (seconds > 5999U) seconds = 5999U;
        (void)snprintf(value, sizeof(value), "WAIT %02lu:%02lu",
                      (unsigned long)(seconds / 60U), (unsigned long)(seconds % 60U));
        OLED_Node_Text(buffer, 2U, 55U, value, 1U, 1U);
        OLED_Node_Bar(buffer, view->warmup_seconds >= 3600U ? 0U :
                     3600U - view->warmup_seconds, 3600U);
    }
    else if ((sensors_ok != 0U) && (view->state == OLED_NODE_COLLECT))
    {
        history = view->history_minutes;
        if (history > 30U) history = 30U;
        (void)snprintf(value, sizeof(value), "HIST %lu/30", (unsigned long)history);
        OLED_Node_Text(buffer, 2U, 55U, value, 1U, 1U);
        OLED_Node_Bar(buffer, history, 30U);
    }
    else if (prediction_ok != 0U)
    {
        OLED_Node_Text(buffer, 2U, 55U, "30M", 1U, 1U);
        OLED_Node_Arrow(buffer, (uint8_t)(view->state == OLED_NODE_UP));
        OLED_Node_Text(buffer, 39U, 55U, view->state == OLED_NODE_UP ? "UP" : "DOWN",
                       1U, 1U);
        percent = (uint32_t)(view->confidence * 100.0f);
        (void)snprintf(value, sizeof(value), view->low_confidence != 0U ?
                      "LOW %lu%%" : "CONF%lu%%", (unsigned long)percent);
        OLED_Node_Text(buffer, 126U - OLED_Node_TextWidth(value), 55U, value, 1U, 1U);
    }
    else
        OLED_Node_Text(buffer, 2U, 55U, view->state == OLED_NODE_AI_ERROR ?
                       "30M MODEL ERROR" : "30M WAIT", 1U, 1U);
    return 1U;
}

