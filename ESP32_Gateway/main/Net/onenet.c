#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_tls.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "onenet.h"
#include "MQTT.h"

#include "base64.h"
#include "hmac_sha1.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "cJSON.h"
#include "json_guard.h"
#include "lora_receiver.h"
#include "sdkconfig.h"


#define PROID                    CONFIG_AIR_GATEWAY_ONENET_PRODUCT_ID
#define ACCESS_KEY               CONFIG_AIR_GATEWAY_ONENET_ACCESS_KEY
#define DEVICE_NAME              CONFIG_AIR_GATEWAY_ONENET_DEVICE_NAME
#define METHOD                   "sha1"
#define ET                       ((unsigned long)CONFIG_AIR_GATEWAY_ONENET_TOKEN_EXPIRY)


#define TAG                      "OneNET"
#define MQTT_PORT                8883
#define MQTT_HOST                "mqttstls.heclouds.com"
/* OneNET 证书名称与接入域名不同；仍校验证书和下列名称。 */
#define MQTT_SERVER_CERT_NAME    "OneNET MQTTS"

#define RX_BUF_SIZE              1024
#define ONENET_TLS_TIMEOUT_MS     ((uint32_t)CONFIG_AIR_GATEWAY_ONENET_TLS_TIMEOUT_MS)
#define ONENET_CONNACK_TIMEOUT_MS 5000U
#define ONENET_RETRY_MAX_MS      30000U
#define ONENET_SNTP_RETRY_MS      30000U
#define ONENET_MIN_VALID_UNIX_TIME 1704067200LL /* 2024-01-01 00:00:00 UTC */

/* 心跳间隔不超过 300 秒。 */
#define MQTT_KEEPALIVE_SEC        240
static uint32_t g_last_ping = 0;

volatile uint8_t OneNet_Ready = 0;
static esp_tls_t *g_tls;
static unsigned char g_rx_buf[RX_BUF_SIZE + 1U];
static unsigned char g_rx_frame[RX_BUF_SIZE + 1U];
static size_t g_rx_used;

typedef enum
{
    ONENET_MQTT_IDLE = 0,
    ONENET_MQTT_WAIT_CONNACK,
    ONENET_MQTT_READY
} onenet_mqtt_state_t;

static bool g_network_initialized;
static volatile bool g_wifi_connected;
static volatile bool g_wifi_reconnect_pending;
static volatile bool g_socket_close_pending;
static char g_wifi_ssid[sizeof(((wifi_config_t *)0)->sta.ssid)];
static char g_wifi_password[sizeof(((wifi_config_t *)0)->sta.password)];
static uint8_t g_wifi_retry_count;
static uint8_t g_mqtt_retry_count;
static uint32_t g_wifi_retry_due_ms;
static uint32_t g_mqtt_retry_due_ms;
static uint32_t g_connack_deadline_ms;
static uint32_t g_sntp_retry_due_ms;
static bool g_sntp_initialized;
static onenet_mqtt_state_t g_mqtt_state = ONENET_MQTT_IDLE;

/* CMake 内嵌的 OneNET PEM 证书以 NUL 结尾。 */
extern const uint8_t onenet_root_ca_pem_start[] asm("_binary_onenet_root_ca_pem_start");
extern const uint8_t onenet_root_ca_pem_end[] asm("_binary_onenet_root_ca_pem_end");

static uint32_t onenet_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* 用有符号差值处理毫秒计数回绕。 */
static bool onenet_time_reached(uint32_t now_ms, uint32_t due_ms)
{
    return (int32_t)(now_ms - due_ms) >= 0;
}

static uint32_t onenet_retry_delay_ms(uint8_t retry_count)
{
    static const uint32_t retry_delays_ms[] = {1000U, 2000U, 4000U, 8000U, 16000U, ONENET_RETRY_MAX_MS};
    const uint8_t last = (uint8_t)(sizeof(retry_delays_ms) / sizeof(retry_delays_ms[0]) - 1U);

    return retry_delays_ms[(retry_count < last) ? retry_count : last];
}

