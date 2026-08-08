#include "rtos_app.h"

#include "cmsis_os2.h"
#include <stdio.h>

#include "app_environment.h"
#include "boot_trace.h"
#include "data_logger.h"
#include "lora_app.h"
#include "oled.h"
#include "tinyml_app.h"

#define RTOS_APP_PERIOD_MS              1000U
#define RTOS_LOGGER_POLL_MS               10U
#define RTOS_LORA_IRQ_FLAG              (1UL << 0)

/* stack_size is expressed in bytes by CMSIS-RTOS2. */
#define RTOS_APP_STACK_BYTES             8192U
#define RTOS_COLLECTION_STACK_BYTES      4096U
#define RTOS_LORA_STACK_BYTES            2048U
#define RTOS_STACK_MONITOR_PERIOD_MS     60000U
#define RTOS_APP_STACK_WARN_BYTES         1024U
#define RTOS_LORA_STACK_WARN_BYTES         512U

static osThreadId_t app_thread_id;
static osThreadId_t lora_thread_id;

static void rtos_monitor_stack_space(void)
{
    uint32_t free_bytes;

    if (app_thread_id != NULL)
    {
        free_bytes = osThreadGetStackSpace(app_thread_id);
        if (free_bytes < RTOS_APP_STACK_WARN_BYTES)
        {
            printf("[RTOS] WARN: application stack only %lu bytes free\r\n",
                   (unsigned long)free_bytes);
        }
    }

    if (lora_thread_id != NULL)
    {
        free_bytes = osThreadGetStackSpace(lora_thread_id);
        if (free_bytes < RTOS_LORA_STACK_WARN_BYTES)
        {
            printf("[RTOS] WARN: LoRa stack only %lu bytes free\r\n",
                   (unsigned long)free_bytes);
        }
    }
}

static uint32_t rtos_ms_to_ticks(uint32_t delay_ms)
{
    uint32_t frequency = osKernelGetTickFreq();
    uint64_t ticks = ((uint64_t)delay_ms * frequency + 999ULL) / 1000ULL;

    if (ticks == 0ULL)
    {
        ticks = 1ULL;
    }
    return (uint32_t)ticks;
}

#if DATA_LOGGER_COLLECTION_MODE
static void rtos_collection_thread(void *argument)
{
    uint32_t last_environment_ms;
    uint32_t last_stack_check_ms;

    (void)argument;
    Environment_App_Init();
    DataLogger_App_Init();
    last_environment_ms = HAL_GetTick();
    last_stack_check_ms = last_environment_ms;

    for (;;)
    {
        uint32_t now = HAL_GetTick();

        if ((now - last_environment_ms) >= RTOS_APP_PERIOD_MS)
        {
            last_environment_ms += RTOS_APP_PERIOD_MS;
            Environment_App_Task();
        }

        DataLogger_App_Task();
        if ((now - last_stack_check_ms) >= RTOS_STACK_MONITOR_PERIOD_MS)
        {
            last_stack_check_ms = now;
            rtos_monitor_stack_space();
        }
        osDelay(rtos_ms_to_ticks(RTOS_LOGGER_POLL_MS));
    }
}
#elif !LORA_APP_LINK_TEST_MODE
static void rtos_application_thread(void *argument)
{
    uint32_t next_wake;
    uint32_t last_stack_check_ms;

    (void)argument;
    Environment_App_Init();
    TinyML_App_Init();
    OLED_Init();
    OLED_Clear();
    lcd_proc();

    next_wake = osKernelGetTickCount();
    last_stack_check_ms = HAL_GetTick();

    for (;;)
    {
        Environment_App_Task();
        TinyML_App_Task();
        oled_task();

        if ((HAL_GetTick() - last_stack_check_ms) >= RTOS_STACK_MONITOR_PERIOD_MS)
        {
            last_stack_check_ms = HAL_GetTick();
            rtos_monitor_stack_space();
        }

        next_wake += rtos_ms_to_ticks(RTOS_APP_PERIOD_MS);
        (void)osDelayUntil(next_wake);
    }
}
#endif

static void rtos_lora_thread(void *argument)
{
    (void)argument;
    Lora_App_Init();

    for (;;)
    {
        uint32_t delay_ms;

        Lora_App_Task();
        delay_ms = Lora_App_GetNextWakeDelayMs();
        (void)osThreadFlagsWait(RTOS_LORA_IRQ_FLAG,
                                osFlagsWaitAny,
                                rtos_ms_to_ticks(delay_ms));
    }
}

void RtosApp_Init(void)
{
    static const osThreadAttr_t lora_attributes =
    {
        .name = "LoRaTask",
        .stack_size = RTOS_LORA_STACK_BYTES,
        .priority = osPriorityAboveNormal
    };

#if DATA_LOGGER_COLLECTION_MODE
    static const osThreadAttr_t app_attributes =
    {
        .name = "CollectionTask",
        .stack_size = RTOS_COLLECTION_STACK_BYTES,
        .priority = osPriorityNormal
    };

    app_thread_id = osThreadNew(rtos_collection_thread, NULL, &app_attributes);
    lora_thread_id = NULL;
#elif LORA_APP_LINK_TEST_MODE
    app_thread_id = NULL;
    lora_thread_id = osThreadNew(rtos_lora_thread, NULL, &lora_attributes);
#else
    static const osThreadAttr_t app_attributes =
    {
        .name = "ApplicationTask",
        .stack_size = RTOS_APP_STACK_BYTES,
        .priority = osPriorityNormal
    };

    app_thread_id = osThreadNew(rtos_application_thread, NULL, &app_attributes);
    lora_thread_id = osThreadNew(rtos_lora_thread, NULL, &lora_attributes);
#endif

    if ((app_thread_id == NULL) && (lora_thread_id == NULL))
    {
        BootTrace_Fatal("no application thread was created");
        Error_Handler();
    }

#if !DATA_LOGGER_COLLECTION_MODE && !LORA_APP_LINK_TEST_MODE
    if ((app_thread_id == NULL) || (lora_thread_id == NULL))
    {
        BootTrace_Fatal("application thread creation failed");
        Error_Handler();
    }
#endif
}

void RtosApp_NotifyLoRaFromISR(void)
{
    if (lora_thread_id != NULL)
    {
        (void)osThreadFlagsSet(lora_thread_id, RTOS_LORA_IRQ_FLAG);
    }
}
