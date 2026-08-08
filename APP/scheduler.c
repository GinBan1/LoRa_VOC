#include "scheduler.h"

#include "app_environment.h"
#include "data_logger.h"
#include "lora_app.h"
#include "main.h"
#include "oled.h"
#include "tinyml_app.h"

#include <stdint.h>

typedef struct
{
    void (*task_func)(void);
    uint32_t period_ms;
    uint32_t last_run_ms;
} scheduler_task_t;

static scheduler_task_t scheduler_tasks[] =
{
#if DATA_LOGGER_COLLECTION_MODE
    {Environment_App_Task, 1000U, 0U},
    {DataLogger_App_Task,     10U, 0U}
#elif LORA_APP_LINK_TEST_MODE
    {Lora_App_Task,           5U, 0U}
#else
    {Environment_App_Task, 1000U, 0U},
    {TinyML_App_Task,       1000U, 0U},
    {oled_task,             1000U, 0U},
    {Lora_App_Task,            5U, 0U}
#endif
};

void scheduler_init(void)
{
    uint32_t now;
    uint32_t i;
    uint32_t task_count;

#if DATA_LOGGER_COLLECTION_MODE
    Environment_App_Init();
    DataLogger_App_Init();
#elif LORA_APP_LINK_TEST_MODE
    Lora_App_Init();
#else
    Environment_App_Init();
    TinyML_App_Init();
    OLED_Init();
    OLED_Clear();
    lcd_proc();
    Lora_App_Init();
#endif

    now = HAL_GetTick();
    task_count = sizeof(scheduler_tasks) / sizeof(scheduler_tasks[0]);
    for (i = 0U; i < task_count; i++)
    {
        scheduler_tasks[i].last_run_ms = now;
    }
}

void scheduler_run(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t i;
    uint32_t task_count =
        sizeof(scheduler_tasks) / sizeof(scheduler_tasks[0]);

    for (i = 0U; i < task_count; i++)
    {
        if ((now - scheduler_tasks[i].last_run_ms) >=
            scheduler_tasks[i].period_ms)
        {
            scheduler_tasks[i].last_run_ms = now;
            scheduler_tasks[i].task_func();
        }
    }
}
