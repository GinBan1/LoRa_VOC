#include "ei_wrapper.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include <cstring>

static const char required_fusion_string[] =
    "temperature + humidity + pressure + sraw_voc";
static const char normal_label[] = "normal";
static const char warning_label[] = "warning";
static const float *model_window;
static ei_wrapper_status_t model_contract_status =
    EI_WRAPPER_INVALID_ARGUMENT;
static int model_last_inference_error;

static int get_window_data(size_t offset, size_t length, float *out_ptr)
{
    if ((model_window == nullptr) || (out_ptr == nullptr) ||
        ((offset + length) > EI_WRAPPER_WINDOW_SIZE))
    {
        return -1;
    }

    for (size_t i = 0; i < length; i++)
    {
        out_ptr[i] = model_window[offset + i];
    }
    return 0;
}

extern "C" ei_wrapper_status_t ei_model_init(void)
{
    const ei_impulse_t *model = ei_default_impulse.impulse;
    bool has_normal_label = false;
    bool has_warning_label = false;

    model_last_inference_error = 0;
    if (model == nullptr)
    {
        model_contract_status = EI_WRAPPER_INVALID_ARGUMENT;
    }
    else if (model->raw_sample_count != EI_WRAPPER_SAMPLE_COUNT)
    {
        model_contract_status = EI_WRAPPER_CONTRACT_SAMPLE_COUNT;
    }
    else if (model->raw_samples_per_frame != EI_WRAPPER_VALUES_PER_SAMPLE)
    {
        model_contract_status = EI_WRAPPER_CONTRACT_FRAME_SIZE;
    }
    else if (model->interval_ms != 1000.0f)
    {
        model_contract_status = EI_WRAPPER_CONTRACT_INTERVAL;
    }
    else if ((model->fusion_string == nullptr) ||
             (std::strcmp(model->fusion_string, required_fusion_string) != 0))
    {
        model_contract_status = EI_WRAPPER_CONTRACT_FUSION;
    }
    else if ((model->results_type != EI_CLASSIFIER_TYPE_CLASSIFICATION) ||
             (model->label_count != 2U) ||
             (model->categories == nullptr))
    {
        model_contract_status = EI_WRAPPER_CONTRACT_OUTPUT;
    }
    else
    {
        for (size_t i = 0; i < model->label_count; i++)
        {
            if (std::strcmp(model->categories[i], normal_label) == 0)
            {
                has_normal_label = true;
            }
            else if (std::strcmp(model->categories[i], warning_label) == 0)
            {
                has_warning_label = true;
            }
        }

        model_contract_status = (has_normal_label && has_warning_label) ?
            EI_WRAPPER_OK : EI_WRAPPER_CONTRACT_OUTPUT;
    }

    return model_contract_status;
}

extern "C" ei_wrapper_status_t ei_predict_air_quality(
    const float *window,
    size_t window_length,
    ei_wrapper_prediction_t *prediction)
{
    signal_t signal;
    ei_impulse_result_t result = {0};
    EI_IMPULSE_ERROR inference_status;

    if ((window == nullptr) || (prediction == nullptr) ||
        (window_length != EI_WRAPPER_WINDOW_SIZE))
    {
        return EI_WRAPPER_INVALID_ARGUMENT;
    }
    if (model_contract_status != EI_WRAPPER_OK)
    {
        return model_contract_status;
    }

    model_window = window;
    signal.total_length = EI_WRAPPER_WINDOW_SIZE;
    signal.get_data = &get_window_data;
    inference_status = run_classifier(&signal, &result, false);
    model_window = nullptr;

    if (inference_status != EI_IMPULSE_OK)
    {
        model_last_inference_error = static_cast<int>(inference_status);
        return EI_WRAPPER_INFERENCE_ERROR;
    }

    prediction->normal_score = 0.0f;
    prediction->warning_score = 0.0f;
    prediction->confidence = 0.0f;
    prediction->predicted_class = EI_WRAPPER_CLASS_UNCERTAIN;
    prediction->dsp_ms = result.timing.dsp;
    prediction->classification_ms = result.timing.classification;

    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
    {
        if (std::strcmp(result.classification[i].label, normal_label) == 0)
        {
            prediction->normal_score = result.classification[i].value;
        }
        else if (std::strcmp(result.classification[i].label,
                             warning_label) == 0)
        {
            prediction->warning_score = result.classification[i].value;
        }
    }

    if (prediction->warning_score >= prediction->normal_score)
    {
        prediction->confidence = prediction->warning_score;
        if (prediction->confidence >= EI_WRAPPER_CONFIDENCE_THRESHOLD)
        {
            prediction->predicted_class = EI_WRAPPER_CLASS_WARNING;
        }
    }
    else
    {
        prediction->confidence = prediction->normal_score;
        if (prediction->confidence >= EI_WRAPPER_CONFIDENCE_THRESHOLD)
        {
            prediction->predicted_class = EI_WRAPPER_CLASS_NORMAL;
        }
    }

    return EI_WRAPPER_OK;
}

extern "C" const char *ei_wrapper_status_string(ei_wrapper_status_t status)
{
    switch (status)
    {
        case EI_WRAPPER_OK:
            return "OK";
        case EI_WRAPPER_INVALID_ARGUMENT:
            return "invalid argument or missing model";
        case EI_WRAPPER_CONTRACT_SAMPLE_COUNT:
            return "model raw_sample_count is not 30";
        case EI_WRAPPER_CONTRACT_FRAME_SIZE:
            return "model raw_samples_per_frame is not 4";
        case EI_WRAPPER_CONTRACT_INTERVAL:
            return "model interval_ms is not 1000";
        case EI_WRAPPER_CONTRACT_FUSION:
            return "model fusion_string does not match runtime sensors";
        case EI_WRAPPER_CONTRACT_OUTPUT:
            return "model output is not normal/warning classification";
        case EI_WRAPPER_INFERENCE_ERROR:
            return "run_classifier failed";
        default:
            return "unknown TinyML status";
    }
}

extern "C" const char *ei_wrapper_class_string(
    ei_wrapper_class_t predicted_class)
{
    switch (predicted_class)
    {
        case EI_WRAPPER_CLASS_NORMAL:
            return "normal";
        case EI_WRAPPER_CLASS_WARNING:
            return "warning";
        case EI_WRAPPER_CLASS_UNCERTAIN:
        default:
            return "uncertain";
    }
}

extern "C" const char *ei_model_exported_fusion_string(void)
{
    const ei_impulse_t *model = ei_default_impulse.impulse;

    if ((model == nullptr) || (model->fusion_string == nullptr))
    {
        return "(missing)";
    }
    return model->fusion_string;
}

extern "C" int ei_model_last_inference_error(void)
{
    return model_last_inference_error;
}
