#include "data_logger.h"

#include "app_environment.h"
#include "main.h"
#include "oled.h"
#include "usart.h"

#include <stdint.h>
#include <stdio.h>

typedef enum
{
    DATA_LOGGER_STATE_CLEAN = 0,
    DATA_LOGGER_STATE_POLLUTION_PRESENT,
    DATA_LOGGER_STATE_RECOVERY
} data_logger_state_t;

extern Environment_Data_t g_env_data;

static uint32_t data_logger_boot_ms;
static uint32_t data_logger_session_start_ms;
static uint32_t data_logger_next_sample_ms;
static uint32_t data_logger_session_id;
static uint32_t data_logger_event_id;
static uint32_t data_logger_emitted_samples;
static uint32_t data_logger_missed_samples;
static uint32_t data_logger_last_environment_sequence;
static uint32_t data_logger_last_status_ms;
static uint8_t data_logger_recording;
static data_logger_state_t data_logger_state;

static const char *data_logger_state_name(data_logger_state_t state)
{
    switch (state)
    {
        case DATA_LOGGER_STATE_POLLUTION_PRESENT:
            return "pollution_present";

        case DATA_LOGGER_STATE_RECOVERY:
            return "recovery";

        case DATA_LOGGER_STATE_CLEAN:
        default:
            return "clean";
    }
}

static void data_logger_print_help(void)
{
    printf("#COMMAND,S=start P=source_placed R=source_removed "
           "C=clean M=manual_marker T=status E=end H=help\r\n");
}

static void data_logger_emit_event(uint32_t now, const char *event_name)
{
    uint32_t elapsed_ms = now - data_logger_session_start_ms;
    uint32_t sensor_ms = Environment_App_GetLastSampleMs();
    uint32_t sensor_sequence = Environment_App_GetSampleSequence();
    uint32_t sensor_age_ms = now - sensor_ms;
    uint32_t sample_index =
        data_logger_next_sample_ms / DATA_LOGGER_SAMPLE_PERIOD_MS;

    printf("EVENT,%lu,%lu,%lu,%lu,%lu,%lu,,,%lu,%lu,,%lu,%lu,%s,%s"
           ",,,,,,,\r\n",
           (unsigned long)DATA_LOGGER_SCHEMA_VERSION,
           (unsigned long)data_logger_session_id,
           (unsigned long)sample_index,
           (unsigned long)elapsed_ms,
           (unsigned long)now,
           (unsigned long)elapsed_ms,
           (unsigned long)sensor_sequence,
           (unsigned long)sensor_age_ms,
           (unsigned long)data_logger_missed_samples,
           (unsigned long)data_logger_event_id,
           data_logger_state_name(data_logger_state),
           event_name);
}

static uint32_t data_logger_emit_data(uint32_t sample_ms,
                                      uint32_t now,
                                      uint32_t sample_index)
{
    uint32_t actual_elapsed_ms = now - data_logger_session_start_ms;
    uint32_t scheduled_uptime_ms = data_logger_session_start_ms + sample_ms;
    uint32_t sensor_ms = Environment_App_GetLastSampleMs();
    uint32_t sensor_sequence = Environment_App_GetSampleSequence();
    uint32_t sensor_age_ms = now - sensor_ms;
    uint32_t sensor_fresh =
        (sensor_sequence != data_logger_last_environment_sequence) ? 1U : 0U;
    int32_t timing_error_ms = (int32_t)(actual_elapsed_ms - sample_ms);

    printf("DATA,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%ld,%lu,%lu,%lu,%lu,%lu,%s,,"
           "%.2f,%.2f,%.2f,%u,%ld,%u,%u\r\n",
           (unsigned long)DATA_LOGGER_SCHEMA_VERSION,
           (unsigned long)data_logger_session_id,
           (unsigned long)sample_index,
           (unsigned long)sample_ms,
           (unsigned long)now,
           (unsigned long)actual_elapsed_ms,
           (unsigned long)scheduled_uptime_ms,
           (long)timing_error_ms,
           (unsigned long)sensor_sequence,
           (unsigned long)sensor_age_ms,
           (unsigned long)sensor_fresh,
           (unsigned long)data_logger_missed_samples,
           (unsigned long)data_logger_event_id,
           data_logger_state_name(data_logger_state),
           g_env_data.temperature,
           g_env_data.humidity,
           g_env_data.pressure,
           (unsigned int)g_env_data.sgp40_raw,
           (long)g_env_data.voc_index,
           (unsigned int)g_env_data.bme680_valid,
           (unsigned int)g_env_data.sgp40_valid);

    data_logger_emitted_samples++;
    data_logger_last_environment_sequence = sensor_sequence;
    return sensor_fresh;
}

