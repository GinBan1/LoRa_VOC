#include "telemetry_protocol.h"

#include <stdbool.h>
#include <string.h>

#define TELEMETRY_FIELD_COUNT   13U
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
        if (accumulator > ((maximum - digit) / 10U))
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
    char *fields[TELEMETRY_FIELD_COUNT];
    const uint8_t *separator;
    size_t body_length;
    size_t field_count = 1U;
    uint16_t received_crc = 0U;
    uint32_t values[12];
    int32_t temperature;

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
            if ((field_count >= TELEMETRY_FIELD_COUNT) ||
                (body[i + 1U] == '\0'))
            {
                return TELEMETRY_PARSE_FORMAT_ERROR;
            }
            fields[field_count++] = &body[i + 1U];
        }
    }
    if ((field_count != TELEMETRY_FIELD_COUNT) ||
        (strcmp(fields[0], "AQ1") != 0))
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
        !parse_u32(fields[9], TELEMETRY_AI_ERROR, &values[8]) ||
        !parse_u32(fields[10], 1000U, &values[9]) ||
        !parse_u32(fields[11], 1000U, &values[10]) ||
        !parse_u32(fields[12], 1000U, &values[11]))
    {
        return TELEMETRY_PARSE_RANGE_ERROR;
    }
    if (((values[2] & TELEMETRY_FLAG_BME680_VALID) != 0U) &&
        (values[5] < 3000U))
    {
        return TELEMETRY_PARSE_RANGE_ERROR;
    }

    packet->sequence = values[0];
    packet->uptime_ms = values[1];
    packet->flags = (uint8_t)values[2];
    packet->temperature_x10 = (int16_t)temperature;
    packet->humidity_x10 = (uint16_t)values[4];
    packet->pressure_x10 = (uint16_t)values[5];
    packet->sraw_voc = (uint16_t)values[6];
    packet->voc_index = (uint16_t)values[7];
    packet->ai_state = (uint8_t)values[8];
    packet->confidence_x1000 = (uint16_t)values[9];
    packet->normal_x1000 = (uint16_t)values[10];
    packet->warning_x1000 = (uint16_t)values[11];
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
