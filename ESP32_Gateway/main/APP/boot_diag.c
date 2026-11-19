#include "boot_diag.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BOOT_DIAG_INTERVAL_MS 300000U

static const char *TAG = "Boot";
static bool g_boot_ready;
static uint32_t g_boot_id;
static int g_boot_reset;
static uint32_t g_boot_last_report;
static TaskHandle_t g_boot_main_task;

const char *Boot_Diag_ResetName(int reason)
{
    switch (reason) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXTERNAL";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WATCHDOG";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        case ESP_RST_USB: return "USB";
        case ESP_RST_JTAG: return "JTAG";
        case ESP_RST_EFUSE: return "EFUSE";
        case ESP_RST_PWR_GLITCH: return "POWER_GLITCH";
        case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
        case ESP_RST_UNKNOWN:
        default: return "UNKNOWN";
    }
}

static uint32_t Boot_Diag_Now(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void Boot_Diag_Resources(const char *phase)
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG, "%s boot=%08lx uptime_ms=%llu internal_free=%lu internal_min=%lu "
             "internal_largest=%lu main_stack_min_bytes=%lu", phase,
             (unsigned long)g_boot_id,
             (unsigned long long)(esp_timer_get_time() / 1000),
             (unsigned long)heap_caps_get_free_size(caps),
             (unsigned long)heap_caps_get_minimum_free_size(caps),
             (unsigned long)heap_caps_get_largest_free_block(caps),
             (unsigned long)uxTaskGetStackHighWaterMark(g_boot_main_task));
}

void Boot_Diag_Init(void)
{
    if (g_boot_ready) return;
    g_boot_reset = (int)esp_reset_reason();
    /* 本次启动的诊断编号，不跨重启保留。 */
    g_boot_id = esp_random();
    g_boot_main_task = xTaskGetCurrentTaskHandle();
    g_boot_last_report = Boot_Diag_Now();
    g_boot_ready = true;
    ESP_LOGI(TAG, "session=%08lx reset=%s(%d) IDF=%s", (unsigned long)g_boot_id,
             Boot_Diag_GetResetName(), g_boot_reset, esp_get_idf_version());
    Boot_Diag_Resources("start");
}

void Boot_Diag_Task(void)
{
    if (!g_boot_ready) return;
    uint32_t now = Boot_Diag_Now();
    if ((uint32_t)(now - g_boot_last_report) < BOOT_DIAG_INTERVAL_MS) return;
    g_boot_last_report = now;
    Boot_Diag_Resources("periodic");
}

uint32_t Boot_Diag_GetBootId(void) { return g_boot_id; }
int Boot_Diag_GetResetReason(void) { return g_boot_reset; }
const char *Boot_Diag_GetResetName(void) { return Boot_Diag_ResetName(g_boot_reset); }

uint8_t Boot_Diag_Format(char *buffer, size_t capacity)
{
    if (!buffer || capacity == 0U) return 0U;
    int n = snprintf(buffer, capacity, "Gateway Boot ID: %08lx\r\nGateway reset: %s (%d)\r\n",
                     (unsigned long)g_boot_id, Boot_Diag_GetResetName(), g_boot_reset);
    if (n < 0 || (size_t)n >= capacity) {
        buffer[0] = '\0';
        return 0U;
    }
    return 1U;
}
