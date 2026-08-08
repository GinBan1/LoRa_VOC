#ifndef EI_WRAPPER_H
#define EI_WRAPPER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 暴露给 main.c 调用的初始化与推理函数
#define EI_WRAPPER_SAMPLE_COUNT       30U
#define EI_WRAPPER_VALUES_PER_SAMPLE  4U
#define EI_WRAPPER_WINDOW_SIZE        120U
#define EI_WRAPPER_CONFIDENCE_THRESHOLD  0.50f

typedef enum
{
    EI_WRAPPER_OK = 0,
    EI_WRAPPER_INVALID_ARGUMENT,
    EI_WRAPPER_CONTRACT_SAMPLE_COUNT,
    EI_WRAPPER_CONTRACT_FRAME_SIZE,
    EI_WRAPPER_CONTRACT_INTERVAL,
    EI_WRAPPER_CONTRACT_FUSION,
    EI_WRAPPER_CONTRACT_OUTPUT,
    EI_WRAPPER_INFERENCE_ERROR
} ei_wrapper_status_t;

typedef enum
{
    EI_WRAPPER_CLASS_UNCERTAIN = 0,
    EI_WRAPPER_CLASS_NORMAL,
    EI_WRAPPER_CLASS_WARNING
} ei_wrapper_class_t;

typedef struct
{
    float normal_score;
    float warning_score;
    float confidence;
    ei_wrapper_class_t predicted_class;
    int32_t dsp_ms;
    int32_t classification_ms;
} ei_wrapper_prediction_t;

ei_wrapper_status_t ei_model_init(void);
ei_wrapper_status_t ei_predict_air_quality(
    const float *window,
    size_t window_length,
    ei_wrapper_prediction_t *prediction);

const char *ei_wrapper_status_string(ei_wrapper_status_t status);
const char *ei_wrapper_class_string(ei_wrapper_class_t predicted_class);
const char *ei_model_exported_fusion_string(void);
int ei_model_last_inference_error(void);

#ifdef __cplusplus
}
#endif

#endif
