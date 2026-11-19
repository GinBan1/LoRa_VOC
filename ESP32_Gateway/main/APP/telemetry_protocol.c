#include "telemetry_protocol.h"

#include <stdbool.h>
#include <string.h>

#define TELEMETRY_FIELD_COUNT   13U
#define TELEMETRY_LIVE_COUNT    17U
#define TELEMETRY_BODY_MAX      192U

static uint16_t crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;

    for (size_t i = 0U; i < length; ++i)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            crc = ((crc & 0x8000U) != 0U)
                ? (uint16_t)((crc << 1) ^ 0x1021U)
                : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static bool parse_hex_digit(char character, uint8_t *value)
{
    if ((character >= '0') && (character <= '9'))
    {
        *value = (uint8_t)(character - '0');
        return true;
    }
    if ((character >= 'A') && (character <= 'F'))
    {
        *value = (uint8_t)(character - 'A' + 10);
        return true;
    }
    return false;
}

static bool parse_u32(const char *text, uint32_t maximum, uint32_t *value)
{
    uint32_t accumulator = 0U;

    if ((text == NULL) || (*text == '\0'))
    {
        return false;
    }
    while (*text != '\0')
    {
        uint32_t digit;
        if ((*text < '0') || (*text > '9'))
        {
            return false;
        }
        digit = (uint32_t)(*text - '0');
        if ((digit > maximum) || (accumulator > ((maximum - digit) / 10U)))
        {
            return false;
        }
        accumulator = accumulator * 10U + digit;
        ++text;
    }
    *value = accumulator;
    return true;
}

static bool parse_i32(const char *text,
                      int32_t minimum,
                      int32_t maximum,
                      int32_t *value)
{
    bool negative = false;
    uint32_t magnitude;
    uint32_t limit;

    if ((text == NULL) || (*text == '\0'))
    {
        return false;
    }
    if (*text == '-')
    {
        negative = true;
        ++text;
        if (*text == '\0')
        {
            return false;
        }
    }

    limit = negative ? (uint32_t)(-(int64_t)minimum) : (uint32_t)maximum;
    if (!parse_u32(text, limit, &magnitude))
    {
        return false;
    }

    *value = negative ? -(int32_t)magnitude : (int32_t)magnitude;
    return ((*value >= minimum) && (*value <= maximum));
}

