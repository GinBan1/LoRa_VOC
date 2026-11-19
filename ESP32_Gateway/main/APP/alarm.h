#ifndef ALARM_H
#define ALARM_H

#include <stdbool.h>
#include <stdint.h>

#define ALARM_CHANNEL_VOC       0U
#define ALARM_CHANNEL_FORECAST  1U
#define ALARM_CHANNEL_EVENT     2U
#define ALARM_CHANNEL_COUNT     3U
#define ALARM_RESTART_CONFIRM_FRAMES 3U

typedef struct
{
    uint16_t voc_enter;
    uint16_t voc_clear;
    uint16_t up_enter;
    uint16_t up_clear;
    uint16_t confidence_min;
    uint32_t hold_ms;
    uint32_t cooldown_ms;
    uint32_t fresh_ms;
} alarm_config_t;

typedef struct
{
    uint32_t sequence;
    uint32_t uptime_ms;
    uint16_t voc_index;
    uint16_t up_score;
    uint16_t confidence;
    uint8_t ai_state;
    bool fresh;
    bool sensors_valid;
    bool ai_valid;
    uint8_t source_protocol;
    uint8_t event_state;
    uint32_t event_end_ms;
    uint16_t event_voc_x10;
    uint32_t event_episode_id;
    bool event_ready;
    /* 自网关接收本帧起经过的毫秒数。 */
    uint32_t event_rx_age_ms;
} alarm_input_t;

typedef struct
{
    uint32_t id;
    uint32_t timestamp_ms;
    uint32_t sequence;
    uint32_t uptime_ms;
    uint16_t voc_index;
    uint16_t up_score;
    uint16_t confidence;
    uint8_t channel;
    bool active;
    uint8_t source_protocol;
    uint8_t event_state;
    uint32_t event_end_ms;
    uint16_t event_voc_x10;
    uint32_t event_episode_id;
} alarm_event_t;

typedef struct
{
    bool active;
    bool candidate;
    bool raised_before;
    uint32_t candidate_ms;
    uint32_t last_raise_ms;
    uint32_t event_id;
} alarm_channel_t;

/* 当前节点会话的通知记录，通信超时不清除。 */
typedef struct
{
    uint32_t episode_id;
    uint32_t alert_id;
    uint32_t recovery_id;
    bool seen;
    bool alert_emitted;
    bool recovery_emitted;
    bool closed;
} alarm_node_history_t;

typedef struct
{
    alarm_config_t config;
    alarm_channel_t channels[ALARM_CHANNEL_COUNT];
    alarm_input_t latest;
    alarm_input_t restart_latest;
    uint32_t restart_last_ms;
    uint8_t restart_frames;
    uint32_t last_new_ms;
    uint32_t next_id;
    bool initialized;
    bool have_input;
    bool input_valid;
    alarm_node_history_t node_history;
} alarm_state_t;

/* 初始化告警；阈值无效时返回 false，告警保持关闭。 */
bool Alarm_Init(alarm_state_t *state, const alarm_config_t *config);
/**
 * @brief 处理新帧并返回是否产生通知，时间使用可回绕的单调毫秒值。
 * AQ2 每个事件最多通知一次告警、一次恢复；恢复需要同一事件的连续 NORMAL 帧。
 * 断链不清除通知记录；网关或确认节点重启后重置。旧协议判断仅用于兼容测试。
 */
bool Alarm_Process(alarm_state_t *state, const alarm_input_t *input,
                   uint32_t now_ms, alarm_event_t *event);
/* 发送前复核通知是否仍有效；旧帧不能取消新通知。 */
bool Alarm_EventCurrent(const alarm_state_t *state, const alarm_input_t *snapshot,
                        uint32_t now_ms, const alarm_event_t *event,
                        uint32_t max_age_ms);

#endif
