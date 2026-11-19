#include "run_logger.h"

#include "app_environment.h"
#include "app_voc_service.h"
#include "cmsis_os2.h"
#include "main.h"
#include "oled.h"
#include "rtos_app.h"
#include "usart.h"
#include <stdarg.h>
#include <stdio.h>

#if RUN_LOGGER_ACTIVE
#define RUN_LOGGER_QUEUE_LENGTH 4U
#define RUN_LOGGER_LINE_BYTES 384U
#define RUN_LOGGER_UART_TIMEOUT_MS 100U
#ifndef RUN_LOGGER_BUILD_ID
#define RUN_LOGGER_BUILD_ID __DATE__ " " __TIME__
#endif

typedef enum { RUN_BOOT, RUN_RAW, RUN_FAULT } RunLogger_Type_t;
typedef struct
{
    uint32_t id;
    RunLogger_Type_t type;
    union
    {
        uint32_t boot_ms;
        struct { uint32_t sequence, sample_ms, scheduled_ms, now_ms, age_ms;
                 uint32_t bme_valid, sgp_valid; float values[5];
                 SGP40_VOCSnapshot_t voc;
                 uint8_t event_present; AppVocDiagnostic event;
                 uint8_t shadow_present; AppVocMlpShadowDiagnostic shadow; } raw;
        struct { uint32_t at_ms, code; int32_t detail; } fault;
    } data;
} RunLogger_Record_t;

extern Environment_Data_t g_env_data;
static osMessageQueueId_t run_queue;
static uint32_t next_record_id;
static volatile uint32_t dropped_records, uart_errors;
static uint32_t last_raw_sequence;
static uint8_t raw_sequence_seen;
static volatile uint32_t radio_fault_reason, radio_fault_count;
/* 发送缓冲由日志任务独占。 */
static RunLogger_Record_t output_record;
static char output_line[RUN_LOGGER_LINE_BYTES];
static uint32_t output_length, last_status_ms, reported_drops, reported_errors;
static uint8_t output_valid;
#if defined(APP_STOP_ENABLE) && APP_STOP_ENABLE
static uint32_t last_stop_ms;
#endif

static uint32_t run_logger_next_id(void)
{
    uint32_t mask = __get_PRIMASK(), id;
    __disable_irq();
    id = ++next_record_id;
    __set_PRIMASK(mask);
    return id;
}

static int run_logger_submit(RunLogger_Record_t *record)
{
    record->id = run_logger_next_id();
    if ((run_queue == NULL) || (osMessageQueuePut(run_queue, record, 0U, 0U) != osOK))
    {
        ++dropped_records;
        return 0;
    }
    return 1;
}

static void run_logger_append(const char *format, ...)
{
    int length;
    va_list arguments;
    if (output_valid == 0U) return;
    va_start(arguments, format);
    length = vsnprintf(output_line + output_length,
                       sizeof(output_line) - output_length, format, arguments);
    va_end(arguments);
    if ((length < 0) || ((uint32_t)length >= sizeof(output_line) - output_length))
        output_valid = 0U;
    else output_length += (uint32_t)length;
}

static void run_logger_begin(const char *type, uint32_t id)
{
    output_length = 0U;
    output_valid = 1U;
    run_logger_append("RUN,2,%s,%lu", type, (unsigned long)id);
}

/* 构建标识并非固件哈希；拒绝分隔符、非 ASCII 字符和换行。 */
static void run_logger_build_id(void)
{
    const char *id = RUN_LOGGER_BUILD_ID;
    uint32_t i;
    for (i = 0U; (id[i] != '\0') && (i < 48U); ++i)
    {
        unsigned char c = (unsigned char)id[i];
        if ((c < 32U) || (c > 126U) || (c == ',') || (c == '*')) c = '_';
        run_logger_append("%c", (int)c);
    }
    if (i == 0U) run_logger_append("unknown_build");
}

static void run_logger_send(void)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i, bit;
    if (output_valid == 0U) { ++uart_errors; return; }
    for (i = 0U; i < output_length; ++i)
    {
        crc ^= (uint16_t)((uint16_t)(uint8_t)output_line[i] << 8);
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
    run_logger_append("*%04X\r\n", (unsigned int)crc);
    if ((output_valid == 0U) ||
        (HAL_UART_Transmit(&huart1, (uint8_t *)output_line,
                           (uint16_t)output_length, RUN_LOGGER_UART_TIMEOUT_MS) != HAL_OK))
        ++uart_errors;
}

static void run_logger_values(const float *values, uint32_t count)
{
    uint32_t i;
    for (i = 0U; i < count; ++i) run_logger_append(",%.9g", (double)values[i]);
}

