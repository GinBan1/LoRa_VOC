/*
 * oled.h
 *
 *  Created on: Nov 17, 2024
 *      Author: 阿妈粽
 */

#ifndef INC_OLED_H_
#define INC_OLED_H_

#include "bsp.h"

#define OLED_I2C_ADDR (0x3C << 1)

/* 节点环境采样值；温度为摄氏度、湿度为 %RH、气压为 hPa。 */
typedef struct {
    float temperature;
    float humidity;
    float pressure;
    int32_t voc_index;
    uint16_t sgp40_raw;
    uint8_t bme680_valid;
    uint8_t sgp40_valid;
} Environment_Data_t;

extern I2C_HandleTypeDef hi2c1;

void OLED_Init(void);
void OLED_Clear(void);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void lcd_proc(void);
void oled_task(void);

#endif