static void data_logger_emit_status(uint32_t now)
{
    uint32_t elapsed_ms = now - data_logger_session_start_ms;

    printf("#STATUS,recording=%u,session_id=%lu,elapsed_ms=%lu,"
           "samples=%lu,missed=%lu,state=%s\r\n",
           (unsigned int)data_logger_recording,
           (unsigned long)data_logger_session_id,
           (unsigned long)(data_logger_recording ? elapsed_ms : 0U),
           (unsigned long)data_logger_emitted_samples,
           (unsigned long)data_logger_missed_samples,
           data_logger_state_name(data_logger_state));
}

static void data_logger_start_session(uint32_t now)
{
    uint32_t warmup_elapsed_ms = now - data_logger_boot_ms;

    if (warmup_elapsed_ms < DATA_LOGGER_WARMUP_MS)
    {
        printf("#WAIT,warmup_remaining_ms=%lu\r\n",
               (unsigned long)(DATA_LOGGER_WARMUP_MS - warmup_elapsed_ms));
        return;
    }

    data_logger_session_id++;
    data_logger_session_start_ms = now;
    data_logger_next_sample_ms = 0U;
    data_logger_event_id = 0U;
    data_logger_emitted_samples = 0U;
    data_logger_missed_samples = 0U;
    /* 预热结束后允许第一条新采样入表。 */
    data_logger_last_environment_sequence = 0U;
    data_logger_last_status_ms = now;
    data_logger_state = DATA_LOGGER_STATE_CLEAN;
    data_logger_recording = 1U;

    data_logger_emit_event(now, "session_start");
    (void)data_logger_emit_data(0U, now, 0U);
    data_logger_next_sample_ms = DATA_LOGGER_SAMPLE_PERIOD_MS;
}

static void data_logger_handle_command(uint8_t command, uint32_t now)
{
    switch (command)
    {
        case 'S':
        case 's':
            if (data_logger_recording != 0U)
            {
                printf("#WARN,session_already_running\r\n");
            }
            else
            {
                data_logger_start_session(now);
            }
            break;

        case 'P':
        case 'p':
            if (data_logger_recording != 0U)
            {
                data_logger_event_id++;
                data_logger_state = DATA_LOGGER_STATE_POLLUTION_PRESENT;
                data_logger_emit_event(now, "source_placed");
            }
            else
            {
                printf("#WARN,press_S_first\r\n");
            }
            break;

        case 'R':
        case 'r':
            if (data_logger_recording != 0U)
            {
                data_logger_event_id++;
                data_logger_state = DATA_LOGGER_STATE_RECOVERY;
                data_logger_emit_event(now, "source_removed");
            }
            else
            {
                printf("#WARN,press_S_first\r\n");
            }
            break;

        case 'C':
        case 'c':
            if (data_logger_recording != 0U)
            {
                data_logger_event_id++;
                data_logger_state = DATA_LOGGER_STATE_CLEAN;
                data_logger_emit_event(now, "clean_confirmed");
            }
            else
            {
                printf("#WARN,press_S_first\r\n");
            }
            break;

        case 'M':
        case 'm':
            if (data_logger_recording != 0U)
            {
                data_logger_event_id++;
                data_logger_emit_event(now, "manual_marker");
            }
            else
            {
                printf("#WARN,press_S_first\r\n");
            }
            break;

        case 'T':
        case 't':
            data_logger_emit_status(now);
            break;

        case 'E':
        case 'e':
            if (data_logger_recording != 0U)
            {
                data_logger_event_id++;
                data_logger_emit_event(now, "session_end");
                if ((now - data_logger_session_start_ms) <
                    DATA_LOGGER_MIN_SESSION_MS)
                {
                    printf("#WARN,session_shorter_than_recommended_ms=%lu\r\n",
                           (unsigned long)DATA_LOGGER_MIN_SESSION_MS);
                }
                data_logger_recording = 0U;
                printf("#LOGGER,session_saved_in_terminal_capture\r\n");
            }
            break;

        case 'H':
        case 'h':
        case '?':
            data_logger_print_help();
            break;

        case '\r':
        case '\n':
        case ' ':
        case '\t':
            break;

        default:
            printf("#WARN,unknown_command=%c\r\n", command);
            data_logger_print_help();
            break;
    }
}