static void onenet_close_socket(void)
{
    if (g_tls != NULL)
    {
        esp_tls_conn_destroy(g_tls);
        g_tls = NULL;
    }

    OneNet_Ready = 0;
    g_last_ping = 0U;
    g_rx_used = 0U;
    g_mqtt_state = ONENET_MQTT_IDLE;
}

static void onenet_schedule_mqtt_retry(uint32_t now_ms, const char *reason)
{
    onenet_close_socket();
    g_mqtt_retry_due_ms = now_ms + onenet_retry_delay_ms(g_mqtt_retry_count);
    if (g_mqtt_retry_count < UINT8_MAX)
    {
        ++g_mqtt_retry_count;
    }

    ESP_LOGW(TAG, "MQTT offline (%s), retry in %lu ms", reason,
             (unsigned long)(g_mqtt_retry_due_ms - now_ms));
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        g_wifi_reconnect_pending = true;
        g_wifi_retry_due_ms = onenet_now_ms();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        const uint32_t now_ms = onenet_now_ms();

        ESP_LOGW(TAG, "WiFi disconnected; reconnect scheduled");
        g_wifi_connected = false;
        g_wifi_reconnect_pending = true;
        g_wifi_retry_due_ms = now_ms + onenet_retry_delay_ms(g_wifi_retry_count);
        if (g_wifi_retry_count < UINT8_MAX)
        {
            ++g_wifi_retry_count;
        }
        g_socket_close_pending = true;
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        g_wifi_connected = true;
        g_wifi_reconnect_pending = false;
        g_wifi_retry_count = 0U;
        g_mqtt_retry_count = 0U;
        g_mqtt_retry_due_ms = onenet_now_ms();
    }
}

static void wifi_init_sta(char *ssid, char *password)
{
    static bool wifi_inited = false;

    if (wifi_inited)
    {
        ESP_LOGI(TAG, "Waiting for WiFi reconnect...");
        /* 重连交给网络任务处理。 */
        return;
    }

    wifi_inited = true;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 不等待入网；IP 由回调通知。 */
    ESP_LOGI(TAG, "WiFi Init Done, Connecting...");
}

/* URL 只保留字母、数字和 ._~-，其余字节转为百分号编码。 */
static char *url_encode_buf(char *buf_out, int out_len, char *buf_in)
{
    int j = 0;
    char *p = buf_in;
    while (*p && j < out_len - 3)
    {
        if ((*p >= '0' && *p <= '9') ||
            (*p >= 'A' && *p <= 'Z') ||
            (*p >= 'a' && *p <= 'z') ||
            *p == '.' || *p == '_' || *p == '~' || *p == '-')
        {
            buf_out[j++] = *p;
        }
        else
        {
            sprintf(&buf_out[j], "%%%02X", (unsigned char)*p);
            j += 3;
        }
        p++;
    }
    buf_out[j] = '\0';
    return buf_out;
}

static void OneNET_GenToken(char *token, unsigned short token_len)
{
    char sign_buf[64];
    char sign_enc[128];
    char hmac_sha1_buf[64];
    char access_key_base64[64];
    char string_for_signature[72];
    size_t olen = 0;

    memset(access_key_base64, 0, sizeof(access_key_base64));
    BASE64_Decode((unsigned char *)access_key_base64, sizeof(access_key_base64),
                  &olen, (unsigned char *)ACCESS_KEY, strlen(ACCESS_KEY));

    memset(string_for_signature, 0, sizeof(string_for_signature));
    snprintf(string_for_signature, sizeof(string_for_signature),
             "%lu\n%s\nproducts/%s/devices/%s\n%s",
             ET, METHOD, PROID, DEVICE_NAME, "2018-10-31");

    memset(hmac_sha1_buf, 0, sizeof(hmac_sha1_buf));
    mqtt_hmac_sha1((unsigned char *)access_key_base64, (int)olen,
              (unsigned char *)string_for_signature, (int)strlen(string_for_signature),
              (unsigned char *)hmac_sha1_buf);

    olen = 0;
    memset(sign_buf, 0, sizeof(sign_buf));
    BASE64_Encode((unsigned char *)sign_buf, sizeof(sign_buf), &olen,
                  (unsigned char *)hmac_sha1_buf, 20);    // SHA1 固定 20 字节

    memset(sign_enc, 0, sizeof(sign_enc));
    url_encode_buf(sign_enc, sizeof(sign_enc), sign_buf);

    snprintf(token, token_len,
             "version=2018-10-31&res=products%%2F%s%%2Fdevices%%2F%s&et=%lu&method=%s&sign=%s",
             PROID, DEVICE_NAME, ET, METHOD, sign_enc);
}