int RunLogger_App_Init(void)
{
    next_record_id = dropped_records = uart_errors = 0U;
    last_status_ms = reported_drops = reported_errors = 0U;
    last_raw_sequence = 0U;
    raw_sequence_seen = 0U;
    radio_fault_reason = radio_fault_count = 0U;
#if defined(APP_STOP_ENABLE) && APP_STOP_ENABLE
    last_stop_ms = 0U;
#endif
    run_queue = osMessageQueueNew(RUN_LOGGER_QUEUE_LENGTH, sizeof(RunLogger_Record_t), NULL);
    return run_queue != NULL;
}

void RunLogger_App_Boot(uint32_t init_ms)
{
    RunLogger_Record_t record;
    record.type = RUN_BOOT;
    record.data.boot_ms = init_ms;
    run_logger_submit(&record);
}

void RunLogger_App_Raw(uint32_t scheduled_ms)
{
    RunLogger_Record_t record;
    AppVocInput event_input;
    uint32_t mask, radio_reason, radio_count;
    uint32_t sequence = Environment_App_GetSampleSequence();
    if ((raw_sequence_seen != 0U) && (sequence == last_raw_sequence)) return;
    last_raw_sequence = sequence;
    raw_sequence_seen = 1U;
    mask = __get_PRIMASK();
    __disable_irq();
    radio_reason = radio_fault_reason;
    radio_count = radio_fault_count;
    radio_fault_count = 0U;
    __set_PRIMASK(mask);
    if (radio_count != 0U)
    {
        RunLogger_App_Fault(8U, (int32_t)radio_reason);
        if (radio_count > 1U) RunLogger_App_Fault(9U, (int32_t)radio_count);
    }
    record.type = RUN_RAW;
    record.data.raw.sequence = sequence;
    record.data.raw.sample_ms = Environment_App_GetLastSampleMs();
    record.data.raw.scheduled_ms = scheduled_ms;
    record.data.raw.now_ms = HAL_GetTick();
    /* RAW 与十秒聚合使用同一采样时间。 */
    if (voc_svc_input(&event_input) && event_input.sequence == sequence &&
        event_input.scheduled_ms == scheduled_ms)
        record.data.raw.now_ms = event_input.logged_ms;
    record.data.raw.age_ms = record.data.raw.now_ms - record.data.raw.sample_ms;
    record.data.raw.bme_valid = g_env_data.bme680_valid;
    record.data.raw.sgp_valid = g_env_data.sgp40_valid;
    record.data.raw.values[0] = g_env_data.temperature;
    record.data.raw.values[1] = g_env_data.humidity;
    record.data.raw.values[2] = g_env_data.pressure;
    record.data.raw.values[3] = (float)g_env_data.sgp40_raw;
    record.data.raw.values[4] = (float)g_env_data.voc_index;
    Environment_App_GetVocSnapshot(&record.data.raw.voc);
    record.data.raw.event_present = (uint8_t)voc_svc_take_evt(&record.data.raw.event);
    record.data.raw.shadow_present = (uint8_t)voc_svc_take_ai(&record.data.raw.shadow);
    run_logger_submit(&record);
}

void RunLogger_App_Fault(uint32_t code, int32_t detail)
{
    RunLogger_Record_t record;
    record.type = RUN_FAULT;
    record.data.fault.at_ms = HAL_GetTick();
    record.data.fault.code = code;
    record.data.fault.detail = detail;
    run_logger_submit(&record);
}

void RunLogger_App_RadioFault(uint32_t reason)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    radio_fault_reason = reason;
    ++radio_fault_count;
    __set_PRIMASK(mask);
}

