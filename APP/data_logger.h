#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

/* 置 1 启用历史数据采集模式，仅运行传感器和 CSV 日志任务。 */
#ifndef DATA_LOGGER_COLLECTION_MODE
#define DATA_LOGGER_COLLECTION_MODE 0U
#endif

#define DATA_LOGGER_SCHEMA_VERSION   2U
#define DATA_LOGGER_WARMUP_MS       60000U
#define DATA_LOGGER_SAMPLE_PERIOD_MS 1000U
#define DATA_LOGGER_STATUS_PERIOD_MS 300000U
#define DATA_LOGGER_MIN_SESSION_MS  3900000U

void DataLogger_App_Init(void);
void DataLogger_App_Task(void);

#endif /* DATA_LOGGER_H */
