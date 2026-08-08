#include "lora_receiver.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sx1268_radio.h"

#define LORA_RX_TASK_STACK_SIZE 4096U
#define LORA_RX_TASK_PRIORITY   6U

static const char *TAG = "LoRaRX";
static lora_receiver_snapshot_t s_snapshot;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task_handle;

static void save_packet(const sx1268_rx_packet_t *packet)
{
    telemetry_packet_t telemetry = {0};
    telemetry_parse_result_t parse_result;
    uint32_t packet_count;
    uint32_t valid_packet_count;

    parse_result = telemetry_protocol_parse(packet->data,
                                             packet->length,
                                             &telemetry);

    portENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.packet_count++;
    if (parse_result == TELEMETRY_PARSE_OK)
    {
        s_snapshot.telemetry_valid = true;
        s_snapshot.valid_packet_count++;
        s_snapshot.last_rx_ms = (uint32_t)(esp_timer_get_time() / 1000);
        s_snapshot.rssi_dbm = packet->rssi_dbm;
        s_snapshot.snr_db = packet->snr_db;
        s_snapshot.payload_length = packet->length;
        s_snapshot.telemetry = telemetry;
    }
    else
    {
        s_snapshot.error_count++;
        if (parse_result == TELEMETRY_PARSE_CRC_ERROR)
        {
            s_snapshot.crc_error_count++;
        }
        else
        {
            s_snapshot.parse_error_count++;
        }
    }
    packet_count = s_snapshot.packet_count;
    valid_packet_count = s_snapshot.valid_packet_count;
    portEXIT_CRITICAL(&s_snapshot_lock);

    if (parse_result == TELEMETRY_PARSE_OK)
    {
        ESP_LOGI(TAG,
                 "RX #%lu valid #%lu seq=%lu, RSSI=%d dBm, SNR=%.1f dB",
                 (unsigned long)packet_count,
                 (unsigned long)valid_packet_count,
                 (unsigned long)telemetry.sequence,
                 packet->rssi_dbm,
                 (double)packet->snr_db);
    }
    else
    {
        ESP_LOGW(TAG, "RX #%lu rejected: %s",
                 (unsigned long)packet_count,
                 telemetry_parse_result_name(parse_result));
    }
}

static void lora_receive_task(void *argument)
{
    (void)argument;

    for (;;)
    {
        sx1268_rx_packet_t packet = {0};
        bool packet_ready = false;
        esp_err_t error = sx1268_radio_poll(&packet, &packet_ready);

        if (packet_ready)
        {
            save_packet(&packet);
        }
        else if ((error != ESP_OK) && (error != ESP_ERR_INVALID_CRC))
        {
            portENTER_CRITICAL(&s_snapshot_lock);
            s_snapshot.error_count++;
            portEXIT_CRITICAL(&s_snapshot_lock);
            ESP_LOGW(TAG, "receive poll failed: %s", sx1268_radio_error_name(error));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        else if (error == ESP_ERR_INVALID_CRC)
        {
            portENTER_CRITICAL(&s_snapshot_lock);
            s_snapshot.error_count++;
            portEXIT_CRITICAL(&s_snapshot_lock);
            ESP_LOGW(TAG, "discarded packet with CRC/header error");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t lora_receiver_init(void)
{
    uint8_t radio_status = 0U;
    esp_err_t error;

    if (s_task_handle != NULL)
    {
        return ESP_OK;
    }

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    error = sx1268_radio_init(&radio_status);

    portENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.radio_status = radio_status;
    s_snapshot.radio_ready = (error == ESP_OK);
    portEXIT_CRITICAL(&s_snapshot_lock);

    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "E22-400M22S init failed: %s", sx1268_radio_error_name(error));
        return error;
    }

    BaseType_t task_result = xTaskCreate(lora_receive_task,
                                         "lora_receiver",
                                         LORA_RX_TASK_STACK_SIZE,
                                         NULL,
                                         LORA_RX_TASK_PRIORITY,
                                         &s_task_handle);
    if (task_result != pdPASS)
    {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "ready: status=0x%02X, 470.3MHz SF7/BW125/CR4-5 private sync",
             radio_status);
    return ESP_OK;
}

void lora_receiver_get_snapshot(lora_receiver_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }

    portENTER_CRITICAL(&s_snapshot_lock);
    memcpy(snapshot, &s_snapshot, sizeof(*snapshot));
    portEXIT_CRITICAL(&s_snapshot_lock);

    snapshot->telemetry_fresh = snapshot->telemetry_valid &&
        ((uint32_t)((uint32_t)(esp_timer_get_time() / 1000) -
                    snapshot->last_rx_ms) <= LORA_RECEIVER_FRESH_MS);
}
