#include "tinyml_app.h"

#include "ei_wrapper.h"
#include "main.h"
#include "oled.h"

#include <stdio.h>
#include <string.h>

#define TINYML_WARMUP_MS  60000U

extern Environment_Data_t g_env_data;

static float tinyml_window[EI_WRAPPER_WINDOW_SIZE];
static uint32_t tinyml_sample_count;
static ei_wrapper_status_t tinyml_model_status;
static uint8_t tinyml_error_reported;
static tinyml_result_t tinyml_latest_result;

static void tinyml_append_sample(void)
{
    const uint32_t frame_size = EI_WRAPPER_VALUES_PER_SAMPLE;
    uint32_t offset;

    if (tinyml_sample_count < EI_WRAPPER_SAMPLE_COUNT)
    {
        offset = tinyml_sample_count * frame_size;
        tinyml_sample_count++;
    }
    else
    {
        memmove(&tinyml_window[0],
                &tinyml_window[frame_size],
                (EI_WRAPPER_WINDOW_SIZE - frame_size) *
                    sizeof(tinyml_window[0]));
        offset = EI_WRAPPER_WINDOW_SIZE - frame_size;
    }

    tinyml_window[offset + 0U] = g_env_data.temperature;
    tinyml_window[offset + 1U] = g_env_data.humidity;
    tinyml_window[offset + 2U] = g_env_data.pressure;
    tinyml_window[offset + 3U] = (float)g_env_data.sgp40_raw;
}

void TinyML_App_Init(void)
{
    memset(tinyml_window, 0, sizeof(tinyml_window));
    memset(&tinyml_latest_result, 0, sizeof(tinyml_latest_result));
    tinyml_sample_count = 0U;
    tinyml_error_reported = 0U;
    tinyml_model_status = ei_model_init();
    tinyml_latest_result.state = TINYML_STATE_WARMING_UP;

    if (tinyml_model_status == EI_WRAPPER_OK)
    {
        printf("[TinyML] Classification model ready: 30 s, 1 Hz, "
               "normal/warning, threshold=%.2f\r\n",
               EI_WRAPPER_CONFIDENCE_THRESHOLD);
        printf("[TinyML] Waiting 60 s for sensor warmup, then 30 s "
               "for the first window\r\n");
        printf("timestamp_ms,temperature,humidity,pressure,sraw_voc,"
               "normal_score,warning_score,confidence,state,dsp_ms,"
               "classification_ms\r\n");
    }
    else
    {
        tinyml_latest_result.state = TINYML_STATE_ERROR;
        printf("[TinyML] Disabled: %s\r\n",
               ei_wrapper_status_string(tinyml_model_status));
        if (tinyml_model_status == EI_WRAPPER_CONTRACT_FUSION)
        {
            printf("[TinyML] Expected fusion: temperature + humidity + "
                   "pressure + sraw_voc\r\n");
            printf("[TinyML] Exported fusion: %s\r\n",
                   ei_model_exported_fusion_string());
        }
        tinyml_error_reported = 1U;
    }
}

void TinyML_App_Task(void)
{
    ei_wrapper_prediction_t prediction;
    ei_wrapper_status_t inference_status;

    if (tinyml_model_status != EI_WRAPPER_OK)
    {
        return;
    }

    if (HAL_GetTick() < TINYML_WARMUP_MS)
    {
        return;
    }

    if (tinyml_latest_result.state == TINYML_STATE_WARMING_UP)
    {
        tinyml_latest_result.state = TINYML_STATE_COLLECTING_WINDOW;
    }

    if ((g_env_data.bme680_valid == 0U) ||
        (g_env_data.sgp40_valid == 0U))
    {
        return;
    }

    tinyml_append_sample();
    if (tinyml_sample_count < EI_WRAPPER_SAMPLE_COUNT)
    {
        return;
    }

    inference_status = ei_predict_air_quality(tinyml_window,
                                               EI_WRAPPER_WINDOW_SIZE,
                                               &prediction);
    if (inference_status != EI_WRAPPER_OK)
    {
        if (tinyml_error_reported == 0U)
        {
            printf("[TinyML] Inference disabled: %s (EI error=%d)\r\n",
                   ei_wrapper_status_string(inference_status),
                   ei_model_last_inference_error());
            tinyml_error_reported = 1U;
        }
        tinyml_latest_result.state = TINYML_STATE_ERROR;
        tinyml_latest_result.valid = 0U;
        return;
    }

    tinyml_latest_result.normal_score = prediction.normal_score;
    tinyml_latest_result.warning_score = prediction.warning_score;
    tinyml_latest_result.confidence = prediction.confidence;
    tinyml_latest_result.timestamp_ms = HAL_GetTick();
    tinyml_latest_result.valid = 1U;

    switch (prediction.predicted_class)
    {
        case EI_WRAPPER_CLASS_NORMAL:
            tinyml_latest_result.state = TINYML_STATE_NORMAL;
            break;
        case EI_WRAPPER_CLASS_WARNING:
            tinyml_latest_result.state = TINYML_STATE_WARNING;
            break;
        case EI_WRAPPER_CLASS_UNCERTAIN:
        default:
            tinyml_latest_result.state = TINYML_STATE_UNCERTAIN;
            break;
    }

    printf("%lu,%.1f,%.1f,%.1f,%u,%.4f,%.4f,%.4f,%s,%ld,%ld\r\n",
           (unsigned long)tinyml_latest_result.timestamp_ms,
           g_env_data.temperature,
           g_env_data.humidity,
           g_env_data.pressure,
           (unsigned int)g_env_data.sgp40_raw,
           prediction.normal_score,
           prediction.warning_score,
           prediction.confidence,
           ei_wrapper_class_string(prediction.predicted_class),
           (long)prediction.dsp_ms,
           (long)prediction.classification_ms);
}

const tinyml_result_t *TinyML_App_GetLatestResult(void)
{
    return &tinyml_latest_result;
}

const char *TinyML_App_StateString(tinyml_state_t state)
{
    switch (state)
    {
        case TINYML_STATE_WARMING_UP:
            return "warming_up";
        case TINYML_STATE_COLLECTING_WINDOW:
            return "collecting";
        case TINYML_STATE_NORMAL:
            return "normal";
        case TINYML_STATE_WARNING:
            return "warning";
        case TINYML_STATE_UNCERTAIN:
            return "uncertain";
        case TINYML_STATE_ERROR:
        default:
            return "error";
    }
}

uint32_t TinyML_App_GetWarmupRemainingSeconds(void)
{
    uint32_t now = HAL_GetTick();

    if (now >= TINYML_WARMUP_MS)
    {
        return 0U;
    }

    return (TINYML_WARMUP_MS - now + 999U) / 1000U;
}

uint32_t TinyML_App_GetWindowSamplesCollected(void)
{
    return tinyml_sample_count;
}
