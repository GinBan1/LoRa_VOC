#ifndef RTOS_APP_H
#define RTOS_APP_H

#include <stdint.h>

/* Create all application threads after the FreeRTOS kernel has started. */
void RtosApp_Init(void);

/* Wake the LoRa thread from the DIO1 EXTI callback. No SPI is done in ISR. */
void RtosApp_NotifyLoRaFromISR(void);

#endif /* RTOS_APP_H */
