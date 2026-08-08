#include "data_logger.h"

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
           "C=clean E=end H=help\r\n");
}

static void data_logger_emit_event(uint32_t now, const char *event_name)
{
    uint32_t elapsed_ms = now - data_logger_session_start_ms;

    printf("EVENT,%lu,%lu,%lu,%lu,%s,%s,,,,,,,\r\n",
           (unsigned long)data_logger_session_id,
           (unsigned long)elapsed_ms,
           (unsigned long)now,
           (unsigned long)data_logger_event_id,
           data_logger_state_name(data_logger_state),
           event_name);
}

static void data_logger_emit_data(uint32_t sample_ms, uint32_t now)
{
    printf("DATA,%lu,%lu,%lu,%lu,%s,,%.2f,%.2f,%.2f,%u,%ld,%u,%u\r\n",
           (unsigned long)data_logger_session_id,
           (unsigned long)sample_ms,
           (unsigned long)now,
           (unsigned long)data_logger_event_id,
           data_logger_state_name(data_logger_state),
           g_env_data.temperature,
           g_env_data.humidity,
           g_env_data.pressure,
           (unsigned int)g_env_data.sgp40_raw,
           (long)g_env_data.voc_index,
           (unsigned int)g_env_data.bme680_valid,
           (unsigned int)g_env_data.sgp40_valid);
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
    data_logger_state = DATA_LOGGER_STATE_CLEAN;
    data_logger_recording = 1U;

    data_logger_emit_event(now, "session_start");
    data_logger_emit_data(0U, now);
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
                data_logger_state = DATA_LOGGER_STATE_CLEAN;
                data_logger_emit_event(now, "clean_confirmed");
            }
            else
            {
                printf("#WARN,press_S_first\r\n");
            }
            break;

        case 'E':
        case 'e':
            if (data_logger_recording != 0U)
            {
                data_logger_emit_event(now, "session_end");
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

    /*
     * USART1 is configured as TX/RX by CubeMX. A zero-timeout poll keeps the
     * cooperative scheduler non-blocking and avoids adding an IRQ/DMA path
     * solely for occasional one-character event markers.
     */
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
    data_logger_recording = 0U;
    data_logger_state = DATA_LOGGER_STATE_CLEAN;

    printf("#LOGGER,ready,warmup_ms=%lu,sample_period_ms=%lu\r\n",
           (unsigned long)DATA_LOGGER_WARMUP_MS,
           (unsigned long)DATA_LOGGER_SAMPLE_PERIOD_MS);
    data_logger_print_help();
    printf("record_type,session_id,timestamp_ms,uptime_ms,event_id,"
           "event_state,event_name,temperature,humidity,pressure,sraw_voc,"
           "voc_index,bme680_valid,sgp40_valid\r\n");
}

void DataLogger_App_Task(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed_ms;

    data_logger_poll_uart(now);

    if (data_logger_recording == 0U)
    {
        return;
    }

    elapsed_ms = now - data_logger_session_start_ms;
    if (elapsed_ms < data_logger_next_sample_ms)
    {
        return;
    }

    if ((elapsed_ms - data_logger_next_sample_ms) >=
        DATA_LOGGER_SAMPLE_PERIOD_MS)
    {
        /*
         * A long blocking operation caused at least one missed sample.
         * Keep the real gap visible instead of fabricating duplicate values.
         */
        printf("#WARN,sample_gap_ms=%lu\r\n",
               (unsigned long)(elapsed_ms - data_logger_next_sample_ms));
        data_logger_next_sample_ms =
            (elapsed_ms / DATA_LOGGER_SAMPLE_PERIOD_MS) *
            DATA_LOGGER_SAMPLE_PERIOD_MS;
    }

    data_logger_emit_data(data_logger_next_sample_ms, now);
    data_logger_next_sample_ms += DATA_LOGGER_SAMPLE_PERIOD_MS;
}
