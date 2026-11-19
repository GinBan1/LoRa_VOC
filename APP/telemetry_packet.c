#include "telemetry_packet.h"

#include "app_voc_service.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>

#define TELEMETRY_FLAG_BME_VALID  (1U << 0)
#define TELEMETRY_FLAG_SGP_VALID  (1U << 1)
#define TELEMETRY_FLAG_EVENT_READY (1U << 2)

static int32_t telemetry_scale_round_clamp(float value,
                                            float scale,
                                            int32_t minimum,
                                            int32_t maximum)
{
    float scaled;

    /* NaN 不参与定点量化。 */
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
    AppVocServiceSnapshot snapshot;
    uint8_t flags = 0U;
    uint32_t event_state;
    int32_t temperature_x10;
    int32_t humidity_x10;
    int32_t pressure_x10;
    int32_t event_voc_x10;
    int32_t voc_index;
    int body_length;
    int suffix_length;
    uint16_t crc;

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return 0U;
    }
    buffer[0] = '\0';

    voc_svc_read(&snapshot, uptime_ms);

    if (snapshot.bme_valid != 0U)
    {
        flags |= TELEMETRY_FLAG_BME_VALID;
    }
    if (snapshot.sgp_valid != 0U)
    {
        flags |= TELEMETRY_FLAG_SGP_VALID;
    }
    event_state = snapshot.state;
    if (event_state > APP_VOC_STATUS_INVALID) event_state = APP_VOC_STATUS_INVALID;
    if (snapshot.ready && event_state >= APP_VOC_STATUS_NORMAL &&
        event_state <= APP_VOC_STATUS_RECOVERY &&
        (flags & (TELEMETRY_FLAG_BME_VALID | TELEMETRY_FLAG_SGP_VALID)) ==
          (TELEMETRY_FLAG_BME_VALID | TELEMETRY_FLAG_SGP_VALID))
    {
        flags |= TELEMETRY_FLAG_EVENT_READY;
    }
    else if (event_state >= APP_VOC_STATUS_NORMAL && event_state <= APP_VOC_STATUS_RECOVERY)
        event_state = APP_VOC_STATUS_INVALID;

    temperature_x10 = telemetry_scale_round_clamp(snapshot.temperature,
                                                    10.0f,
                                                    -400,
                                                    850);
    humidity_x10 = telemetry_scale_round_clamp(snapshot.humidity,
                                                10.0f,
                                                0,
                                                1000);
    pressure_x10 = telemetry_scale_round_clamp(snapshot.pressure,
                                                10.0f,
                                                0,
                                                11000);
    event_voc_x10 = telemetry_scale_round_clamp(snapshot.voc_mean, 10.f, 0, 5000);
    voc_index = snapshot.voc_index;
    if (voc_index < 0) voc_index = 0;
    if (voc_index > 500) voc_index = 500;

    body_length = snprintf(buffer,
                           buffer_size,
                           "AQ2,%lu,%lu,%u,%ld,%ld,%ld,%u,%ld,%lu,%lu,%ld,%lu",
                           (unsigned long)sequence,
                           (unsigned long)uptime_ms,
                           (unsigned int)flags,
                           (long)temperature_x10,
                           (long)humidity_x10,
                           (long)pressure_x10,
                           (unsigned int)snapshot.sraw_voc,
                           (long)voc_index,
                           (unsigned long)event_state,
                           (unsigned long)snapshot.event_end_ms,
                           (long)event_voc_x10,
                           (unsigned long)snapshot.episode_id);
    if ((body_length <= 0) || ((size_t)body_length >= buffer_size))
    {
        buffer[0] = '\0';
        return 0U;
    }

#if APP_LIVE_AI_ENABLE
    {
        uint32_t ai_end_ms = 0U;
        uint32_t ai_state = snapshot.ai_state;
        int32_t ai_score = 0;
        int32_t ai_delta = 0;
        int ai_length;
        if (ai_state >= APP_VOC_STATUS_NORMAL && ai_state <= APP_VOC_STATUS_RECOVERY)
        {
            if ((flags & TELEMETRY_FLAG_EVENT_READY) && snapshot.ai_end_ms != 0U &&
                snapshot.ai_end_ms == snapshot.event_end_ms &&
                (uint32_t)(uptime_ms - snapshot.ai_end_ms) <= 15000U &&
                isfinite(snapshot.ai_score) && snapshot.ai_score >= 0.f &&
                snapshot.ai_score <= 1.f && isfinite(snapshot.ai_delta) &&
                snapshot.ai_delta >= -499.f && snapshot.ai_delta <= 499.f)
            {
                ai_end_ms = snapshot.ai_end_ms;
                ai_score = telemetry_scale_round_clamp(snapshot.ai_score, 1000.f, 0, 1000);
                ai_delta = telemetry_scale_round_clamp(snapshot.ai_delta, 100.f, -49900, 49900);
            }
            else ai_state = APP_VOC_STATUS_INVALID;
        }
        else if (ai_state > APP_VOC_STATUS_INVALID) ai_state = APP_VOC_STATUS_INVALID;
        ai_length = snprintf(&buffer[body_length], buffer_size - (size_t)body_length,
            ",%lu,%lu,%ld,%ld", (unsigned long)ai_end_ms, (unsigned long)ai_state,
            (long)ai_score, (long)ai_delta);
        if (ai_length <= 0 || (size_t)ai_length >= buffer_size - (size_t)body_length)
        {
            buffer[0] = '\0';
            return 0U;
        }
        body_length += ai_length;
    }
#endif

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
