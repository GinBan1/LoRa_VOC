/* 启动 LoRa 网关、显示、告警和 OneNET 网络任务。 */

#include "bsp.h"
#include "alarm_app.h"
#include "boot_diag.h"
#include "sdkconfig.h"

void app_main(void)
{
    Boot_Diag_Init();
    scheduler_init();
    printf("=== ESP32 LoRa / OLED / OneNET Gateway ===\r\n");

    /* 联网和重连独立运行，不阻塞 LoRa 接收和显示。 */
    OneNet_Init(CONFIG_AIR_GATEWAY_WIFI_SSID,
                CONFIG_AIR_GATEWAY_WIFI_PASSWORD);
    Alarm_App_Init();

    while (1)
    {
        scheduler_run();
        Boot_Diag_Task();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
