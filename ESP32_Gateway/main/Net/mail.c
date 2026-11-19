#include "mail.h"
#include "smtp_client.h"
#include "mail_test.h"
#include "sdkconfig.h"
#include "esp_log.h"

#if defined(CONFIG_AIR_GATEWAY_MAIL_ENABLE) && CONFIG_AIR_GATEWAY_MAIL_ENABLE
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "alarm_app.h"
#include "boot_diag.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"

#define MAIL_EXPIRY_MS       300000U
#define MAIL_CONNECT_MS       15000
#define MAIL_ATTEMPTS             3U
#define MAIL_WORKER_STACK      8192

typedef struct {
    alarm_event_t event;
    uint32_t due_ms;
    uint32_t last_id;
    uint8_t attempts;
    bool pending;
    bool have_id;
} mail_slot_t;

typedef struct {
    esp_tls_t *tls;
    alarm_event_t event;
    uint32_t test_id;
} mail_transport_t;

static const char *TAG = "Mail";
static SemaphoreHandle_t g_mail_lock;
static mail_slot_t g_mail_slots[ALARM_CHANNEL_COUNT];
static uint32_t g_mail_boot_id;
static bool g_mail_ready;
static mail_test_state_t g_mail_test;

static bool Mail_AlarmPending(void)
{
    for (unsigned i = 0; i < ALARM_CHANNEL_COUNT; ++i)
        if (g_mail_slots[i].pending) return true;
    return false;
}

static uint32_t Mail_Now(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool Mail_EventValid(const alarm_event_t *event)
{
    return event && event->channel < ALARM_CHANNEL_COUNT && event->id != 0 &&
           event->voc_index <= 500 && event->up_score <= 1000 && event->confidence <= 1000;
}

static bool Mail_ConfigValid(void)
{
    size_t n = strlen(CONFIG_AIR_GATEWAY_MAIL_AUTH_CODE);
    if (!SMTP_AddressValid(CONFIG_AIR_GATEWAY_MAIL_FROM) ||
        !SMTP_AddressValid(CONFIG_AIR_GATEWAY_MAIL_TO) || n == 0 || n > 128) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)CONFIG_AIR_GATEWAY_MAIL_AUTH_CODE[i];
        if (c <= 32 || c > 126) return false;
    }
    return true;
}

static bool Mail_NetworkReady(void)
{
    wifi_ap_record_t ap;
    return time(NULL) >= (time_t)1704067200 && esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

static int Mail_Read(void *context, char *buffer, size_t size)
{
    mail_transport_t *transport = context;
    int n = (int)esp_tls_conn_read(transport->tls, buffer, size);
    if (n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE) return -2;
    return n < 0 ? -1 : n;
}

static int Mail_Write(void *context, const char *buffer, size_t size)
{
    mail_transport_t *transport = context;
    int n = (int)esp_tls_conn_write(transport->tls, buffer, size);
    if (n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE) return -2;
    return n < 0 ? -1 : n;
}

static uint32_t Mail_Clock(void *context)
{
    (void)context;
    return Mail_Now();
}

static void Mail_Wait(void *context, uint32_t milliseconds)
{
    (void)context;
    TickType_t delay = pdMS_TO_TICKS(milliseconds);
    vTaskDelay(delay ? delay : 1);
}

static bool Mail_Current(void *context)
{
    mail_transport_t *transport = context;
    if (transport->test_id) {
        xSemaphoreTake(g_mail_lock, portMAX_DELAY);
        bool current = Mail_TestCurrent(&g_mail_test, transport->test_id, Mail_Now()) &&
                       !Mail_AlarmPending();
        xSemaphoreGive(g_mail_lock);
        return current;
    }
    return (uint32_t)(Mail_Now() - transport->event.timestamp_ms) < MAIL_EXPIRY_MS &&
           Alarm_App_EventCurrent(&transport->event);
}

static bool Mail_Encode(const char *source, char *output, size_t capacity)
{
    size_t size = 0;
    if (mbedtls_base64_encode((unsigned char *)output, capacity - 1, &size,
                             (const unsigned char *)source, strlen(source)) != 0 || size >= capacity)
        return false;
    output[size] = 0;
    return true;
}

static void Mail_Resources(const char *phase)
{
    ESP_LOGI(TAG, "SMTP %s: internal free=%lu largest=%lu stack minimum=%lu", phase,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned long)uxTaskGetStackHighWaterMark(NULL));
}

