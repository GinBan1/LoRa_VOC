#ifndef APP_ENVIRONMENT_H
#define APP_ENVIRONMENT_H

#include <stdint.h>
#include "SGP40.h"

void Environment_App_Init(void);
void Environment_App_Task(void);

/* 最近一次采样的时间戳和递增序号。 */
uint32_t Environment_App_GetLastSampleMs(void);
uint32_t Environment_App_GetSampleSequence(void);
/* 读取随最近一次采样保存的诊断值，而非算法实时状态。 */
void Environment_App_GetVocSnapshot(SGP40_VOCSnapshot_t *snapshot);

#endif /* APP_ENVIRONMENT_H */
