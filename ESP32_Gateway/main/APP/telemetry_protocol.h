#ifndef TELEMETRY_PROTOCOL_H
#define TELEMETRY_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELEMETRY_FLAG_BME680_VALID  (1U << 0)
#define TELEMETRY_FLAG_SGP40_VALID   (1U << 1)
#define TELEMETRY_FLAG_AI_VALID      (1U << 2)
/* AQ2 第 2 位表示事件检测就绪。 */
#define TELEMETRY_FLAG_EVENT_READY   (1U << 2)
#define TELEMETRY_EVENT_FRESH_MS     15000U
#define TELEMETRY_FLAG_KNOWN_MASK    (TELEMETRY_FLAG_BME680_VALID | \
                                      TELEMETRY_FLAG_SGP40_VALID | \
                                      TELEMETRY_FLAG_AI_VALID)

typedef enum
{
    TELEMETRY_AI_WARMUP = 0,
    TELEMETRY_AI_COLLECT,
    TELEMETRY_AI_DOWN,
    TELEMETRY_AI_UP,
    TELEMETRY_AI_UNCERTAIN,
    TELEMETRY_AI_ERROR
} telemetry_ai_state_t;

typedef enum
{
    TELEMETRY_EVENT_WARMUP = 0,
    TELEMETRY_EVENT_COLLECT,
    TELEMETRY_EVENT_NORMAL,
    TELEMETRY_EVENT_SUSPECTED,
    TELEMETRY_EVENT_ALARM,
    TELEMETRY_EVENT_RECOVERY,
    TELEMETRY_EVENT_INVALID
} telemetry_event_state_t;

/* 旧协议的 2/3 表示预测下降/上升，不代表安全或污染。 */
#define TELEMETRY_AI_NORMAL  TELEMETRY_AI_DOWN
#define TELEMETRY_AI_WARNING TELEMETRY_AI_UP
#define TELEMETRY_LOW_CONFIDENCE_X1000 600U

typedef struct
{
    uint32_t sequence;
    uint32_t uptime_ms;
    uint8_t flags;
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint16_t pressure_x10;
    uint16_t sraw_voc;
    uint16_t voc_index;
    uint8_t ai_state;
    uint16_t confidence_x1000;
    uint16_t normal_x1000;  /* AQ1 下降分数。 */
    uint16_t warning_x1000; /* AQ1 上升分数。 */
    uint8_t protocol_version; /* 1 为旧预测，2 为当前事件。 */
    uint8_t event_state;
    uint32_t event_end_ms;
    uint16_t event_voc_x10;
    uint32_t event_episode_id;
    /* AQ2 可选诊断字段，不触发正式告警。 */
    uint8_t live_ai_present;
    uint8_t live_ai_state; /* 采用 TELEMETRY_EVENT_* 状态。 */
    uint16_t live_ai_score_x1000;
    uint32_t live_ai_end_ms; /* 0 表示当前无分数。 */
    int32_t live_ai_delta_x100;
} telemetry_packet_t;

typedef enum
{
    TELEMETRY_PARSE_OK = 0,
    TELEMETRY_PARSE_FORMAT_ERROR,
    TELEMETRY_PARSE_CRC_ERROR,
    TELEMETRY_PARSE_RANGE_ERROR
} telemetry_parse_result_t;

telemetry_parse_result_t telemetry_protocol_parse(const uint8_t *payload,
                                                   size_t payload_length,
                                                   telemetry_packet_t *packet);
const char *telemetry_parse_result_name(telemetry_parse_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_PROTOCOL_H */
