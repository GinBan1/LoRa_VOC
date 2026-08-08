//ESP-IDF
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

//协议文件
#include "onenet.h"
#include "MQTT.h"

//算法
#include "base64.h"
#include "hmac_sha1.h"

//C库
#include <string.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "cJSON.h"
#include "lora_receiver.h"
#include "sdkconfig.h"


#define PROID                    CONFIG_AIR_GATEWAY_ONENET_PRODUCT_ID
#define ACCESS_KEY               CONFIG_AIR_GATEWAY_ONENET_ACCESS_KEY
#define DEVICE_NAME              CONFIG_AIR_GATEWAY_ONENET_DEVICE_NAME
#define METHOD                   "sha1"
#define ET                       ((unsigned long)CONFIG_AIR_GATEWAY_ONENET_TOKEN_EXPIRY)


#define TAG                      "OneNET"
#define MQTT_PORT                1883
#define MQTT_HOST                "mqtts.heclouds.com"

#define RX_BUF_SIZE              1024
#define ONENET_TCP_TIMEOUT_MS     10000U
#define ONENET_CONNACK_TIMEOUT_MS 5000U
#define ONENET_RETRY_MAX_MS      30000U

// MQTT 心跳间隔（秒），OneNET 要求不大于300s
#define MQTT_KEEPALIVE_SEC        240
static uint32_t g_last_ping = 0;

/*---------------------------------------------------------------------------*/
/* 全局变量                                                                   */
/*---------------------------------------------------------------------------*/
volatile uint8_t OneNet_Ready = 0;
static int g_sock = -1;
static unsigned char g_rx_buf[RX_BUF_SIZE + 1U];
static unsigned char g_rx_frame[RX_BUF_SIZE + 1U];
static size_t g_rx_used;

typedef enum
{
    ONENET_MQTT_IDLE = 0,
    ONENET_MQTT_TCP_CONNECTING,
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
static uint32_t g_tcp_deadline_ms;
static uint32_t g_connack_deadline_ms;
static onenet_mqtt_state_t g_mqtt_state = ONENET_MQTT_IDLE;

static uint32_t onenet_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Signed subtraction keeps all due-time checks correct across uint32_t wrap. */
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
    if (g_sock >= 0)
    {
        close(g_sock);
        g_sock = -1;
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

// 用于WiFi连接的FreeRTOS事件组
/*---------------------------------------------------------------------------*/
/* WiFi 事件处理                                                               */
/*---------------------------------------------------------------------------*/
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
        // WiFi 已初始化，直接等待重连
        ESP_LOGI(TAG, "Waiting for WiFi reconnect...");
        /* Reconnect attempts are driven asynchronously by the network task. */
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

    ESP_LOGI(TAG, "WiFi Init Done, Connecting...");

    // 等待连接成功
    /* Do not wait here: the event handler reports the eventual IP address. */
}

/*===========================================================================*/
/* Token生成                                                                   */
/*===========================================================================*/

// URL 编码表（不编码数字和字母，其余按百分比编码）
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

    // 对 sign 做 URL 编码
    memset(sign_enc, 0, sizeof(sign_enc));
    url_encode_buf(sign_enc, sizeof(sign_enc), sign_buf);

    snprintf(token, token_len,
             "version=2018-10-31&res=products%%2F%s%%2Fdevices%%2F%s&et=%lu&method=%s&sign=%s",
             PROID, DEVICE_NAME, ET, METHOD, sign_enc);
}

