#include "scheduler.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lora_receiver.h"
#include "oled_display.h"
#include "onenet.h"

typedef struct
{
    void (*task_func)(void);
    uint32_t rate_ms;
    uint32_t last_run;
} scheduler_task_t;

static const char *TAG = "Scheduler";
static uint8_t s_task_count;

static scheduler_task_t s_tasks[] =
{
    {oled_display_task,  500U,   0U},
};

void scheduler_init(void)
{
    esp_err_t error;
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    s_task_count = (uint8_t)(sizeof(s_tasks) / sizeof(s_tasks[0]));
    for (uint8_t i = 0U; i < s_task_count; ++i)
    {
        s_tasks[i].last_run = now_ms;
    }

    /* LoRa uses a dedicated task so a network receive cannot delay radio RX. */
    error = lora_receiver_init();
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "LoRa receiver init failed: %s", esp_err_to_name(error));
    }

    error = oled_display_init();
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(error));
    }
}

void scheduler_run(void)
{
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    for (uint8_t i = 0U; i < s_task_count; ++i)
    {
        if ((uint32_t)(now_ms - s_tasks[i].last_run) >= s_tasks[i].rate_ms)
        {
            s_tasks[i].last_run = now_ms;
            s_tasks[i].task_func();
        }
    }
}
