#ifndef OLED_DASHBOARD_H
#define OLED_DASHBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "telemetry_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_DASHBOARD_WIDTH 128U
#define OLED_DASHBOARD_HEIGHT 64U
#define OLED_DASHBOARD_FRAMEBUFFER_SIZE 1024U

typedef struct
{
    bool radio_ready;
    bool cloud_ready;
    bool telemetry_valid;
    bool telemetry_fresh;
    uint32_t rx_age_ms;
    int16_t rssi_dbm;
    telemetry_packet_t telemetry;
} oled_dashboard_view_t;

/**
 * @brief 按 SSD1306 页顺序绘制 128×64 图像，不操作硬件。
 * 缓冲须至少 1024 字节且不与 view 重叠；参数无效时不写入。
 */
bool oled_dashboard_render(uint8_t *fb, size_t fb_size,
                           const oled_dashboard_view_t *view);
/* 仅显示 AQ2 状态，过期或无效不显示为正常。 */
const char *oled_dashboard_event_status(const oled_dashboard_view_t *view);

#ifdef __cplusplus
}
#endif

#endif /* OLED_DASHBOARD_H */
