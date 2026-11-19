#ifndef OLED_NODE_H
#define OLED_NODE_H

#include <stdint.h>
#include <stddef.h>

#define OLED_NODE_WIDTH 128U
#define OLED_NODE_HEIGHT 64U
#define OLED_NODE_BUFFER_SIZE 1024U

typedef enum
{
    OLED_NODE_WARMUP = 0,
    OLED_NODE_COLLECT,
    OLED_NODE_DOWN,
    OLED_NODE_UP,
    OLED_NODE_UNCERTAIN,
    OLED_NODE_AI_ERROR
} OLED_Node_State_t;

typedef struct
{
    /* 温度为摄氏度，湿度为 %RH，气压为 hPa；置信度取 0～1。 */
    float temperature;
    float humidity;
    float pressure;
    float confidence;
    int32_t voc_index;
    uint32_t warmup_seconds;
    uint32_t history_minutes; /* 已完成的有效分钟窗数。 */
    OLED_Node_State_t state;
    uint8_t fresh;
    uint8_t environment_valid;
    uint8_t voc_valid;
    uint8_t prediction_valid;
    uint8_t low_confidence;
    /* 事件视图：1 为固定规则，2 为板载 MLP。 */
    uint8_t event_mode;
    uint8_t event_state; /* 事件状态：0 预热、1 初始化、2 正常、3 疑似、4 告警、5 恢复、6 无效。 */
    uint8_t event_ready;
    uint32_t bootstrap_bins;
    float event_voc_mean;
    float event_score;
} OLED_Node_View_t;

/* 返回显示用的状态文字。 */
const char *OLED_Node_EventStatus(const OLED_Node_View_t *view);

/* 渲染 1024 字节 SSD1306 页缓冲，不访问设备。参数无效返回 0，不写缓冲。 */
uint8_t OLED_Node_Render(const OLED_Node_View_t *view, uint8_t *buffer,
                         size_t buffer_size);

#endif