/* 在网络任务中连接并校验 TLS。 */
static int onenet_tls_connect(void)
{
    const esp_tls_cfg_t tls_cfg = {
        .cacert_buf = onenet_root_ca_pem_start,
        .cacert_bytes = (unsigned int)(onenet_root_ca_pem_end - onenet_root_ca_pem_start),
        .common_name = MQTT_SERVER_CERT_NAME,
        .skip_common_name = false,
        /* 握手后保留非阻塞读写。 */
        .non_block = true,
        .timeout_ms = (int)ONENET_TLS_TIMEOUT_MS,
        .tls_version = ESP_TLS_VER_TLS_1_2,
    };

    return esp_tls_conn_new_sync(MQTT_HOST, (int)strlen(MQTT_HOST),
                                 MQTT_PORT, &tls_cfg, g_tls);
}

static bool onenet_clock_is_valid(void)
{
    time_t now;

    time(&now);
    return (int64_t)now >= ONENET_MIN_VALID_UNIX_TIME;
}

/* 异步校时，供 TLS 检查证书有效期。 */
static bool onenet_ensure_time_synced(uint32_t now_ms)
{
    esp_err_t result;

    if (onenet_clock_is_valid())
    {
        return true;
    }

    if (!onenet_time_reached(now_ms, g_sntp_retry_due_ms))
    {
        return false;
    }

    if (!g_sntp_initialized)
    {
        esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        result = esp_netif_sntp_init(&sntp_cfg);
        if (result == ESP_OK)
        {
            g_sntp_initialized = true;
            ESP_LOGI(TAG, "SNTP started; waiting for a valid clock before TLS");
        }
        else
        {
            ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(result));
        }
    }
    else
    {
        result = esp_netif_sntp_start();
        if (result != ESP_OK)
        {
            ESP_LOGW(TAG, "SNTP retry failed: %s", esp_err_to_name(result));
        }
    }

    g_sntp_retry_due_ms = now_ms + ONENET_SNTP_RETRY_MS;
    return false;
}

