#ifndef  SGP40_H
#define  SGP40_H

#include <stdint.h>
// SGP40 I2C 统一设备地址 (7位地址0x59，左移1位)
#define SGP40_I2C_ADDR  (0x59 << 1)

void GasIndexAlgorithmParams_Init(void);
uint16_t SGP40_Read_RawData(float bme_temp, float bme_hum);

/* 读取 VOC 算法的运行诊断，不用于恢复持久状态。 */
typedef struct
{
    uint32_t init_count;
    uint32_t init_ms;
    uint32_t process_count;
    uint32_t estimator_initialized;
    float mean;
    float std;
    float estimator_uptime_gamma_s;
} SGP40_VOCSnapshot_t;

/* 仅应用任务调用；原始值为零或输出为空时不推进算法。 */
uint8_t SGP40_ProcessVOC(uint16_t raw, int32_t *voc_index);
void SGP40_GetSnapshot(SGP40_VOCSnapshot_t *snapshot);
const char *SGP40_GetAlgorithmVersion(void);
#endif // !SGP40_H