static void data_logger_poll_uart(uint32_t now)
{
    uint8_t command;
    uint32_t reads;

    /* 零超时读取单字符事件标记，不阻塞采集任务。 */
    for (reads = 0U; reads < 8U; reads++)
    {
        if (HAL_UART_Receive(&huart1, &command, 1U, 0U) != HAL_OK)
        {
            break;
        }

        data_logger_handle_command(command, now);
    }
}

void DataLogger_App_Init(void)
{
    data_logger_boot_ms = HAL_GetTick();
    data_logger_session_start_ms = data_logger_boot_ms;
    data_logger_next_sample_ms = 0U;
    data_logger_session_id = 0U;
    data_logger_event_id = 0U;
    data_logger_emitted_samples = 0U;
    data_logger_missed_samples = 0U;
    data_logger_last_environment_sequence = 0U;
    data_logger_last_status_ms = data_logger_boot_ms;
    data_logger_recording = 0U;
    data_logger_state = DATA_LOGGER_STATE_CLEAN;

    printf("#LOGGER,ready,schema=%lu,warmup_ms=%lu,sample_period_ms=%lu,"
           "min_session_ms=%lu\r\n",
           (unsigned long)DATA_LOGGER_SCHEMA_VERSION,
           (unsigned long)DATA_LOGGER_WARMUP_MS,
           (unsigned long)DATA_LOGGER_SAMPLE_PERIOD_MS,
           (unsigned long)DATA_LOGGER_MIN_SESSION_MS);
    data_logger_print_help();
    printf("record_type,schema_version,session_id,sample_index,timestamp_ms,"
           "uptime_ms,actual_elapsed_ms,scheduled_uptime_ms,timing_error_ms,"
           "sensor_sequence,sensor_age_ms,sensor_fresh,missed_samples_total,"
           "event_id,event_state,event_name,temperature,humidity,pressure,"
           "sraw_voc,voc_index,bme680_valid,sgp40_valid\r\n");
}

void DataLogger_App_Task(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed_ms;
    uint32_t late_slots;
    uint32_t sample_index;

    data_logger_poll_uart(now);

    if (data_logger_recording == 0U)
    {
        return;
    }

    elapsed_ms = now - data_logger_session_start_ms;

    if ((now - data_logger_last_status_ms) >= DATA_LOGGER_STATUS_PERIOD_MS)
    {
        data_logger_emit_status(now);
        data_logger_last_status_ms = now;
    }

    if (elapsed_ms < data_logger_next_sample_ms)
    {
        return;
    }

    late_slots = (elapsed_ms - data_logger_next_sample_ms) /
                 DATA_LOGGER_SAMPLE_PERIOD_MS;
    if (late_slots > 0U)
    {
        /* 有漏采就保留时间缺口，不复制上一条数据。 */
        data_logger_missed_samples += late_slots;
        data_logger_next_sample_ms +=
            late_slots * DATA_LOGGER_SAMPLE_PERIOD_MS;
        printf("#WARN,missed_samples=%lu,total_missed=%lu\r\n",
               (unsigned long)late_slots,
               (unsigned long)data_logger_missed_samples);
    }

    sample_index = data_logger_next_sample_ms /
                   DATA_LOGGER_SAMPLE_PERIOD_MS;
    if (data_logger_emit_data(data_logger_next_sample_ms,
                              now,
                              sample_index) == 0U)
    {
        printf("#WARN,stale_sensor_sample,index=%lu\r\n",
               (unsigned long)sample_index);
    }
    data_logger_next_sample_ms += DATA_LOGGER_SAMPLE_PERIOD_MS;
}
