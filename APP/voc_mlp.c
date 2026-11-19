#include "voc_mlp.h"
#include <math.h>
#include <stddef.h>

static float clipped(float value)
{
    if (value > 8.0f) return 8.0f;
    if (value < -8.0f) return -8.0f;
    return value;
}

bool voc_mlp_predict(const voc_mlp_model *model, const float *features,
                     voc_mlp_workspace *workspace, float *score)
{
    if (!model || !features || !workspace || !score ||
        (model->input_count != 1U && model->input_count != VOC_MLP_INPUT_MAX))
        return false;
    for (uint32_t i=0U; i<model->input_count; ++i) {
        if (!isfinite(features[i]) || !isfinite(model->mean[i]) ||
            !isfinite(model->scale[i]) || model->scale[i] < 1.0e-6f)
            return false;
        float z=(features[i]-model->mean[i])/model->scale[i];
        if (!isfinite(z)) return false;
        workspace->normalized[i]=clipped(z);
    }
    for (uint32_t o=0U; o<VOC_MLP_HIDDEN1; ++o) {
        float value=model->b1[o];
        for (uint32_t i=0U; i<model->input_count; ++i)
            value+=workspace->normalized[i]*model->w1[o][i];
        if (!isfinite(value)) return false;
        workspace->hidden1[o]=value>0.0f?value:0.0f;
    }
    for (uint32_t o=0U; o<VOC_MLP_HIDDEN2; ++o) {
        float value=model->b2[o];
        for (uint32_t i=0U; i<VOC_MLP_HIDDEN1; ++i)
            value+=workspace->hidden1[i]*model->w2[o][i];
        if (!isfinite(value)) return false;
        workspace->hidden2[o]=value>0.0f?value:0.0f;
    }
    float logit=model->b3;
    for (uint32_t i=0U; i<VOC_MLP_HIDDEN2; ++i)
        logit+=workspace->hidden2[i]*model->w3[i];
    if (!isfinite(logit)) return false;
    float result;
    if (logit>=0.0f) result=1.0f/(1.0f+expf(-logit));
    else { float e=expf(logit); result=e/(1.0f+e); }
    if (!isfinite(result)) return false;
    *score=result;
    return true;
}