void RunLogger_App_Task(void)
{
    RunLogger_Record_t *record = &output_record;
    uint32_t now;
    if (osMessageQueueGet(run_queue, record, NULL, osWaitForever) != osOK) return;
    switch (record->type)
    {
    case RUN_BOOT:
        run_logger_begin("BOOT", record->id);
        run_logger_append(",voc_event_v1,%lu,%s,", (unsigned long)record->data.boot_ms,
                          APP_VOC_MLP_MODEL_TAG);
        run_logger_build_id();
        run_logger_append(",%s", SGP40_GetAlgorithmVersion());
        run_logger_send();
        break;
    case RUN_RAW:
        run_logger_begin("RAW", record->id);
        run_logger_append(",%lu,%lu,%lu,%lu,%lu,%lu,%lu",
            (unsigned long)record->data.raw.sequence, (unsigned long)record->data.raw.sample_ms,
            (unsigned long)record->data.raw.scheduled_ms, (unsigned long)record->data.raw.now_ms,
            (unsigned long)record->data.raw.age_ms, (unsigned long)record->data.raw.bme_valid,
            (unsigned long)record->data.raw.sgp_valid);
        run_logger_values(record->data.raw.values, 5U);
        run_logger_append(",%lu,%lu", (unsigned long)dropped_records, (unsigned long)uart_errors);
        run_logger_append(",%lu,%lu,%lu,%lu,%.9g,%.9g,%.9g",
            (unsigned long)record->data.raw.voc.init_count,
            (unsigned long)record->data.raw.voc.init_ms,
            (unsigned long)record->data.raw.voc.process_count,
            (unsigned long)record->data.raw.voc.estimator_initialized,
            (double)record->data.raw.voc.mean, (double)record->data.raw.voc.std,
            (double)record->data.raw.voc.estimator_uptime_gamma_s);
        run_logger_send();
        if (record->data.raw.event_present != 0U)
        {
            const AppVocDiagnostic *event=&record->data.raw.event;
            output_length=0U;output_valid=1U;
            run_logger_append("VEVT,1,%lu,%lu,%lu,%lu,%u,%u,%u,%lu,%.9g,%u,%u,%u,%u,%u,%.9g,%.9g,%.9g",
                (unsigned long)record->id,(unsigned long)event->start_ms,(unsigned long)event->end_ms,
                (unsigned long)event->valid_slots,(unsigned)event->accepted,(unsigned)event->warmup_ready,
                (unsigned)event->mode,(unsigned long)event->reason_flags,(double)event->voc_mean,
                (unsigned)event->detector.ready,(unsigned)event->detector.state,(unsigned)event->detector.alarm_active,
                (unsigned)event->detector.alarm_started,(unsigned)event->detector.reset,
                (double)event->detector.score,(double)event->detector.baseline,(double)event->detector.scale);
            run_logger_send();
        }
        if (record->data.raw.shadow_present != 0U)
        {
            const AppVocMlpShadowDiagnostic *shadow=&record->data.raw.shadow;
            output_length=0U;output_valid=1U;
            run_logger_append("VMLP,1,%lu,%lu,%lu,%lu,%u,%u,%u,%u,%u,%.9g,%s,%lu",
                (unsigned long)record->id,(unsigned long)shadow->start_ms,(unsigned long)shadow->end_ms,
                (unsigned long)shadow->valid_slots,(unsigned)shadow->accepted,(unsigned)shadow->ready,
                (unsigned)shadow->state,(unsigned)shadow->active,(unsigned)shadow->alarm_started,
                (double)shadow->score,APP_VOC_MLP_MODEL_TAG,(unsigned long)shadow->reason_flags);
            run_logger_values(shadow->features,12U);
            run_logger_send();
        }
#if defined(APP_STOP_ENABLE) && APP_STOP_ENABLE
        {
            uint32_t at_ms = HAL_GetTick();
            if ((uint32_t)(at_ms - last_stop_ms) >= 10000U)
            {
                app_stop_diag_t diag;
                last_stop_ms = at_ms;
                app_stop_get_diag(&diag);
                output_length = 0U;
                output_valid = 1U;
                run_logger_append("LPSTOP,1,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
                    (unsigned long)at_ms, (unsigned long)diag.state,
                    (unsigned long)diag.entries, (unsigned long)diag.rtc_wakes,
                    (unsigned long)diag.early_wakes, (unsigned long)diag.bypasses,
                    (unsigned long)diag.elapsed_ms, (unsigned long)diag.deadline_overruns,
                    (unsigned long)diag.fault_reason);
                run_logger_send();
            }
        }
#endif
        break;
    case RUN_FAULT:
        run_logger_begin("FAULT", record->id);
        run_logger_append(",%lu,%lu,%ld", (unsigned long)record->data.fault.at_ms,
            (unsigned long)record->data.fault.code, (long)record->data.fault.detail);
        run_logger_send();
        break;
    default: break;
    }
    now = HAL_GetTick();
    if (((uint32_t)(now - last_status_ms) >= 60000U) ||
        (reported_drops != dropped_records) || (reported_errors != uart_errors))
    {
        uint32_t drops = dropped_records, errors = uart_errors;
        /* STATUS 沿用出队记录的编号，避免乱序。 */
        run_logger_begin("STATUS", record->id);
        run_logger_append(",%lu,%lu,%lu,%lu", (unsigned long)now,
            (unsigned long)drops, (unsigned long)errors,
            (unsigned long)osMessageQueueGetCount(run_queue));
        run_logger_send();
        last_status_ms = now;
        reported_drops = drops;
        reported_errors = errors;
    }
}
#else
int RunLogger_App_Init(void) { return 1; }
void RunLogger_App_Task(void) { }
void RunLogger_App_Boot(uint32_t init_ms) { (void)init_ms; }
void RunLogger_App_Raw(uint32_t scheduled_ms) { (void)scheduled_ms; }
void RunLogger_App_Fault(uint32_t code, int32_t detail) { (void)code; (void)detail; }
void RunLogger_App_RadioFault(uint32_t reason) { (void)reason; }
#endif
