#include "BME680.h"

struct bme68x_dev bme_dev;
uint8_t dev_addr = (0x76 << 1); // BME680 的 8位写地址 (SDO接地)

// 1. 底层 I2C 读接口 (适配博世 API)
BME68X_INTF_RET_TYPE bme68x_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    uint8_t device_addr = *(uint8_t*)intf_ptr;
    if (HAL_I2C_Mem_Read(&hi2c1, device_addr, reg_addr, 1, reg_data, len, HAL_MAX_DELAY) == HAL_OK)
        return BME68X_OK; // 0
    return BME68X_E_COM_FAIL; // 通信失败
}

// 2. 底层 I2C 写接口 (适配博世 API)
BME68X_INTF_RET_TYPE bme68x_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    uint8_t device_addr = *(uint8_t*)intf_ptr;
    if (HAL_I2C_Mem_Write(&hi2c1, device_addr, reg_addr, 1, (uint8_t*)reg_data, len, HAL_MAX_DELAY) == HAL_OK)
        return BME68X_OK; // 0
    return BME68X_E_COM_FAIL; // 通信失败
}

// 3. 底层延时接口 (微秒级延时，用 HAL_Delay 毫秒级替代即可满足传感器要求)
void bme68x_delay_us(uint32_t period, void *intf_ptr)
{
    // 将微秒转为毫秒，向上取整确保延时足够
    uint32_t delay_ms = (period / 1000) + 1; 
    HAL_Delay(delay_ms);
}

// 4. 初始化函数
int8_t BME680_Hardware_Init(void)
{
    int8_t rslt;

    // 绑定底层接口到官方结构体
    bme_dev.read     = bme68x_i2c_read;
    bme_dev.write    = bme68x_i2c_write;
    bme_dev.delay_us = bme68x_delay_us;
    bme_dev.intf_ptr = &dev_addr;       // 传入 I2C 地址
    bme_dev.intf     = BME68X_I2C_INTF; // 指定为 I2C 接口
    bme_dev.amb_temp = 25;              // 默认环境温度 25 度

    // 调用官方初始化函数
    rslt = bme68x_init(&bme_dev);
    
    // 如果初始化成功，配置传感器参数 (过采样率等)
    if (rslt == BME68X_OK) {
        struct bme68x_conf conf;
        conf.filter = BME68X_FILTER_OFF;
        conf.odr    = BME68X_ODR_NONE;
        conf.os_hum = BME68X_OS_1X; // 湿度 1倍过采样
        conf.os_pres = BME68X_OS_16X; // 气压 16倍过采样
        conf.os_temp = BME68X_OS_2X;  // 温度 2倍过采样
        bme68x_set_conf(&conf, &bme_dev);
    }
    return rslt;
}

// 5. 触发并读取一次数据
int8_t BME680_Read_Temp_Hum_Press(struct bme68x_data *data)
{
    uint8_t n_fields;
	struct bme68x_conf conf; 
	bme68x_get_conf(&conf, &bme_dev);
    // 设为强制测量模式 (测完一次自动回到睡眠)
    bme68x_set_op_mode(BME68X_FORCED_MODE, &bme_dev);
    
    // 延时等待测量完成 (根据你配置的过采样率计算出的时间)
    uint32_t meas_dur = bme68x_get_meas_dur(BME68X_FORCED_MODE,&conf, &bme_dev);
    bme_dev.delay_us(meas_dur, bme_dev.intf_ptr);

    // 读取数据
    return bme68x_get_data(BME68X_FORCED_MODE, data, &n_fields, &bme_dev);
}
