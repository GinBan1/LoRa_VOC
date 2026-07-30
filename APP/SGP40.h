#ifndef  SGP40_H
#define  SGP40_H

#include "bsp.h"
// SGP40 I2C 统一设备地址 (7位地址0x59，左移1位)
#define SGP40_I2C_ADDR  (0x59 << 1)

void GasIndexAlgorithmParams_Init(void);
uint16_t SGP40_Read_RawData(float bme_temp, float bme_hum);
#endif // !SGP40_H
