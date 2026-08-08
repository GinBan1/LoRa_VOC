/**
 * @file main.c
 * @brief ESP32 LoRa receiver and OneNET gateway entry point.
 */

#include "bsp.h"
#include "sdkconfig.h"

void app_main(void)
{
    /* APP modules own their initialization; main stays deliberately small. */
    scheduler_init();
    printf("=== ESP32 LoRa / OLED / OneNET Gateway ===\r\n");

    /* Connection and retry run in a dedicated task, so OLED/LoRa never wait
     * for DNS, TCP, MQTT login, or reconnect backoff. */
    OneNet_Init(CONFIG_AIR_GATEWAY_WIFI_SSID,
                CONFIG_AIR_GATEWAY_WIFI_PASSWORD);

    while (1)
    {
        scheduler_run();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
