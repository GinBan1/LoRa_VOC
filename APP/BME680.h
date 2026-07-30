#ifndef BME680_H
#define BME680_H

#include "bsp.h"

// BME680 7位地址 0x76，HAL库需要左移1位,SDO接GND时地址为0x76，接VCC时地址为0x77
#define BME680_I2C_ADDR  (0x76 << 1)

struct bme68x_data; 
extern struct bme68x_dev bme_dev;

int8_t BME680_Hardware_Init(void);
int8_t BME680_Read_Temp_Hum_Press(struct bme68x_data *data);

#endif // BME680_H