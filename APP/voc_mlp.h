#ifndef VOC_MLP_H
#define VOC_MLP_H

#include <stdbool.h>
#include <stdint.h>

#define VOC_MLP_INPUT_MAX 12U
#define VOC_MLP_HIDDEN1 16U
#define VOC_MLP_HIDDEN2 8U

/* 固定容量的 MLP 推理结构。得分不参与正式规则告警。 */
typedef struct {
    uint32_t input_count;
    float mean[VOC_MLP_INPUT_MAX];
    float scale[VOC_MLP_INPUT_MAX];
    float w1[VOC_MLP_HIDDEN1][VOC_MLP_INPUT_MAX];
    float b1[VOC_MLP_HIDDEN1];
    float w2[VOC_MLP_HIDDEN2][VOC_MLP_HIDDEN1];
    float b2[VOC_MLP_HIDDEN2];
    float w3[VOC_MLP_HIDDEN2];
    float b3;
} voc_mlp_model;

typedef struct {
    float normalized[VOC_MLP_INPUT_MAX];
    float hidden1[VOC_MLP_HIDDEN1];
    float hidden2[VOC_MLP_HIDDEN2];
} voc_mlp_workspace;

/* 工作区由调用者提供；参数无效时返回 false，原得分不变。 */
bool voc_mlp_predict(const voc_mlp_model *model, const float *features,
                     voc_mlp_workspace *workspace, float *score);

#endif
