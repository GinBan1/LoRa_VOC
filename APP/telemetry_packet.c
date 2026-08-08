#include "telemetry_packet.h"

#include "oled.h"
#include "tinyml_app.h"

#include <limits.h>
#include <stdio.h>

#define TELEMETRY_FLAG_BME_VALID  (1U << 0)
#define TELEMETRY_FLAG_SGP_VALID  (1U << 1)
#define TELEMETRY_FLAG_AI_VALID   (1U << 2)

extern Environment_Data_t g_env_data;

static int32_t telemetry_scale_round_clamp(float value,
                                            float scale,
                                            int32_t minimum,
                                            int32_t maximum)
{
    float scaled;

    /* NaN has no meaningful fixed-point representation. */
    if (value != value)
    {
        return 0;
    }

    scaled = value * scale;
    if (scaled <= (float)minimum)
    {
        return minimum;
    }
    if (scaled >= (float)maximum)
    {
        return maximum;
    }

    if (scaled >= 0.0f)
    {
        scaled += 0.5f;
    }
    else
    {
        scaled -= 0.5f;
    }

    return (int32_t)scaled;
}

static uint16_t telemetry_crc16_ccitt_false(const uint8_t *data,
                                             size_t length)
{
    uint16_t crc = 0xFFFFU;
    size_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= (uint16_t)data[index] << 8;
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

size_t TelemetryPacket_Format(char *buffer,
                              size_t buffer_size,
                              uint32_t sequence,
                              uint32_t uptime_ms)
{
    const tinyml_result_t *ai_result;
    uint8_t flags = 0U;
    uint32_t ai_state;
    int32_t temperature_x10;
    int32_t humidity_x10;
    int32_t pressure_x10;
    int32_t confidence_x1000;
    int32_t normal_x1000;
    int32_t warning_x1000;
    int body_length;
    int suffix_length;
    uint16_t crc;

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return 0U;
    }
    buffer[0] = '\0';

    ai_result = TinyML_App_GetLatestResult();
    if (ai_result == NULL)
    {
        return 0U;
    }

    if (g_env_data.bme680_valid != 0U)
    {
        flags |= TELEMETRY_FLAG_BME_VALID;
    }
    if (g_env_data.sgp40_valid != 0U)
    {
        flags |= TELEMETRY_FLAG_SGP_VALID;
    }
    if (ai_result->valid != 0U)
    {
        flags |= TELEMETRY_FLAG_AI_VALID;
    }

    temperature_x10 = telemetry_scale_round_clamp(g_env_data.temperature,
                                                    10.0f,
                                                    INT16_MIN,
                                                    INT16_MAX);
    humidity_x10 = telemetry_scale_round_clamp(g_env_data.humidity,
                                                10.0f,
                                                0,
                                                1000);
    pressure_x10 = telemetry_scale_round_clamp(g_env_data.pressure,
                                                10.0f,
                                                0,
                                                UINT16_MAX);
    confidence_x1000 = telemetry_scale_round_clamp(ai_result->confidence,
                                                    1000.0f,
                                                    0,
                                                    1000);
    normal_x1000 = telemetry_scale_round_clamp(ai_result->normal_score,
                                                1000.0f,
                                                0,
                                                1000);
    warning_x1000 = telemetry_scale_round_clamp(ai_result->warning_score,
                                                 1000.0f,
                                                 0,
                                                 1000);

    ai_state = (uint32_t)ai_result->state;
    if (ai_state > (uint32_t)TINYML_STATE_ERROR)
    {
        ai_state = (uint32_t)TINYML_STATE_ERROR;
    }

    body_length = snprintf(buffer,
                           buffer_size,
                           "AQ1,%lu,%lu,%u,%ld,%ld,%ld,%u,%ld,%lu,%ld,%ld,%ld",
                           (unsigned long)sequence,
                           (unsigned long)uptime_ms,
                           (unsigned int)flags,
                           (long)temperature_x10,
                           (long)humidity_x10,
                           (long)pressure_x10,
                           (unsigned int)g_env_data.sgp40_raw,
                           (long)g_env_data.voc_index,
                           (unsigned long)ai_state,
                           (long)confidence_x1000,
                           (long)normal_x1000,
                           (long)warning_x1000);
    if ((body_length <= 0) || ((size_t)body_length >= buffer_size))
    {
        buffer[0] = '\0';
        return 0U;
    }

    crc = telemetry_crc16_ccitt_false((const uint8_t *)buffer,
                                       (size_t)body_length);
    suffix_length = snprintf(&buffer[body_length],
                             buffer_size - (size_t)body_length,
                             "*%04X",
                             (unsigned int)crc);
    if ((suffix_length != 5) ||
        ((size_t)(body_length + suffix_length) >= buffer_size))
    {
        buffer[0] = '\0';
        return 0U;
    }

    return (size_t)(body_length + suffix_length);
}
