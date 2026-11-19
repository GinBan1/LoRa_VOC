#ifndef RTOS_APP_H
#define RTOS_APP_H

#include <stdint.h>

/* 调度器启动后创建应用任务。 */
void RtosApp_Init(void);

/* DIO1 中断只唤醒无线任务，不访问 SPI。 */
void RtosApp_NotifyLoRaFromISR(void);

#if defined(APP_STOP_ENABLE) && APP_STOP_ENABLE
typedef struct
{
    uint32_t state, entries, rtc_wakes, early_wakes, bypasses;
    uint32_t elapsed_ms, deadline_overruns, fault_reason;
} app_stop_diag_t;

/* 仅任务上下文可读；计数由空闲任务更新。 */
void app_stop_get_diag(app_stop_diag_t *diag);
#endif

#endif /* RTOS_APP_H */
