#ifndef ONENET_H
#define ONENET_H

#include <stdbool.h>
#include <stdint.h>

#include "net_common.h"

extern volatile uint8_t OneNet_Ready;

/* Starts the dedicated WiFi/OneNET task; returns immediately. */
void OneNet_Init(const char *wifi_ssid, const char *wifi_password);
void OneNET_Subscribe(void);
void OneNet_SendData(void);
void OneNet_ProcessLoop(void);
void OneNet_RevPro(unsigned char *cmd);

#endif /* ONENET_H */
