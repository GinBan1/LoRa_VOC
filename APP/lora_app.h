#ifndef LORA_APP_H
#define LORA_APP_H

#include <stdint.h>

#define LORA_APP_LINK_TEST_MODE  0U
#define LORA_APP_FREQUENCY_HZ   470300000UL
#define LORA_APP_TX_POWER_DBM   10
#define LORA_APP_TX_PERIOD_MS   5000UL
#define LORA_APP_TX_TIMEOUT_MS  5000UL

void Lora_App_Init(void);
void Lora_App_Task(void);
void Lora_App_Dio1Irq(void);
uint8_t Lora_App_IsReady(void);

#endif /* LORA_APP_H */