/* 在网络任务中发完一帧 MQTT，允许短暂重试。 */
static int tls_send(const unsigned char *data, int len)
{
    int sent = 0;
    const uint32_t deadline_ms = onenet_now_ms() + 250U;

    if (g_tls == NULL || data == NULL || len <= 0)
    {
        return -1;
    }

    while (sent < len)
    {
        const int result = (int)esp_tls_conn_write(g_tls, data + sent,
                                                    (size_t)(len - sent));

        if (result > 0)
        {
            sent += result;
            continue;
        }
        if (result == ESP_TLS_ERR_SSL_WANT_READ || result == ESP_TLS_ERR_SSL_WANT_WRITE)
        {
            if (onenet_time_reached(onenet_now_ms(), deadline_ms))
            {
                return -1;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        return -1;
    }

    return sent;
}

/* 正数为收到的字节数，0 无数据，-2 对端关闭，-1 错误。 */
static int tls_recv(unsigned char *buf, int len)
{
    const int result = (g_tls != NULL) ?
        (int)esp_tls_conn_read(g_tls, buf, (size_t)len) : -1;

    if (result > 0)
    {
        return result;
    }
    if (result == 0)
    {
        return -2;
    }
    if (result == ESP_TLS_ERR_SSL_WANT_READ || result == ESP_TLS_ERR_SSL_WANT_WRITE)
    {
        return 0;
    }

    return -1;
}

/* 缓存 TLS 下发数据；单次读取不一定恰好是一帧 MQTT。 */
static int onenet_receive_stream(void)
{
    int result;

    if (g_rx_used >= RX_BUF_SIZE)
    {
        return -1;
    }

    result = tls_recv(&g_rx_buf[g_rx_used],
                         (int)(RX_BUF_SIZE - g_rx_used));
    if (result > 0)
    {
        g_rx_used += (size_t)result;
        g_rx_buf[g_rx_used] = '\0';
    }
    return result;
}

/* 取出一帧：1 成功，0 需继续接收，-1 帧格式错误。 */
static int onenet_extract_mqtt_frame(size_t *frame_length)
{
    size_t total_length = 0U;
    int32 result;

    if (frame_length == NULL) return -1;
    *frame_length = 0U;
    result = MQTT_FrameLength(g_rx_buf, g_rx_used, &total_length);
    if (result < 0 || total_length > RX_BUF_SIZE) return -1;
    if (result == 0) return 0;
    if (MQTT_UnPacketRecvLength(g_rx_buf, total_length) == 255U) return -1;

    memcpy(g_rx_frame, g_rx_buf, total_length);
    g_rx_frame[total_length] = '\0';
    g_rx_used -= total_length;
    if (g_rx_used > 0U)
        memmove(g_rx_buf, &g_rx_buf[total_length], g_rx_used);
    g_rx_buf[g_rx_used] = '\0';
    *frame_length = total_length;
    return 1;
}

/* 发起 OneNET 的 MQTT 登录。 */
static bool onenet_send_connect_request(uint32_t now_ms)
{
    MQTT_PACKET_STRUCTURE mqtt_packet = {NULL, 0, 0, 0};
    char token_buf[192];
    int send_result;

    OneNET_GenToken(token_buf, sizeof(token_buf));
    if (MQTT_PacketConnect(PROID, token_buf, DEVICE_NAME,
                           MQTT_KEEPALIVE_SEC, 1, MQTT_QOS_LEVEL0,
                           NULL, NULL, 0, &mqtt_packet) != 0)
    {
        ESP_LOGE(TAG, "Failed to build MQTT CONNECT packet");
        return false;
    }

    send_result = tls_send(mqtt_packet._data, (int)mqtt_packet._len);
    MQTT_DeleteBuffer(&mqtt_packet);
    if (send_result <= 0)
    {
        onenet_schedule_mqtt_retry(now_ms, "MQTT CONNECT send failed");
        return false;
    }

    g_mqtt_state = ONENET_MQTT_WAIT_CONNACK;
    g_connack_deadline_ms = now_ms + ONENET_CONNACK_TIMEOUT_MS;
    return true;
}

static void onenet_process_connection(uint32_t now_ms)
{
    int connect_result;
    int rx_len;
    size_t frame_length;
    int frame_result;

    if (g_socket_close_pending)
    {
        g_socket_close_pending = false;
        onenet_close_socket();
    }

    if (!g_wifi_connected)
    {
        if (g_wifi_reconnect_pending && onenet_time_reached(now_ms, g_wifi_retry_due_ms))
        {
            const esp_err_t result = esp_wifi_connect();

            g_wifi_reconnect_pending = false;
            if (result != ESP_OK && result != ESP_ERR_WIFI_STATE)
            {
                ESP_LOGW(TAG, "WiFi connect request failed: %s", esp_err_to_name(result));
                g_wifi_reconnect_pending = true;
                g_wifi_retry_due_ms = now_ms + onenet_retry_delay_ms(g_wifi_retry_count);
                if (g_wifi_retry_count < UINT8_MAX)
                {
                    ++g_wifi_retry_count;
                }
            }
        }
        return;
    }

    if (g_mqtt_state == ONENET_MQTT_IDLE)
    {
        if (!onenet_time_reached(now_ms, g_mqtt_retry_due_ms))
        {
            return;
        }

        if (!onenet_ensure_time_synced(now_ms))
        {
            return;
        }

        ESP_LOGI(TAG, "Connecting to OneNET MQTTS broker...");
        g_tls = esp_tls_init();
        if (g_tls == NULL)
        {
            onenet_schedule_mqtt_retry(now_ms, "TLS context allocation failed");
            return;
        }

        connect_result = onenet_tls_connect();
        if (connect_result < 0)
        {
            onenet_schedule_mqtt_retry(now_ms, "TLS handshake or server certificate validation failed");
        }
        else if (connect_result == 1)
        {
            ESP_LOGI(TAG, "TLS established; OneNET CA and certificate name verified");
            (void)onenet_send_connect_request(now_ms);
        }
        else
        {
            onenet_schedule_mqtt_retry(now_ms, "Unexpected TLS handshake status");
        }
        return;
    }

    if (g_mqtt_state == ONENET_MQTT_WAIT_CONNACK)
    {
        rx_len = onenet_receive_stream();
        if (rx_len < 0)
        {
            onenet_schedule_mqtt_retry(now_ms, "MQTT CONNACK receive failed");
            return;
        }

        frame_result = onenet_extract_mqtt_frame(&frame_length);
        if (frame_result < 0)
        {
            onenet_schedule_mqtt_retry(now_ms, "malformed MQTT CONNACK stream");
        }
        else if (frame_result > 0)
        {
            if (MQTT_UnPacketRecvLength(g_rx_frame, frame_length) == MQTT_PKT_CONNACK &&
                MQTT_UnPacketConnectAckLength(g_rx_frame, frame_length) == 0)
            {
                OneNet_Ready = 1U;
                g_mqtt_state = ONENET_MQTT_READY;
                g_mqtt_retry_count = 0U;
                g_last_ping = now_ms;
                ESP_LOGI(TAG, "OneNET MQTT connected");
                OneNET_Subscribe();
                return;
            }
            onenet_schedule_mqtt_retry(now_ms, "MQTT CONNACK rejected");
        }
        else if (onenet_time_reached(now_ms, g_connack_deadline_ms))
        {
            onenet_schedule_mqtt_retry(now_ms, "MQTT CONNACK timeout");
        }
    }
}

static void onenet_network_task(void *arg)
{
    uint32_t last_upload_ms = 0U;

    (void)arg;
    for (;;)
    {
        OneNet_ProcessLoop();

        /* 上传由网络任务负责，重连不重置已发帧序号。 */
        if ((uint32_t)(onenet_now_ms() - last_upload_ms) >= 5000U)
        {
            last_upload_ms = onenet_now_ms();
            OneNet_SendData();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void OneNet_Init(const char *wifi_ssid, const char *wifi_password)
{
    esp_err_t result;

    if (g_network_initialized || wifi_ssid == NULL || wifi_password == NULL)
    {
        return;
    }
    if (wifi_ssid[0] == '\0')
    {
        ESP_LOGI(TAG, "Wi-Fi not configured; local LoRa mode");
        return;
    }

    strncpy(g_wifi_ssid, wifi_ssid, sizeof(g_wifi_ssid) - 1U);
    strncpy(g_wifi_password, wifi_password, sizeof(g_wifi_password) - 1U);

    result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        result = nvs_flash_erase();
        if (result == ESP_OK)
        {
            result = nvs_flash_init();
        }
    }
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(result));
        return;
    }

    wifi_init_sta(g_wifi_ssid, g_wifi_password);
    g_network_initialized = true;
    g_mqtt_retry_due_ms = onenet_now_ms();

    if (xTaskCreate(onenet_network_task, "onenet_net", 8192U, NULL, 4U, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create OneNET network task");
        g_network_initialized = false;
    }
}

void OneNet_SendData(void)
{
    static bool has_uploaded_sequence = false;
    static uint32_t last_uploaded_sequence = 0U;
    static uint32_t last_uploaded_uptime = 0U;
    static uint8_t last_uploaded_protocol = 0U;
    lora_receiver_snapshot_t snapshot;
    MQTT_PACKET_STRUCTURE mqtt_packet = {NULL, 0, 0, 0};
    char json[384];
    float temperature;
    float humidity;
    float pressure;
    float up_score;
    uint16_t voc_index;
    uint8_t ai_state;
    int16_t lora_rssi;
    bool ai_valid;
    bool event_packet;
    int json_length;
    int send_result;

    if (!OneNet_Ready || g_tls == NULL)
    {
        return;
    }

    lora_receiver_get_snapshot(&snapshot);
    if (!snapshot.telemetry_valid || !snapshot.telemetry_fresh ||
        ((snapshot.telemetry.flags & TELEMETRY_FLAG_BME680_VALID) == 0U) ||
        ((snapshot.telemetry.flags & TELEMETRY_FLAG_SGP40_VALID) == 0U))
    {
        return;
    }

    if (has_uploaded_sequence &&
        snapshot.telemetry.sequence == last_uploaded_sequence &&
        snapshot.telemetry.uptime_ms == last_uploaded_uptime &&
        snapshot.telemetry.protocol_version == last_uploaded_protocol)
    {
        return;
    }

    temperature = (float)snapshot.telemetry.temperature_x10 / 10.0f;
    humidity = (float)snapshot.telemetry.humidity_x10 / 10.0f;
    pressure = (float)snapshot.telemetry.pressure_x10 / 10.0f;
    /* 仅旧 AQ1 使用 warning_score 和 ai_state 表示预测分数与方向。 */
    up_score = (float)snapshot.telemetry.warning_x1000 / 1000.0f;
    voc_index = snapshot.telemetry.voc_index;
    ai_state = snapshot.telemetry.ai_state;
    lora_rssi = snapshot.rssi_dbm;
    event_packet = snapshot.telemetry.protocol_version == 2U;
    /* AQ2 的第 2 位表示事件就绪，而非预测分数有效。 */
    ai_valid = !event_packet && (snapshot.telemetry.flags & TELEMETRY_FLAG_AI_VALID) != 0U;

    /* 上传前按 OneNET 物模型检查字段范围。 */
    if (temperature < 0.0f || temperature > 50.0f ||
        humidity < 0.0f || humidity > 100.0f ||
        pressure < 300.0f || pressure > 1100.0f ||
        voc_index > 500U || (!event_packet && ai_state > 5U) ||
        lora_rssi < -150 || lora_rssi > 0 ||
        (ai_valid && (up_score < 0.0f || up_score > 1.0f)))
    {
        ESP_LOGW(TAG,
                 "Skip seq=%lu: telemetry is outside OneNET model range",
                 (unsigned long)snapshot.telemetry.sequence);
        return;
    }

    if (event_packet)
    {
        /* OneNET 未登记 AQ2 事件字段，仅上传环境数据。 */
        json_length = snprintf(
            json, sizeof(json),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{\"Temp\":{\"value\":%.1f},\"humidity\":{\"value\":%.1f},\"pressure\":{\"value\":%.1f},\"voc_index\":{\"value\":%u},\"lora_rssi\":{\"value\":%d}}}",
            (unsigned long)snapshot.telemetry.sequence,
            (double)temperature, (double)humidity, (double)pressure,
            (unsigned int)voc_index, (int)lora_rssi);
    }
    else if (ai_valid)
    {
        json_length = snprintf(
            json,
            sizeof(json),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{\"Temp\":{\"value\":%.1f},\"humidity\":{\"value\":%.1f},\"pressure\":{\"value\":%.1f},\"voc_index\":{\"value\":%u},\"ai_state\":{\"value\":%u},\"warning_score\":{\"value\":%.3f},\"lora_rssi\":{\"value\":%d}}}",
            (unsigned long)snapshot.telemetry.sequence,
            (double)temperature,
            (double)humidity,
            (double)pressure,
            (unsigned int)voc_index,
            (unsigned int)ai_state,
            (double)up_score,
            (int)lora_rssi);
    }
    else
    {
        json_length = snprintf(
            json,
            sizeof(json),
            "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{\"Temp\":{\"value\":%.1f},\"humidity\":{\"value\":%.1f},\"pressure\":{\"value\":%.1f},\"voc_index\":{\"value\":%u},\"ai_state\":{\"value\":%u},\"lora_rssi\":{\"value\":%d}}}",
            (unsigned long)snapshot.telemetry.sequence,
            (double)temperature,
            (double)humidity,
            (double)pressure,
            (unsigned int)voc_index,
            (unsigned int)ai_state,
            (int)lora_rssi);
    }

    if (json_length <= 0 || json_length >= (int)sizeof(json))
    {
        ESP_LOGE(TAG, "Failed to format telemetry JSON");
        return;
    }

    if (MQTT_PacketSaveData(PROID,
                            DEVICE_NAME,
                            (uint16_t)json_length,
                            NULL,
                            &mqtt_packet) != 0)
    {
        ESP_LOGE(TAG, "Failed to build OneNET MQTT packet");
        return;
    }

    if (mqtt_packet._data == NULL ||
        mqtt_packet._len + (uint32_t)json_length > mqtt_packet._size)
    {
        ESP_LOGE(TAG, "OneNET MQTT packet buffer is too small");
        MQTT_DeleteBuffer(&mqtt_packet);
        return;
    }

    memcpy(&mqtt_packet._data[mqtt_packet._len], json, (size_t)json_length);
    mqtt_packet._len += (uint32_t)json_length;

    send_result = tls_send(mqtt_packet._data, (int)mqtt_packet._len);
    MQTT_DeleteBuffer(&mqtt_packet);

    if (send_result < 0)
    {
        onenet_schedule_mqtt_retry(onenet_now_ms(), "telemetry send failed");
        return;
    }
    if (send_result == 0)
    {
        return;
    }

    last_uploaded_sequence = snapshot.telemetry.sequence;
    last_uploaded_uptime = snapshot.telemetry.uptime_ms;
    last_uploaded_protocol = snapshot.telemetry.protocol_version;
    has_uploaded_sequence = true;
    if (event_packet)
    {
        ESP_LOGI(TAG, "Uploaded AQ2 environment seq=%lu; event state=%u ready=%u episode=%lu (local only)",
                 (unsigned long)snapshot.telemetry.sequence,
                 (unsigned)snapshot.telemetry.event_state,
                 (unsigned)((snapshot.telemetry.flags & TELEMETRY_FLAG_EVENT_READY) != 0U),
                 (unsigned long)snapshot.telemetry.event_episode_id);
        return;
    }
    ESP_LOGI(TAG,
             "Uploaded seq=%lu: T=%.1f H=%.1f P=%.1f VOC=%u AI=%u Score=%s RSSI=%d",
             (unsigned long)snapshot.telemetry.sequence,
             (double)temperature,
             (double)humidity,
             (double)pressure,
             (unsigned int)voc_index,
             (unsigned int)ai_state,
             ai_valid ? "valid" : "waiting",
             (int)lora_rssi);
}

/* 向指定主题发布消息。 */
void OneNET_Publish(const char *topic, const char *msg)
{
    MQTT_PACKET_STRUCTURE mqtt_packet = {NULL, 0, 0, 0};
    int send_result;

    if (!OneNet_Ready || (g_tls == NULL) || (topic == NULL) || (msg == NULL))
        return;

    if (MQTT_PacketPublish(MQTT_PUBLISH_ID, topic, msg, strlen(msg),
                           MQTT_QOS_LEVEL0, 0, 1, &mqtt_packet) == 0)
    {
        send_result = tls_send(mqtt_packet._data, (int)mqtt_packet._len);
        MQTT_DeleteBuffer(&mqtt_packet);
        if (send_result < 0)
        {
            onenet_schedule_mqtt_retry(onenet_now_ms(), "publish send failed");
        }
    }
}

/* 订阅属性设置消息。 */
void OneNET_Subscribe(void)
{
    MQTT_PACKET_STRUCTURE mqtt_packet = {NULL, 0, 0, 0};
    char topic_buf[58];
    const char *topic = topic_buf;
    int send_result;

    if (!OneNet_Ready || (g_tls == NULL))
        return;

    snprintf(topic_buf, sizeof(topic_buf), "$sys/%s/%s/thing/property/set", PROID, DEVICE_NAME);

    if (MQTT_PacketSubscribe(MQTT_SUBSCRIBE_ID, MQTT_QOS_LEVEL0, &topic, 1, &mqtt_packet) == 0)
    {
        send_result = tls_send(mqtt_packet._data, (int)mqtt_packet._len);
        MQTT_DeleteBuffer(&mqtt_packet);
        if (send_result <= 0)
        {
            onenet_schedule_mqtt_retry(onenet_now_ms(), "subscribe send failed");
        }
    }
}

/* 处理一帧完整的平台消息。 */
void OneNet_RevProFrame(const unsigned char *cmd, size_t frame_length)
{
    char *req_payload = NULL;
    char *cmdid_topic = NULL;

    char reply_topic[128];
    char reply_msg[128];
    char reply_id[96];

    unsigned short topic_len = 0;
    unsigned short req_len = 0;

    unsigned char qos = 0;
    unsigned short pkt_id = 0;

    unsigned char type = 0;

    uint8 result;

    cJSON *raw_json, *params_json;

    type = MQTT_UnPacketRecvLength(cmd, frame_length);
    switch (type)
    {
        case MQTT_PKT_PUBLISH:
            result = MQTT_UnPacketPublishLength(cmd, frame_length, &cmdid_topic, &topic_len,
                                               &req_payload, &req_len, &qos, &pkt_id);
            if (result == 0 && Json_GuardValid(req_payload, req_len))
            {
                raw_json = cJSON_Parse(req_payload);
                if (raw_json)
                {
                    params_json = cJSON_GetObjectItem(raw_json, "params");
                    if (params_json)
                    {
                        ESP_LOGI(TAG, "Received OneNET property-set command");
                    }

                    cJSON *id_json = cJSON_GetObjectItem(raw_json, "id");
                    const char *request_id = (id_json && (id_json->type == cJSON_String) &&
                                              id_json->valuestring) ? id_json->valuestring : "";
                    bool id_valid = Json_EscapeString(request_id, reply_id, sizeof(reply_id));
                    int reply_topic_len = snprintf(reply_topic, sizeof(reply_topic),
                                             "$sys/%s/%s/thing/property/set_reply",
                                             PROID, DEVICE_NAME);
                    int message_len = snprintf(reply_msg, sizeof(reply_msg),
                                               "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}",
                                               reply_id);

                    cJSON_Delete(raw_json);
                    if (id_valid && (reply_topic_len > 0) && ((size_t)reply_topic_len < sizeof(reply_topic)) &&
                        (message_len > 0) && ((size_t)message_len < sizeof(reply_msg)))
                    {
                        OneNET_Publish(reply_topic, reply_msg);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "OneNET reply exceeds local buffer");
                    }
                }
            }
            break;

        case MQTT_PKT_PUBACK:
            (void)MQTT_UnPacketPublishAckLength(cmd, frame_length);
            break;

        case MQTT_PKT_SUBACK:
            if (MQTT_UnPacketSubscribeLength(cmd, frame_length) != 0)
                ESP_LOGW(TAG, "OneNET command subscription was rejected");
            break;

        default:
            break;
    }

    /* 解包失败时指针已清空。 */
    MQTT_FreeBuffer(cmdid_topic);
    MQTT_FreeBuffer(req_payload);
}

/* 在网络任务中轮询 MQTT 数据和重连状态。 */
void OneNet_ProcessLoop(void)
{
    const uint32_t now_ms = onenet_now_ms();
    int rx_len;
    int frame_result;
    size_t frame_length;

    if (g_socket_close_pending || g_mqtt_state != ONENET_MQTT_READY ||
        !g_wifi_connected || !OneNet_Ready || g_tls == NULL)
    {
        onenet_process_connection(now_ms);
        return;
    }

    if ((uint32_t)(now_ms - g_last_ping) >= (MQTT_KEEPALIVE_SEC * 1000U) / 2U)
    {
        MQTT_PACKET_STRUCTURE ping_packet = {NULL, 0, 0, 0};
        if (MQTT_PacketPing(&ping_packet) == 0)
        {
            const int send_result = tls_send(ping_packet._data, (int)ping_packet._len);
            MQTT_DeleteBuffer(&ping_packet);
            if (send_result <= 0)
            {
                onenet_schedule_mqtt_retry(now_ms, "PING send failed");
                return;
            }
        }
        g_last_ping = now_ms;
    }

    rx_len = onenet_receive_stream();
    if (rx_len < 0)
    {
        onenet_schedule_mqtt_retry(now_ms,
                                  (rx_len == -2) ? "broker closed connection" : "socket receive failed");
        return;
    }

    do
    {
        frame_result = onenet_extract_mqtt_frame(&frame_length);
        if (frame_result < 0)
        {
            onenet_schedule_mqtt_retry(now_ms, "malformed MQTT stream");
            return;
        }
        if (frame_result > 0)
        {
            OneNet_RevProFrame(g_rx_frame, frame_length);
        }
    } while (frame_result > 0);

}