/* 生成邮件文案，不改变触发条件。 */
static bool Mail_FormatCopy(const alarm_event_t *event, uint32_t test_id,
                            char *subject, size_t subject_size, char *body, size_t body_size)
{
    if (!subject || !subject_size || !body || !body_size) return false;
    const char *title;
    int n;
    if (test_id) {
        title = "网关邮件测试";
        n = snprintf(body, body_size, "这是一封网关测试邮件。\r\n");
    } else if (!event) {
        return false;
    } else if (event->source_protocol == 2U) {
        if (event->channel != ALARM_CHANNEL_EVENT) return false;
        title = event->active ? "VOC 事件报警" : "VOC 事件恢复";
        n = snprintf(body, body_size, "VOC 指数（10 秒均值）：%u.%u\r\n",
                     (unsigned)(event->event_voc_x10 / 10U), (unsigned)(event->event_voc_x10 % 10U));
    } else if (event->channel == ALARM_CHANNEL_VOC) {
        title = event->active ? "VOC 指数报警" : "VOC 指数恢复";
        n = snprintf(body, body_size, "VOC 指数：%u\r\n", (unsigned)event->voc_index);
    } else if (event->channel == ALARM_CHANNEL_FORECAST) {
        title = event->active ? "30 分钟趋势提醒" : "30 分钟趋势解除";
        n = snprintf(body, body_size, "上升分数：%u.%u%%\r\n",
                     (unsigned)(event->up_score / 10U), (unsigned)(event->up_score % 10U));
    } else {
        return false;
    }
    if (n < 0 || (size_t)n >= body_size) return false;
    n = snprintf(subject, subject_size, "%s", title);
    return n >= 0 && (size_t)n < subject_size;
}

/* 正文使用七位传输编码，兼容不支持 8BITMIME 的服务器。 */
static bool Mail_EncodeCopy(const char *subject, const char *body, char *encoded_subject,
                            size_t subject_size, char *encoded_body, size_t body_size)
{
    char subject_b64[54];  /* 主题的 Base64 内容限 52 字节。 */
    char body_b64[132];
    if (!Mail_Encode(subject, subject_b64, sizeof(subject_b64)) ||
        !Mail_Encode(body, body_b64, sizeof(body_b64))) return false;
    int n = snprintf(encoded_subject, subject_size, "=?UTF-8?B?%s?=", subject_b64);
    if (n < 0 || (size_t)n >= subject_size || n > 67) return false;
    size_t length = strlen(body_b64), used = 0;
    for (size_t i = 0; i < length;) {
        size_t take = length - i;
        if (take > 76U) take = 76U;
        if (take + 2U >= body_size - used) return false;
        memcpy(encoded_body + used, body_b64 + i, take);
        used += take;
        encoded_body[used++] = '\r';
        encoded_body[used++] = '\n';
        i += take;
    }
    if (!used) return false;
    encoded_body[used] = '\0';
    return true;
}

