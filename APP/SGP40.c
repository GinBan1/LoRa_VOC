#include "SGP40.h"

GasIndexAlgorithmParams voc_params; // 定义算法结构体

// 盛思锐官方的 CRC8 校验算法 (多项式: 0x31, 初始值: 0xFF)
uint8_t SGP40_checkCRC(uint8_t *data)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < 2; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; --bit)
        {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else
				crc = (crc << 1);
        }
    }
    return crc;
}

// 读取原始数据
uint16_t SGP40_Read_RawData(float bme_temp, float bme_hum)
{
    uint8_t cmd[8] = {0x26, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // 读取原始数据的命令，动态传入BME680温湿度
    uint8_t rx_buf[3] = {0}; // 接收缓冲区，包含2字节原始数据和1字节CRC校验
    uint16_t raw_data = 0;
	//将 BME680 的浮点数转化为 SGP40 需要的格式
    // 限制湿度范围 0~100
    if(bme_hum < 0.0f) bme_hum = 0.0f;
    if(bme_hum > 100.0f) bme_hum = 100.0f;
    uint16_t rh_ticks = (uint16_t)((bme_hum * 65535.0f / 100.0f) + 0.5f);

    //限制温度范围 -45~130
    if(bme_temp < -45.0f) bme_temp = -45.0f;
    if(bme_temp > 130.0f) bme_temp = 130.0f;
    uint16_t t_ticks = (uint16_t)(((bme_temp + 45.0f) * 65535.0f / 175.0f) + 0.5f);

    //组装湿度和湿度的 CRC
    cmd[2] = (uint8_t)(rh_ticks >> 8);
    cmd[3] = (uint8_t)(rh_ticks & 0xFF);
    cmd[4] = SGP40_checkCRC(&cmd[2]); // 动态计算湿度的 CRC 校验码

    //组装温度和温度的 CRC
    cmd[5] = (uint8_t)(t_ticks >> 8);
    cmd[6] = (uint8_t)(t_ticks & 0xFF);
    cmd[7] = SGP40_checkCRC(&cmd[5]); // 动态计算温度的 CRC 校验码
    // 发送命令
    if (HAL_I2C_Master_Transmit(&hi2c1, SGP40_I2C_ADDR, cmd, 8, 200) != HAL_OK)
    {
		 printf("发送读取命令失败\n");
         return 0;//发送失败
    }
    HAL_Delay(50); // 等待传感器测量，手册规定大于30ms，多留余量
    // 如果I2C外设不在READY状态，先等它就绪再读
    uint32_t timeout = HAL_GetTick() + 100;
    while(hi2c1.State != HAL_I2C_STATE_READY)
    {
        if(HAL_GetTick() > timeout) break;
    }
    //读取数据
    if (HAL_I2C_Master_Receive(&hi2c1, SGP40_I2C_ADDR, rx_buf, 3, 200) == HAL_OK)
    {
        // 验证CRC校验
        if (SGP40_checkCRC(rx_buf) == rx_buf[2])
        {
            raw_data = (rx_buf[0] << 8) | rx_buf[1];// 合并16位原始数据
            return raw_data;
        }
		else
        {
            // 如果走到这里，说明通信成了，但是返回的数据CRC不对，说明总线有毛刺波形变形
            printf("【通信成功但校验失败】收到数据: 0x%02X 0x%02X, 它的CRC是: 0x%02X, 传感器期望的CRC是: 0x%02X\n",
                   rx_buf[0], rx_buf[1], SGP40_checkCRC(rx_buf), rx_buf[2]);
        }
    }
	else
    {
        printf("【硬件报错】命令发成了，但是读取 3 字节回包时超时拒接！\n");
    }
    return 0;//读取失败或CRC校验失败
}
void GasIndexAlgorithmParams_Init(void)
{
    GasIndexAlgorithm_init(&voc_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC); // 初始化算法参数
}