telemetry_parse_result_t telemetry_protocol_parse(const uint8_t *payload,
                                                   size_t payload_length,
                                                   telemetry_packet_t *packet)
{
    char body[TELEMETRY_BODY_MAX];
    char *fields[TELEMETRY_LIVE_COUNT];
    const uint8_t *separator;
    size_t body_length;
    size_t field_count = 1U;
    uint16_t received_crc = 0U;
    uint32_t values[12];
    int32_t temperature;
    bool is_event;
    telemetry_packet_t parsed = {0};

    if ((payload == NULL) || (packet == NULL) || (payload_length == 0U))
    {
        return TELEMETRY_PARSE_FORMAT_ERROR;
    }

    separator = memchr(payload, '*', payload_length);
    if (separator == NULL)
    {
        return TELEMETRY_PARSE_FORMAT_ERROR;
    }
    body_length = (size_t)(separator - payload);
    if ((body_length == 0U) || (body_length >= sizeof(body)) ||
        ((payload_length - body_length - 1U) != 4U))
    {
        return TELEMETRY_PARSE_FORMAT_ERROR;
    }

    for (size_t i = 0U; i < 4U; ++i)
    {
        uint8_t nibble;
        if (!parse_hex_digit((char)separator[1U + i], &nibble))
        {
            return TELEMETRY_PARSE_FORMAT_ERROR;
        }
        received_crc = (uint16_t)((received_crc << 4) | nibble);
    }
    if (crc16_ccitt_false(payload, body_length) != received_crc)
    {
        return TELEMETRY_PARSE_CRC_ERROR;
    }

    for (size_t i = 0U; i < body_length; ++i)
    {
        if ((payload[i] < 0x20U) || (payload[i] > 0x7EU))
        {
            return TELEMETRY_PARSE_FORMAT_ERROR;
        }
    }
    memcpy(body, payload, body_length);
    body[body_length] = '\0';

    fields[0] = body;
    for (size_t i = 0U; i < body_length; ++i)
    {
        if (body[i] == ',')
        {
            body[i] = '\0';
            if ((field_count >= TELEMETRY_LIVE_COUNT) ||
                (body[i + 1U] == '\0'))
            {
                return TELEMETRY_PARSE_FORMAT_ERROR;
            }
            fields[field_count++] = &body[i + 1U];
        }
    }
    is_event = strcmp(fields[0], "AQ2") == 0;
    if ((field_count != TELEMETRY_FIELD_COUNT &&
         !(is_event && field_count == TELEMETRY_LIVE_COUNT)) ||
        (!is_event && strcmp(fields[0], "AQ1") != 0))
    {
        return TELEMETRY_PARSE_FORMAT_ERROR;
    }
    if (!parse_u32(fields[1], UINT32_MAX, &values[0]) ||
        !parse_u32(fields[2], UINT32_MAX, &values[1]) ||
        !parse_u32(fields[3], TELEMETRY_FLAG_KNOWN_MASK, &values[2]) ||
        !parse_i32(fields[4], -400, 850, &temperature) ||
        !parse_u32(fields[5], 1000U, &values[4]) ||
        !parse_u32(fields[6], 11000U, &values[5]) ||
        !parse_u32(fields[7], 65535U, &values[6]) ||
        !parse_u32(fields[8], 500U, &values[7]) ||
        !parse_u32(fields[9], is_event ? TELEMETRY_EVENT_INVALID : TELEMETRY_AI_ERROR, &values[8]) ||
        !parse_u32(fields[10], is_event ? UINT32_MAX : 1000U, &values[9]) ||
        !parse_u32(fields[11], is_event ? 5000U : 1000U, &values[10]) ||
        !parse_u32(fields[12], is_event ? UINT32_MAX : 1000U, &values[11]))
    {
        return TELEMETRY_PARSE_RANGE_ERROR;
    }
    if (((values[2] & TELEMETRY_FLAG_BME680_VALID) != 0U) &&
        (values[5] < 3000U))
    {
        return TELEMETRY_PARSE_RANGE_ERROR;
    }

    if (is_event)
    {
        bool ready = (values[2] & TELEMETRY_FLAG_EVENT_READY) != 0U;
        bool ready_state = values[8] >= TELEMETRY_EVENT_NORMAL && values[8] <= TELEMETRY_EVENT_RECOVERY;
        uint32_t age = values[1] - values[9];
        if (ready != ready_state ||
            (values[9] != 0U && age > INT32_MAX) ||
            (ready && (age > TELEMETRY_EVENT_FRESH_MS || values[10] == 0U || values[7] == 0U ||
                       values[6] == 0U || (values[2] & 3U) != 3U)) ||
            ((values[8] == TELEMETRY_EVENT_ALARM || values[8] == TELEMETRY_EVENT_RECOVERY) && values[11] == 0U))
            return TELEMETRY_PARSE_RANGE_ERROR;
        parsed.protocol_version = 2U;
        parsed.event_state = (uint8_t)values[8];
        parsed.event_end_ms = values[9];
        parsed.event_voc_x10 = (uint16_t)values[10];
        parsed.event_episode_id = values[11];
        if (field_count == TELEMETRY_LIVE_COUNT)
        {
            uint32_t end, state, score;
            int32_t delta;
            bool ai_ready;
            if (!parse_u32(fields[13], UINT32_MAX, &end) ||
                !parse_u32(fields[14], TELEMETRY_EVENT_INVALID, &state) ||
                !parse_u32(fields[15], 1000U, &score) ||
                !parse_i32(fields[16], -49900, 49900, &delta))
                return TELEMETRY_PARSE_RANGE_ERROR;
            ai_ready = state >= TELEMETRY_EVENT_NORMAL && state <= TELEMETRY_EVENT_RECOVERY;
            const uint32_t sensor_flags = TELEMETRY_FLAG_BME680_VALID | TELEMETRY_FLAG_SGP40_VALID;
            if ((ai_ready && (end == 0U || end != parsed.event_end_ms ||
                              (values[1] - end) > TELEMETRY_EVENT_FRESH_MS ||
                              (values[2] & sensor_flags) != sensor_flags ||
                              values[6] == 0U || values[7] == 0U)) ||
                (!ai_ready && (end != 0U || score != 0U || delta != 0)))
                return TELEMETRY_PARSE_RANGE_ERROR;
            parsed.live_ai_present = 1U;
            parsed.live_ai_end_ms = end;
            parsed.live_ai_state = (uint8_t)state;
            parsed.live_ai_score_x1000 = (uint16_t)score;
            parsed.live_ai_delta_x100 = delta;
        }
    }
    else
    {
        parsed.protocol_version = 1U;
        parsed.ai_state = (uint8_t)values[8];
        parsed.confidence_x1000 = (uint16_t)values[9];
        /* AQ1 的预测分数不写入当前事件字段。 */
        parsed.normal_x1000 = (uint16_t)values[10];
        parsed.warning_x1000 = (uint16_t)values[11];
    }
    parsed.sequence = values[0];
    parsed.uptime_ms = values[1];
    parsed.flags = (uint8_t)values[2];
    parsed.temperature_x10 = (int16_t)temperature;
    parsed.humidity_x10 = (uint16_t)values[4];
    parsed.pressure_x10 = (uint16_t)values[5];
    parsed.sraw_voc = (uint16_t)values[6];
    parsed.voc_index = (uint16_t)values[7];
    *packet = parsed;
    return TELEMETRY_PARSE_OK;
}

const char *telemetry_parse_result_name(telemetry_parse_result_t result)
{
    switch (result)
    {
        case TELEMETRY_PARSE_OK:           return "ok";
        case TELEMETRY_PARSE_FORMAT_ERROR: return "format";
        case TELEMETRY_PARSE_CRC_ERROR:    return "crc";
        case TELEMETRY_PARSE_RANGE_ERROR:  return "range";
        default:                           return "unknown";
    }
}
