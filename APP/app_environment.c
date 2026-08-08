#include "app_environment.h"

#include "BME680.h"
#include "SGP40.h"
#include "bme68x.h"
#include "data_logger.h"
#include "main.h"
#include "sensirion_gas_index_algorithm.h"

#include <stdio.h>

extern Environment_Data_t g_env_data;
extern GasIndexAlgorithmParams voc_params;

static void environment_read_sensors(void)
{
    struct bme68x_data bme_data;
    float temperature = 25.0f;
    float humidity = 50.0f;
    uint16_t sgp40_raw;

    if (BME680_Read_Temp_Hum_Press(&bme_data) == BME68X_OK)
    {
        g_env_data.temperature = bme_data.temperature;
        g_env_data.humidity = bme_data.humidity;
        g_env_data.pressure = bme_data.pressure / 100.0f;
        g_env_data.bme680_valid = 1U;
        temperature = bme_data.temperature;
        humidity = bme_data.humidity;
    }
    else
    {
        g_env_data.bme680_valid = 0U;
#if !DATA_LOGGER_COLLECTION_MODE
        printf("#ERROR,BME680_read_failed,using_default_SGP40_compensation\r\n");
#endif
    }

    sgp40_raw = SGP40_Read_RawData(temperature, humidity);
    if (sgp40_raw != 0U)
    {
        int32_t voc_index = 0;
        GasIndexAlgorithm_process(&voc_params, sgp40_raw, &voc_index);
        g_env_data.voc_index = voc_index;
        g_env_data.sgp40_raw = sgp40_raw;
        g_env_data.sgp40_valid = 1U;
    }
    else
    {
        g_env_data.sgp40_valid = 0U;
#if !DATA_LOGGER_COLLECTION_MODE
        printf("#ERROR,SGP40_read_failed\r\n");
#endif
    }
}

void Environment_App_Init(void)
{
    GasIndexAlgorithmParams_Init();
    BME680_Hardware_Init();
}

void Environment_App_Task(void)
{
    environment_read_sensors();
}
