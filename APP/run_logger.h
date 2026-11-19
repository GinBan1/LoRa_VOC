#ifndef RUN_LOGGER_H
#define RUN_LOGGER_H

#include <stdint.h>
#include "data_logger.h"
#include "lora_app.h"

#ifndef RUN_LOGGER_ENABLE
#define RUN_LOGGER_ENABLE 1U
#endif
#define RUN_LOGGER_ACTIVE (RUN_LOGGER_ENABLE && !DATA_LOGGER_COLLECTION_MODE && !LORA_APP_LINK_TEST_MODE)

/* 仅应用任务生成记录；日志任务独占 USART1，不从中断调用生产接口。 */
int RunLogger_App_Init(void);
void RunLogger_App_Task(void);
void RunLogger_App_Boot(uint32_t init_ms);
void RunLogger_App_Raw(uint32_t scheduled_ms);
/* 故障码：5～7 各任务栈不足；8 无线故障原因；9 未处理无线故障累计次数。 */
void RunLogger_App_Fault(uint32_t code, int32_t detail);
/* 无线任务只登记故障，由应用任务入队。原因 1～8 依次为初始化、休眠、编码、发射命令、IRQ 读取、无线超时、意外 IRQ、软件超时。 */
void RunLogger_App_RadioFault(uint32_t reason);
#endif