static smtp_result_t Mail_Deliver(const alarm_event_t *event, uint32_t test_id)
{
    mail_transport_t transport = { .test_id = test_id };
    if (event) transport.event = *event;
    if (!Mail_Current(&transport)) return SMTP_STALE;
    Mail_Resources("before");
    char user[164] = {0};
    char password[176] = {0};
    char subject_text[48];
    char body_text[96];
    char subject[68];
    char body[136];
    char message_id[96];
    char date[64];
    smtp_result_t result = SMTP_INVALID;
    time_t now = time(NULL);
    struct tm utc;
    if (!gmtime_r(&now, &utc) ||
        !strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S +0000", &utc)) goto cleanup;
    if (!Mail_FormatCopy(event, test_id, subject_text, sizeof(subject_text), body_text, sizeof(body_text)) ||
        !Mail_EncodeCopy(subject_text, body_text, subject, sizeof(subject), body, sizeof(body))) goto cleanup;
    int n = snprintf(message_id, sizeof(message_id), test_id ? "air-test-%08lx-%lu@smtp.qq.com" :
                 "air-%08lx-%lu@smtp.qq.com", (unsigned long)g_mail_boot_id,
                 (unsigned long)(test_id ? test_id : event->id));
    if (n < 0 || (size_t)n >= sizeof(message_id)) goto cleanup;
    if (!Mail_Encode(CONFIG_AIR_GATEWAY_MAIL_FROM, user, sizeof(user)) ||
        !Mail_Encode(CONFIG_AIR_GATEWAY_MAIL_AUTH_CODE, password, sizeof(password))) goto cleanup;

    transport.tls = esp_tls_init();
    if (!transport.tls) { result = SMTP_IO; goto cleanup; }
    const esp_tls_cfg_t tls_config = {
        .crt_bundle_attach = esp_crt_bundle_attach,
        .common_name = "smtp.qq.com",
        .skip_common_name = false,
        .non_block = true,
        .timeout_ms = MAIL_CONNECT_MS,
        .tls_version = ESP_TLS_VER_TLS_1_2
    };
    if (esp_tls_conn_new_sync("smtp.qq.com", 11, 465, &tls_config, transport.tls) != 1) {
        result = SMTP_IO;
        goto cleanup;
    }
    if (!Mail_Current(&transport)) { result = SMTP_STALE; goto cleanup; }
    smtp_io_t io = {
        .context = &transport, .Read = Mail_Read, .Write = Mail_Write,
        .NowMs = Mail_Clock, .WaitMs = Mail_Wait, .IsCurrent = Mail_Current
    };
    smtp_message_t message = {
        .from = CONFIG_AIR_GATEWAY_MAIL_FROM, .to = CONFIG_AIR_GATEWAY_MAIL_TO,
        .user_base64 = user, .password_base64 = password,
        .subject = subject, .body = body, .message_id = message_id, .date = date
    };
    result = SMTP_SendMime(&io, &message);
cleanup:
    if (transport.tls) esp_tls_conn_destroy(transport.tls);
    mbedtls_platform_zeroize(user, sizeof(user));
    mbedtls_platform_zeroize(password, sizeof(password));
    Mail_Resources("after");
    return result;
}

static void Mail_Task(void *argument)
{
    (void)argument;
    for (;;) {
        for (unsigned channel = 0; channel < ALARM_CHANNEL_COUNT; ++channel) {
            mail_slot_t slot;
            xSemaphoreTake(g_mail_lock, portMAX_DELAY);
            slot = g_mail_slots[channel];
            xSemaphoreGive(g_mail_lock);
            if (!slot.pending) continue;
            uint32_t now = Mail_Now();
            bool current = (uint32_t)(now - slot.event.timestamp_ms) < MAIL_EXPIRY_MS &&
                           Alarm_App_EventCurrent(&slot.event);
            smtp_result_t result = SMTP_STALE;
            if (current) {
                if ((int32_t)(now - slot.due_ms) < 0 || !Mail_NetworkReady()) continue;
                result = Mail_Deliver(&slot.event, 0);
            }
            xSemaphoreTake(g_mail_lock, portMAX_DELAY);
            mail_slot_t *pending = &g_mail_slots[channel];
            if (pending->pending && pending->event.id == slot.event.id) {
                if (result == SMTP_OK || result == SMTP_STALE || result == SMTP_AMBIGUOUS ||
                    result == SMTP_INVALID || ++pending->attempts >= MAIL_ATTEMPTS) {
                    pending->pending = false;
                } else {
                    pending->due_ms = Mail_Now() + (30000U << (pending->attempts - 1));
                }
            }
            xSemaphoreGive(g_mail_lock);
            /* 不记录服务器回复、凭据、地址或邮件内容。 */
            if (result == SMTP_OK) ESP_LOGI(TAG, "Event %lu accepted by SMTP", (unsigned long)slot.event.id);
            else if (result == SMTP_AMBIGUOUS)
                ESP_LOGW(TAG, "Event %lu delivery unknown; not retried", (unsigned long)slot.event.id);
            else if (result != SMTP_STALE)
                ESP_LOGW(TAG, "Event %lu attempt failed (%d)", (unsigned long)slot.event.id, (int)result);
        }
        bool network_ready = Mail_NetworkReady();
        xSemaphoreTake(g_mail_lock, portMAX_DELAY);
        bool was_pending = g_mail_test.pending;
        bool due = Mail_TestDue(&g_mail_test, Mail_Now(), network_ready, Mail_AlarmPending());
        bool expired = was_pending && !g_mail_test.pending;
        uint32_t test_id = g_mail_test.id;
        xSemaphoreGive(g_mail_lock);
        if (expired) ESP_LOGW(TAG, "TEST expired after 5 minutes; enter mail test to request again");
        if (due) {
            ESP_LOGI(TAG, "TEST %lu sending; real alarms have priority", (unsigned long)test_id);
            smtp_result_t result = Mail_Deliver(NULL, test_id);
            xSemaphoreTake(g_mail_lock, portMAX_DELAY);
            Mail_TestComplete(&g_mail_test, test_id, Mail_Now(), result);
            bool retry = g_mail_test.pending;
            xSemaphoreGive(g_mail_lock);
            if (result == SMTP_OK)
                ESP_LOGI(TAG, "TEST %lu accepted by SMTP; check inbox and spam", (unsigned long)test_id);
            else if (result == SMTP_AMBIGUOUS)
                ESP_LOGW(TAG, "TEST %lu delivery unknown; not retried", (unsigned long)test_id);
            else if (result == SMTP_STALE)
                ESP_LOGW(TAG, "TEST %lu cancelled: expired or real alarm priority; not retried",
                         (unsigned long)test_id);
            else ESP_LOGW(TAG, "TEST %lu failed (%d); %s", (unsigned long)test_id,
                          (int)result, retry ? "retry scheduled" : "request ended");
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

#if defined(CONFIG_AIR_GATEWAY_MAIL_TEST_CONSOLE) && CONFIG_AIR_GATEWAY_MAIL_TEST_CONSOLE
static void Mail_ConsoleTask(void *argument)
{
    (void)argument;
    mail_test_input_t input = {0};
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
        ESP_LOGW(TAG, "TEST console unavailable; no test email requested");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Manual TEST ready: enter mail test then Enter; no startup email");
    for (;;) {
        unsigned char bytes[32];
        ssize_t n = read(STDIN_FILENO, bytes, sizeof(bytes));
        for (ssize_t i = 0; i < n; ++i)
            if (Mail_TestInput(&input, bytes[i])) (void)Mail_RequestTest();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#endif

void Mail_Init(void)
{
    if (g_mail_ready) return;
    if (!Mail_ConfigValid()) {
        ESP_LOGW(TAG, "Email inactive: configure sender, authorization code and recipient locally");
        return;
    }
    g_mail_lock = xSemaphoreCreateMutex();
    if (!g_mail_lock) { ESP_LOGE(TAG, "Email mutex allocation failed"); return; }
    g_mail_boot_id = Boot_Diag_GetBootId();
    if (xTaskCreate(Mail_Task, "mail", MAIL_WORKER_STACK, NULL, 2, NULL) != pdPASS) {
        vSemaphoreDelete(g_mail_lock);
        g_mail_lock = NULL;
        ESP_LOGE(TAG, "Email task allocation failed");
        return;
    }
    g_mail_ready = true;
    ESP_LOGI(TAG, "Email worker ready; TLS certificate/hostname verification required on port 465");
#if defined(CONFIG_AIR_GATEWAY_MAIL_TEST_CONSOLE) && CONFIG_AIR_GATEWAY_MAIL_TEST_CONSOLE
    if (xTaskCreate(Mail_ConsoleTask, "mail_console", 3072, NULL, 1, NULL) != pdPASS)
        ESP_LOGW(TAG, "TEST console task allocation failed; alarm email worker remains active");
#endif
}

bool Mail_RequestTest(void)
{
    if (!g_mail_ready) return false;
    xSemaphoreTake(g_mail_lock, portMAX_DELAY);
    bool accepted = Mail_TestRequest(&g_mail_test, Mail_Now());
    xSemaphoreGive(g_mail_lock);
    if (accepted) ESP_LOGI(TAG, "TEST queued; waiting for network/time and any real alarm; expires in 5 minutes");
    else ESP_LOGW(TAG, "TEST not queued: already pending or wait 60 seconds between requests");
    return accepted;
}

bool Mail_SendAlarm(const alarm_event_t *event)
{
    if (!g_mail_ready || !Mail_EventValid(event)) return false;
    xSemaphoreTake(g_mail_lock, portMAX_DELAY);
    mail_slot_t *slot = &g_mail_slots[event->channel];
    if (!slot->have_id || slot->last_id != event->id) {
        slot->event = *event;
        slot->due_ms = Mail_Now();
        slot->attempts = 0;
        slot->pending = true;
        slot->last_id = event->id;
        slot->have_id = true;
    }
    xSemaphoreGive(g_mail_lock);
    return true;
}

#else

void Mail_Init(void)
{
    ESP_LOGI("Mail", "Email disabled; no SMTP connections will be made");
}

bool Mail_SendAlarm(const alarm_event_t *event)
{
    (void)event;
    return false;
}

bool Mail_RequestTest(void)
{
    return false;
}

#endif
