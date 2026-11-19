#include "alarm_app.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lora_receiver.h"
#include "mail.h"
#include "sdkconfig.h"

#define ALARM_EVENT_MAX_AGE_MS 300000U

static const char *TAG = "Alarm";
static alarm_state_t alarm_state;
static portMUX_TYPE alarm_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t Alarm_App_Time(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static alarm_input_t Alarm_App_Input(const lora_receiver_snapshot_t *snapshot)
{
    const telemetry_packet_t *packet = &snapshot->telemetry;
    const uint8_t sensors = TELEMETRY_FLAG_BME680_VALID | TELEMETRY_FLAG_SGP40_VALID;
    return (alarm_input_t){
        .sequence = packet->sequence,
        .uptime_ms = packet->uptime_ms,
        .voc_index = packet->voc_index,
        .up_score = packet->warning_x1000,
        .confidence = packet->confidence_x1000,
        .ai_state = packet->ai_state,
        /* AQ1 仅用于查看，不产生告警。 */
        .fresh = packet->protocol_version == 2U && snapshot->radio_ready && snapshot->telemetry_valid && snapshot->telemetry_fresh,
        .sensors_valid = (packet->flags & sensors) == sensors,
        .ai_valid = false,
        .source_protocol = packet->protocol_version,
        .event_state = packet->event_state,
        .event_end_ms = packet->event_end_ms,
        .event_voc_x10 = packet->event_voc_x10,
        .event_episode_id = packet->event_episode_id,
        .event_ready = packet->protocol_version == 2U && (packet->flags & TELEMETRY_FLAG_EVENT_READY) != 0U,
        .event_rx_age_ms = Alarm_App_Time() - snapshot->last_rx_ms
    };
}

bool Alarm_App_EventCurrent(const alarm_event_t *event)
{
    if (event == NULL || event->channel >= ALARM_CHANNEL_COUNT || event->id == 0)
        return false;
    lora_receiver_snapshot_t snapshot;
    lora_receiver_get_snapshot(&snapshot);
    alarm_input_t input = Alarm_App_Input(&snapshot);
    uint32_t now_ms = Alarm_App_Time();

    portENTER_CRITICAL(&alarm_lock);
    bool current = Alarm_EventCurrent(&alarm_state, &input, now_ms, event,
                                     ALARM_EVENT_MAX_AGE_MS);
    portEXIT_CRITICAL(&alarm_lock);
    return current;
}

#if CONFIG_AIR_GATEWAY_ALARM_ENABLE
static void Alarm_App_Task(void *argument)
{
    (void)argument;
    TickType_t wake_tick = xTaskGetTickCount();
    while (1)
    {
        lora_receiver_snapshot_t snapshot;
        lora_receiver_get_snapshot(&snapshot);
        alarm_input_t input = Alarm_App_Input(&snapshot);
        alarm_event_t event;
        portENTER_CRITICAL(&alarm_lock);
        bool emitted = Alarm_Process(&alarm_state, &input, Alarm_App_Time(), &event);
        portEXIT_CRITICAL(&alarm_lock);
        if (emitted)
        {
            ESP_LOGI(TAG, "Node VOC event %s: id=%lu seq=%lu VOC=%u mean_x10=%u episode=%lu state=%u",
                     event.active ? "raised" : "recovered",
                     (unsigned long)event.id, (unsigned long)event.sequence,
                     (unsigned)event.voc_index, (unsigned)event.event_voc_x10,
                     (unsigned long)event.event_episode_id, (unsigned)event.event_state);
#if CONFIG_AIR_GATEWAY_MAIL_ENABLE
            if (!Mail_SendAlarm(&event))
                ESP_LOGW(TAG, "Mail event was not queued (id=%lu)", (unsigned long)event.id);
#endif
        }
        vTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(1000));
    }
}
#endif

void Alarm_App_Init(void)
{
#if CONFIG_AIR_GATEWAY_ALARM_ENABLE
    const alarm_config_t config = {
        .voc_enter = CONFIG_AIR_GATEWAY_ALARM_VOC_ENTER,
        .voc_clear = CONFIG_AIR_GATEWAY_ALARM_VOC_CLEAR,
        .up_enter = CONFIG_AIR_GATEWAY_ALARM_UP_ENTER,
        .up_clear = CONFIG_AIR_GATEWAY_ALARM_UP_CLEAR,
        .confidence_min = CONFIG_AIR_GATEWAY_ALARM_CONFIDENCE_MIN,
        .hold_ms = CONFIG_AIR_GATEWAY_ALARM_HOLD_SECONDS * 1000U,
        .cooldown_ms = CONFIG_AIR_GATEWAY_ALARM_COOLDOWN_SECONDS * 1000U,
        .fresh_ms = LORA_RECEIVER_FRESH_MS
    };
    if (alarm_state.initialized)
        return;
    if (!Alarm_Init(&alarm_state, &config))
    {
        ESP_LOGE(TAG, "Invalid alarm thresholds; alarm task disabled");
        return;
    }
    Mail_Init();
    if (xTaskCreate(Alarm_App_Task, "alarm", 4096, NULL, 3, NULL) != pdPASS)
    {
        alarm_state.initialized = false;
        ESP_LOGE(TAG, "Could not create alarm task");
        return;
    }
    ESP_LOGI(TAG, "Tracking AQ2 node-confirmed events; legacy AQ1 alerts disabled; no additional threshold/hold");
#else
    ESP_LOGI(TAG, "Alarm tracking disabled");
#endif
}
