#ifndef APP_VOC_MLP_SHADOW_H
#define APP_VOC_MLP_SHADOW_H

#include "app_voc_event.h"
#include <stdint.h>

#ifndef APP_VOC_MLP_SHADOW_ENABLE
#define APP_VOC_MLP_SHADOW_ENABLE 1U
#endif
#ifndef APP_VOC_MLP_ADAPT_ENABLE
#define APP_VOC_MLP_ADAPT_ENABLE 1U
#endif
#if APP_VOC_MLP_ADAPT_ENABLE != 1U
#error This release only includes the adaptive VOC model
#endif
const char *AppVocMlpShadow_ModelTag(void);
#define APP_VOC_MLP_MODEL_TAG AppVocMlpShadow_ModelTag()
#define APP_VOC_MLP_FEATURE_COUNT 12U
#define APP_VOC_MLP_REASON_MISMATCH 16U
#define APP_VOC_MLP_REASON_NUMERIC 32U

/* 模型内部状态使用 VocState 的 0～3，不覆盖正式告警状态或邮件决策。 */
typedef struct {
    uint32_t start_ms, end_ms, valid_slots, reason_flags;
    uint8_t accepted, ready, state, active, alarm_started;
    float score;
    float features[APP_VOC_MLP_FEATURE_COUNT];
} AppVocMlpShadowDiagnostic;

void AppVocMlpShadow_Init(void);
void AppVocMlpShadow_Reset(uint32_t reason_flags);
/* 仅由应用任务按温度、湿度、气压、SRAW、VOC 顺序送入采样；completed 可为空，每个窗口最多输出一次。 */
int AppVocMlpShadow_Feed(const AppVocInput *input, const float axes[5],
                         const AppVocDiagnostic *completed,
                         AppVocMlpShadowDiagnostic *out);
#endif