/*===========================================================================*/
/* Socket TCP 通信（替代ESP8266 AT指令）                                        */
/*===========================================================================*/
/* Returns 1 when connected now, 0 while TCP is connecting, and -1 on error. */
static int socket_connect_start(const char *host, uint16_t port, int *sock_out)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct hostent *he;
    struct sockaddr_in dest = {0};

    *sock_out = -1;
    if (sock < 0)
    {
        return -1;
    }

    /* DNS can take time, but this function only runs in the dedicated network
     * task so it never stalls LoRa reception or OLED refreshes. */
    he = gethostbyname(host);
    if (he == NULL)
    {
        close(sock);
        return -1;
    }

    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    memcpy(&dest.sin_addr, he->h_addr, he->h_length);

    if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0)
    {
        close(sock);
        return -1;
    }

    *sock_out = sock;
    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) == 0)
    {
        return 1;
    }

    if (errno == EINPROGRESS || errno == EALREADY)
    {
        return 0;
    }

    close(sock);
    *sock_out = -1;
    return -1;
}

static int socket_connect_complete(int sock)
{
    int socket_error = 0;
    socklen_t socket_error_len = sizeof(socket_error);
    struct timeval timeout = {0};
    fd_set write_fds;
    fd_set error_fds;
    int select_result;

    FD_ZERO(&write_fds);
    FD_ZERO(&error_fds);
    FD_SET(sock, &write_fds);
    FD_SET(sock, &error_fds);
    select_result = select(sock + 1, NULL, &write_fds, &error_fds, &timeout);
    if (select_result == 0)
    {
        return 0;
    }
    if (select_result < 0)
    {
        return -1;
    }

    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len) < 0)
    {
        return -1;
    }

    return (socket_error == 0) ? 1 : -1;
}

/* Send one complete MQTT frame. This function is only called by the dedicated
 * network task, so a short retry window cannot delay LoRa or OLED work. */
