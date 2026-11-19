#ifndef APP_VOC_SERVICE_H
#define APP_VOC_SERVICE_H

#include <stdint.h>
#include "app_voc_event.h"
#include "app_voc_mlp_shadow.h"

#ifndef APP_LIVE_AI_ENABLE
#define APP_LIVE_AI_ENABLE 0U
#endif

typedef enum
{
    APP_VOC_STATUS_WARMUP = 0,
    APP_VOC_STATUS_COLLECT = 1,
    APP_VOC_STATUS_NORMAL = 2,
    APP_VOC_STATUS_SUSPECTED = 3,
    APP_VOC_STATUS_ALARM = 4,
    APP_VOC_STATUS_RECOVERY = 5,
    APP_VOC_STATUS_INVALID = 6
} AppVocStatus;

typedef struct
{
    uint8_t state;
    uint8_t ready;
    uint32_t event_end_ms;
    float voc_mean;
    uint32_t episode_id;
    uint32_t warmup_seconds;
    uint32_t bootstrap_bins;
    /* 环境数据和事件状态同批发布。 */
    uint32_t sample_ms;
    uint32_t sample_sequence;
    float temperature, humidity, pressure;
    uint16_t sraw_voc;
    int32_t voc_index;
    uint8_t bme_valid, sgp_valid;
#if APP_LIVE_AI_ENABLE
    /* 仅供诊断，不参与正式规则状态机。 */
    uint8_t ai_state;
    uint32_t ai_end_ms;
    float ai_score, ai_delta;
#endif
} AppVocServiceSnapshot;

typedef struct
{
    float score, voc_mean;
    uint8_t state, ready;
} AppVocAiView;

/* 应用任务独立更新检测；无线任务短暂屏蔽中断以复制快照，不等待生产者。 */
void voc_svc_init(void);
void voc_svc_step(uint32_t scheduled_ms);
void voc_svc_read(AppVocServiceSnapshot *out, uint32_t now_ms);
/* 应用任务读取持久显示数据，不消耗日志。 */
void voc_svc_ai_view(AppVocAiView *out, uint32_t now_ms);

/* 应用任务只读一次本窗诊断；下次更新后旧记录作废。 */
int voc_svc_take_evt(AppVocDiagnostic *out);
int voc_svc_input(AppVocInput *out);
/* 一次性模型诊断，不改变网关告警判定。 */
int voc_svc_take_ai(AppVocMlpShadowDiagnostic *out);

#endif
