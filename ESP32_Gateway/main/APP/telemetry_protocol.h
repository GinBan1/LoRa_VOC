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
#define TELEMETRY_FLAG_KNOWN_MASK    (TELEMETRY_FLAG_BME680_VALID | \
                                      TELEMETRY_FLAG_SGP40_VALID | \
                                      TELEMETRY_FLAG_AI_VALID)

typedef enum
{
    TELEMETRY_AI_WARMUP = 0,
    TELEMETRY_AI_COLLECT,
    TELEMETRY_AI_NORMAL,
    TELEMETRY_AI_WARNING,
    TELEMETRY_AI_UNCERTAIN,
    TELEMETRY_AI_ERROR
} telemetry_ai_state_t;

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
    uint16_t normal_x1000;
    uint16_t warning_x1000;
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