static int socket_send(int sock, const unsigned char *data, int len)
{
    int sent = 0;
    const uint32_t deadline_ms = onenet_now_ms() + 250U;

    if (data == NULL || len <= 0)
    {
        return -1;
    }

    while (sent < len)
    {
        const int result = send(sock, data + sent, len - sent, 0);

        if (result > 0)
        {
            sent += result;
            continue;
        }
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
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

/* >0 bytes received, 0 no data, -2 orderly peer close, -1 socket error. */
static int socket_recv(int sock, unsigned char *buf, int len)
{
    const int result = recv(sock, buf, len, 0);

    if (result > 0)
    {
        return result;
    }
    if (result == 0)
    {
        return -2;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
    {
        return 0;
    }

    return -1;
}

/* Append currently available TCP data to the MQTT stream accumulator.
 * MQTT is a byte stream: one recv() may contain half a packet or many packets. */
static int onenet_receive_stream(void)
{
    int result;

    if (g_rx_used >= RX_BUF_SIZE)
    {
        return -1;
    }

    result = socket_recv(g_sock,
                         &g_rx_buf[g_rx_used],
                         (int)(RX_BUF_SIZE - g_rx_used));
    if (result > 0)
    {
        g_rx_used += (size_t)result;
        g_rx_buf[g_rx_used] = '\0';
    }
    return result;
}

/* 1: a complete frame was copied, 0: more bytes are required, -1: malformed. */
static int onenet_extract_mqtt_frame(size_t *frame_length)
{
    uint32_t remaining_length = 0U;
    uint32_t multiplier = 1U;
    size_t header_length = 1U;
    size_t index;
    size_t total_length;
    uint8_t type;

    if (frame_length == NULL)
    {
        return -1;
    }
    *frame_length = 0U;

    if (g_rx_used < 2U)
    {
        return 0;
    }

    type = g_rx_buf[0] >> 4;
    if (type < MQTT_PKT_CONNECT || type > MQTT_PKT_DISCONNECT)
    {
        return -1;
    }

    for (index = 1U; index <= 4U; ++index)
    {
        uint8_t encoded;

        if (index >= g_rx_used)
        {
            return 0;
        }

        encoded = g_rx_buf[index];
        remaining_length += (uint32_t)(encoded & 0x7FU) * multiplier;
        ++header_length;

        if ((encoded & 0x80U) == 0U)
        {
            break;
        }
        if (index == 4U)
        {
            return -1;
        }
        multiplier *= 128U;
    }

    if (remaining_length > (uint32_t)(RX_BUF_SIZE - header_length))
    {
        return -1;
    }

    total_length = header_length + (size_t)remaining_length;
    if (g_rx_used < total_length)
    {
        return 0;
    }

    memcpy(g_rx_frame, g_rx_buf, total_length);
    g_rx_frame[total_length] = '\0';
    g_rx_used -= total_length;
    if (g_rx_used > 0U)
    {
        memmove(g_rx_buf, &g_rx_buf[total_length], g_rx_used);
    }
    g_rx_buf[g_rx_used] = '\0';
    *frame_length = total_length;
    return 1;
}

//==========================================================
//    函数名称：    OneNet_DevLink
//
//    函数功能：    与onenet创建连接
//
//    入口参数：    wifi_ssid：WiFi名称
//                wifi_password：WiFi密码
//
//    返回参数：    1-成功    0-失败
//
//    说明：        先连WiFi再连MQTT Broker
//==========================================================
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

    send_result = socket_send(g_sock, mqtt_packet._data, (int)mqtt_packet._len);
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

        ESP_LOGI(TAG, "Connecting to MQTT broker...");
        connect_result = socket_connect_start(MQTT_HOST, MQTT_PORT, &g_sock);
        if (connect_result < 0)
        {
            onenet_schedule_mqtt_retry(now_ms, "TCP connect start failed");
        }
        else if (connect_result == 0)
        {
            g_mqtt_state = ONENET_MQTT_TCP_CONNECTING;
            g_tcp_deadline_ms = now_ms + ONENET_TCP_TIMEOUT_MS;
        }
        else
        {
            (void)onenet_send_connect_request(now_ms);
        }
        return;
    }

    if (g_mqtt_state == ONENET_MQTT_TCP_CONNECTING)
    {
        connect_result = socket_connect_complete(g_sock);
        if (connect_result < 0)
        {
            onenet_schedule_mqtt_retry(now_ms, "TCP connect failed");
        }
        else if (connect_result > 0)
        {
            (void)onenet_send_connect_request(now_ms);
        }
        else if (onenet_time_reached(now_ms, g_tcp_deadline_ms))
        {
            onenet_schedule_mqtt_retry(now_ms, "TCP connect timeout");
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
            (void)frame_length;
            if (MQTT_UnPacketRecv(g_rx_frame) == MQTT_PKT_CONNACK &&
                MQTT_UnPacketConnectAck(g_rx_frame) == 0)
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

        /* Upload only from this task.  This keeps socket ownership out of
         * the scheduler and preserves the sequence watermark across reconnects. */
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

    /* wifi_init_sta() performs the one-time ESP-IDF setup but no longer waits. */
    wifi_init_sta(g_wifi_ssid, g_wifi_password);
    g_network_initialized = true;
    g_mqtt_retry_due_ms = onenet_now_ms();

    if (xTaskCreate(onenet_network_task, "onenet_net", 6144U, NULL, 4U, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create OneNET network task");
        g_network_initialized = false;
    }
}

void OneNet_SendData(void)
{
    static bool has_uploaded_sequence = false;
    static uint32_t last_uploaded_sequence = 0U;
    lora_receiver_snapshot_t snapshot;
    MQTT_PACKET_STRUCTURE mqtt_packet = {NULL, 0, 0, 0};
    char json[384];
    float temperature;
    float humidity;
    float pressure;
    float warning_score;
    uint16_t voc_index;
    uint8_t ai_state;
    int16_t lora_rssi;
    bool ai_valid;
    int json_length;
    int send_result;

    if (!OneNet_Ready || g_sock < 0)
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
        snapshot.telemetry.sequence == last_uploaded_sequence)
    {
        return;
    }

    temperature = (float)snapshot.telemetry.temperature_x10 / 10.0f;
    humidity = (float)snapshot.telemetry.humidity_x10 / 10.0f;
    pressure = (float)snapshot.telemetry.pressure_x10 / 10.0f;
    warning_score = (float)snapshot.telemetry.warning_x1000 / 1000.0f;
    voc_index = snapshot.telemetry.voc_index;
    ai_state = snapshot.telemetry.ai_state;
    lora_rssi = snapshot.rssi_dbm;
    ai_valid = (snapshot.telemetry.flags & TELEMETRY_FLAG_AI_VALID) != 0U;

    /* Validate every value against the configured OneNET thing model. */
    if (temperature < 0.0f || temperature > 50.0f ||
        humidity < 0.0f || humidity > 100.0f ||
        pressure < 300.0f || pressure > 1100.0f ||
        voc_index > 500U || ai_state > 5U ||
        lora_rssi < -150 || lora_rssi > 0 ||
        (ai_valid && (warning_score < 0.0f || warning_score > 1.0f)))
    {
        ESP_LOGW(TAG,
                 "Skip seq=%lu: telemetry is outside OneNET model range",
                 (unsigned long)snapshot.telemetry.sequence);
        return;
    }

    if (ai_valid)
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
            (double)warning_score,
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

    send_result = socket_send(g_sock, mqtt_packet._data, (int)mqtt_packet._len);
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
    has_uploaded_sequence = true;
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

//==========================================================
//    函数名称：    OneNET_Publish
//
//    函数功能：    发布消息
//
//    入口参数：    topic：发布的主题
//                msg：消息内容
//
//    返回参数：    无
//
//    说明：
//==========================================================
void OneNET_Publish(const char *topic, const char *msg)
{
    MQTT_PACKET_STRUCTURE mqtt_packet = {NULL, 0, 0, 0};
    int send_result;

    if (!OneNet_Ready || (g_sock < 0) || (topic == NULL) || (msg == NULL))
        return;

    printf("Publish Topic: %s, Msg: %s\r\n", topic, msg);

    if (MQTT_PacketPublish(MQTT_PUBLISH_ID, topic, msg, strlen(msg),
                           MQTT_QOS_LEVEL0, 0, 1, &mqtt_packet) == 0)
    {
        send_result = socket_send(g_sock, mqtt_packet._data, (int)mqtt_packet._len);
        MQTT_DeleteBuffer(&mqtt_packet);
        if (send_result < 0)
        {
            onenet_schedule_mqtt_retry(onenet_now_ms(), "publish send failed");
        }
    }
}

//==========================================================
//    函数名称：    OneNET_Subscribe
//
//    函数功能：    订阅属性设置下发
//
//    入口参数：    无
//
//    返回参数：    无
//
//    说明：
//==========================================================
void OneNET_Subscribe(void)
{
    MQTT_PACKET_STRUCTURE mqtt_packet = {NULL, 0, 0, 0};
    char topic_buf[58];
    const char *topic = topic_buf;
    int send_result;

    if (!OneNet_Ready || (g_sock < 0))
        return;

    snprintf(topic_buf, sizeof(topic_buf), "$sys/%s/%s/thing/property/set", PROID, DEVICE_NAME);

    if (MQTT_PacketSubscribe(MQTT_SUBSCRIBE_ID, MQTT_QOS_LEVEL0, &topic, 1, &mqtt_packet) == 0)
    {
        send_result = socket_send(g_sock, mqtt_packet._data, (int)mqtt_packet._len);
        MQTT_DeleteBuffer(&mqtt_packet);
        if (send_result <= 0)
        {
            onenet_schedule_mqtt_retry(onenet_now_ms(), "subscribe send failed");
        }
    }
}

//==========================================================
//    函数名称：    OneNet_RevPro
//
//    函数功能：    平台返回数据检测
//
//    入口参数：    cmd：平台返回的数据
//
//    返回参数：    无
//
//    说明：        与原STM32 onenet.c 保持一致的逻辑
//==========================================================
void OneNet_RevPro(unsigned char *cmd)
{
    char *req_payload = NULL;
    char *cmdid_topic = NULL;

    char reply_topic[128];
    char reply_msg[128];

    unsigned short topic_len = 0;
    unsigned short req_len = 0;

    unsigned char qos = 0;
    static unsigned short pkt_id = 0;

    unsigned char type = 0;

    short result = 0;

    cJSON *raw_json, *params_json;

    type = MQTT_UnPacketRecv(cmd);
    switch (type)
    {
        case MQTT_PKT_PUBLISH:
            result = MQTT_UnPacketPublish(cmd, &cmdid_topic, &topic_len,
                                          &req_payload, &req_len, &qos, &pkt_id);
            if (result == 0)
            {
                printf("OneNET Recv: %s\r\n", req_payload);

                raw_json = cJSON_Parse(req_payload);
                if (raw_json)
                {
                    params_json = cJSON_GetObjectItem(raw_json, "params");
                    if (params_json)
                    {
                        char *params_text = cJSON_PrintUnformatted(params_json);
                        if (params_text)
                        {
                            printf("Params: %s\r\n", params_text);
                            free(params_text);
                        }
                    }

                    cJSON *id_json = cJSON_GetObjectItem(raw_json, "id");
                    const char *request_id = (id_json && (id_json->type == cJSON_String) &&
                                              id_json->valuestring) ? id_json->valuestring : "";
                    int topic_len = snprintf(reply_topic, sizeof(reply_topic),
                                             "$sys/%s/%s/thing/property/set_reply",
                                             PROID, DEVICE_NAME);
                    int message_len = snprintf(reply_msg, sizeof(reply_msg),
                                               "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}",
                                               request_id);

                    cJSON_Delete(raw_json);
                    if ((topic_len > 0) && ((size_t)topic_len < sizeof(reply_topic)) &&
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
            if (MQTT_UnPacketPublishAck(cmd) == 0)
                printf("Tips:    MQTT Publish Send OK\r\n");
            break;

        case MQTT_PKT_SUBACK:
            if (MQTT_UnPacketSubscribe(cmd) == 0)
                printf("Tips:    MQTT Subscribe OK\r\n");
            else
                printf("Tips:    MQTT Subscribe Err\r\n");
            break;

        default:
            result = -1;
            break;
    }

    if (result == -1) return;

    if (type == MQTT_PKT_CMD || type == MQTT_PKT_PUBLISH)
    {
        MQTT_FreeBuffer(cmdid_topic);
        MQTT_FreeBuffer(req_payload);
    }
}

//==========================================================
//    函数名称：    OneNet_ProcessLoop
//
//    函数功能：    非阻塞轮询MQTT数据（替代ESP8266 DMA空闲中断）
//
//    入口参数：    无
//
//    返回参数：    无
//
//    说明：        需在主循环中定期调用，例如每100ms
//==========================================================
void OneNet_ProcessLoop(void)
{
    const uint32_t now_ms = onenet_now_ms();
    int rx_len;
    int frame_result;
    size_t frame_length;

    if (g_socket_close_pending || g_mqtt_state != ONENET_MQTT_READY ||
        !g_wifi_connected || !OneNet_Ready || g_sock < 0)
    {
        onenet_process_connection(now_ms);
        return;
    }

    if ((uint32_t)(now_ms - g_last_ping) >= (MQTT_KEEPALIVE_SEC * 1000U) / 2U)
    {
        MQTT_PACKET_STRUCTURE ping_packet = {NULL, 0, 0, 0};
        if (MQTT_PacketPing(&ping_packet) == 0)
        {
            const int send_result = socket_send(g_sock, ping_packet._data, (int)ping_packet._len);
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
            (void)frame_length;
            OneNet_RevPro(g_rx_frame);
        }
    } while (frame_result > 0);

}
