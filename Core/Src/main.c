/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If this file has not been licensed with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp.h"
#include "ei_wrapper.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
extern Environment_Data_t g_env_data;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void System_Environment_Data(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
volatile uint8_t sample_flag = 0; // SysTick每1000ms置1，主循环采集标志
/**
 * @brief  一阶低通滤波函数 (EMA)
 * @param  raw_value: 当前AI预测出的原始离散值
 * @retval 平滑后的连续预测值
 */
float low_pass_filter(float raw_value) 
{
    static float filtered_value = 0.0f;
    static uint8_t is_first_run = 1;

    // 第一次调用时用真实值初始化，防止从 0 开始缓慢上升
    if (is_first_run) 
	{
        filtered_value = raw_value;
        is_first_run = 0;
    } 
	else 
	{
        // alpha 越小越平滑，响应越缓和；推荐范围 0.05 ~ 0.2
        // 在 1 秒采样一次的频率下，0.08 可以非常完美的消除 -607 / -708 的跳变阶梯
        float alpha = 0.08f; 
        filtered_value = alpha * raw_value + (1.0f - alpha) * filtered_value;
    }

    return filtered_value;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  GasIndexAlgorithmParams_Init(); // 初始化算法参数
  scheduler_init(); // 初始化调度器，计算任务数量
  BME680_Hardware_Init();
  OLED_Init();
  OLED_Clear();
  printf("timestamp_ms,temperature,humidity,pressure,sraw_voc\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    if (sample_flag)
    {
        sample_flag = 0;
        System_Environment_Data();
        oled_task();

        if (g_env_data.bme680_valid && g_env_data.sgp40_valid)
        {
            if (HAL_GetTick() >= 50000)
            {
                float temp  = g_env_data.temperature;
                float hum   = g_env_data.humidity;
                float press = g_env_data.pressure;
                float sraw  = (float)g_env_data.sgp40_raw;

                float predicted_delta = ei_predict_30m_delta(temp, hum, press, sraw);
				
                float smooth_delta = low_pass_filter(predicted_delta);
                trend_analyzer_update(smooth_delta); // 趋势分析

                printf("%lu,%.1f,%.1f,%.1f,%u,%.1f,%.2f\r\n",
                       HAL_GetTick(),
                       temp, hum, press,
                       g_env_data.sgp40_raw,
                       predicted_delta,smooth_delta);
            }
        }
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void System_Environment_Data(void)
{
    struct bme68x_data bme_data;
	extern GasIndexAlgorithmParams voc_params;

    float temp = 25.0f, hum = 50.0f; // 默认温湿度

    //BME680测出最新的温湿度
    if(BME680_Read_Temp_Hum_Press(&bme_data) == BME68X_OK)
    {
		g_env_data.temperature = bme_data.temperature;
        g_env_data.humidity = bme_data.humidity;
        g_env_data.pressure = bme_data.pressure / 100.0f;
        g_env_data.bme680_valid = 1; // 盖章：BME680数据真实有效！

        temp = bme_data.temperature;
        hum = bme_data.humidity;
    }
    else
    {
        printf("BME680 读取失败，使用默认温湿度补偿SGP40\n");
		g_env_data.bme680_valid = 0;
    }
    // SGP40独立读取，BME680失败时用默认值补偿
    uint16_t sgp40_raw = SGP40_Read_RawData(temp, hum);
    if(sgp40_raw != 0)
    {
        int32_t voc_index = 0;
        GasIndexAlgorithm_process(&voc_params, sgp40_raw, &voc_index);
        g_env_data.voc_index = voc_index;
        g_env_data.sgp40_raw = sgp40_raw;
        g_env_data.sgp40_valid = 1;
    }

    else
    {
        printf("SGP40 读取失败\n");
		g_env_data.sgp40_valid = 0;
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         of the assert_param that has failed.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
