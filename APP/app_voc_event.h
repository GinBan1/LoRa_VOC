#ifndef APP_VOC_EVENT_H
#define APP_VOC_EVENT_H
#include <stdint.h>
#include "voc_event_detector.h"
#ifndef APP_VOC_EVENT_ENABLE
#define APP_VOC_EVENT_ENABLE 1U
#endif
#ifndef APP_VOC_EVENT_ADAPTIVE
#define APP_VOC_EVENT_ADAPTIVE 0U
#endif
/* 将 1 Hz 采样聚合为十秒检测窗口，日志与事件服务共用同一份结果。 */
#define APP_VOC_REASON_WARMUP 1U
#define APP_VOC_REASON_INCOMPLETE 2U
#define APP_VOC_REASON_CLOCK 4U
#define APP_VOC_REASON_REINIT 8U
typedef struct {
    uint32_t sequence,sample_ms,scheduled_ms,logged_ms;
    uint32_t voc_init_count,voc_init_ms,voc_process_count;
    float voc;
    uint8_t quality;
} AppVocInput;
typedef struct {
    uint32_t start_ms,end_ms,valid_slots,reason_flags;
    float voc_mean;
    uint8_t accepted,warmup_ready,mode;
    VocOutput detector;
} AppVocDiagnostic;
void AppVocEvent_Init(void);
/* 重置时丢弃未完成窗口，并在下一条诊断记录原因。 */
void AppVocEvent_Reset(uint32_t reason_flags);
/* 仅应用任务调用；按左闭右开的十秒窗统计不重复的秒槽，每次最多输出一窗。 */
int AppVocEvent_Feed(const AppVocInput *,AppVocDiagnostic *);
#endif
