#ifndef ONENET_H
#define ONENET_H

#include <stdbool.h>
#include <stdint.h>

#include "net_common.h"

extern volatile uint8_t OneNet_Ready;

/* 已配置 SSID 时启动网络任务，不等待联网。 */
void OneNet_Init(const char *wifi_ssid, const char *wifi_password);
void OneNET_Subscribe(void);
void OneNet_SendData(void);
void OneNet_ProcessLoop(void);
void OneNet_RevProFrame(const unsigned char *cmd, size_t frame_length);
#endif /* ONENET_H */
