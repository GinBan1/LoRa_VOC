#include "lora_app.h"

#include "main.h"
#include "sx1268_radio.h"
#include "telemetry_packet.h"
#include "run_logger.h"

#include <stdio.h>
#include <string.h>

typedef enum
{
    LORA_STATE_RETRY_INIT = 0,
    LORA_STATE_IDLE,
    LORA_STATE_TRANSMITTING
} lora_state_t;

static volatile uint8_t lora_dio1_irq = 0U;
static volatile uint8_t lora_sleeping = 0U;
static volatile lora_state_t lora_state = LORA_STATE_RETRY_INIT;
static uint32_t lora_next_action_ms = 0U;
static uint32_t lora_tx_started_ms = 0U;
static uint32_t lora_packet_counter = 0U;

static sx1268_result_t lora_sleep(void)
{
    sx1268_result_t result = SX1268_SleepWarm();
    lora_sleeping = (uint8_t)(result == SX1268_OK);
    return result;
}

static void lora_try_init(void)
{
    sx1268_result_t result;
    uint8_t status = 0U;

    lora_sleeping = 0U;
    printf("\r\n[LoRa] Initializing E22-400M22S...\r\n");
    result = SX1268_Init(LORA_APP_FREQUENCY_HZ,
                         LORA_APP_TX_POWER_DBM,
                         &status);

    if (result == SX1268_OK)
    {
        lora_state = LORA_STATE_IDLE;
        lora_next_action_ms = HAL_GetTick() + 1000U;
        printf("[LoRa] Ready: status=0x%02X, freq=%lu Hz, power=%d dBm\r\n",
               status, LORA_APP_FREQUENCY_HZ, LORA_APP_TX_POWER_DBM);
        printf("[LoRa] Profile: SF7/BW125/CR4-5/private sync word\r\n");
        result = lora_sleep();
        if (result != SX1268_OK)
        {
            lora_state = LORA_STATE_RETRY_INIT;
            lora_next_action_ms = HAL_GetTick() + 5000U;
            printf("[LoRa] Sleep command failed: %s\r\n",
                   SX1268_ResultString(result));
            RunLogger_App_RadioFault(2U);
        }
    }
    else
    {
        lora_state = LORA_STATE_RETRY_INIT;
        lora_next_action_ms = HAL_GetTick() + 5000U;
        printf("[LoRa] Init failed: %s (status=0x%02X), retry in 5 s\r\n",
               SX1268_ResultString(result), status);
        RunLogger_App_RadioFault(1U);
    }
}

static void lora_start_telemetry_packet(void)
{
    char payload[TELEMETRY_PACKET_MAX_SIZE];
    size_t payload_length;
    sx1268_result_t result;

    lora_packet_counter++;
    payload_length = TelemetryPacket_Format(payload,
                                             sizeof(payload),
                                             lora_packet_counter,
                                             HAL_GetTick());
    if ((payload_length == 0U) || (payload_length > 255U))
    {
        printf("[LoRa] Telemetry format failed for packet #%lu\r\n",
               (unsigned long)lora_packet_counter);
        RunLogger_App_RadioFault(3U);
        lora_next_action_ms = HAL_GetTick() + LORA_APP_TX_PERIOD_MS;
        return;
    }

    /* 先清中断标记，再启动发射，避免漏掉快速返回的 DIO1 边沿。 */
    lora_dio1_irq = 0U;
    lora_sleeping = 0U;
    result = SX1268_StartTx((const uint8_t *)payload,
                            (uint8_t)payload_length,
                            LORA_APP_TX_TIMEOUT_MS);
    if (result == SX1268_OK)
    {
        lora_state = LORA_STATE_TRANSMITTING;
        lora_tx_started_ms = HAL_GetTick();
        printf("[LoRa] TX telemetry #%lu (%u bytes): %s\r\n",
               (unsigned long)lora_packet_counter,
               (unsigned int)payload_length,
               payload);
    }
    else
    {
        lora_state = LORA_STATE_RETRY_INIT;
        lora_next_action_ms = HAL_GetTick() + 5000U;
        printf("[LoRa] TX command failed: %s\r\n",
               SX1268_ResultString(result));
        RunLogger_App_RadioFault(4U);
    }
}

static void lora_handle_irq(void)
{
    uint16_t irq = 0U;
    sx1268_result_t result;

    lora_dio1_irq = 0U;
    lora_sleeping = 0U;
    result = SX1268_GetAndClearIrq(&irq);
    (void)SX1268_Standby();

    if (result != SX1268_OK)
    {
        lora_state = LORA_STATE_RETRY_INIT;
        lora_next_action_ms = HAL_GetTick() + 5000U;
        printf("[LoRa] IRQ read failed: %s\r\n",
               SX1268_ResultString(result));
        RunLogger_App_RadioFault(5U);
        return;
    }

    if ((irq & SX1268_IRQ_TX_DONE) != 0U)
    {
        lora_state = LORA_STATE_IDLE;
        lora_next_action_ms = HAL_GetTick() + LORA_APP_TX_PERIOD_MS;
        printf("[LoRa] TX done #%lu, IRQ=0x%04X\r\n",
               (unsigned long)lora_packet_counter, irq);
    }
    else if ((irq & SX1268_IRQ_TIMEOUT) != 0U)
    {
        lora_state = LORA_STATE_IDLE;
        lora_next_action_ms = HAL_GetTick() + LORA_APP_TX_PERIOD_MS;
        printf("[LoRa] Radio timeout, IRQ=0x%04X\r\n", irq);
        RunLogger_App_RadioFault(6U);
    }
    else
    {
        lora_state = LORA_STATE_IDLE;
        lora_next_action_ms = HAL_GetTick() + LORA_APP_TX_PERIOD_MS;
        printf("[LoRa] Unexpected DIO1 IRQ=0x%04X\r\n", irq);
        RunLogger_App_RadioFault(7U);
    }

    result = lora_sleep();
    if (result != SX1268_OK)
    {
        lora_state = LORA_STATE_RETRY_INIT;
        lora_next_action_ms = HAL_GetTick() + 5000U;
        printf("[LoRa] Sleep command failed: %s\r\n",
               SX1268_ResultString(result));
        RunLogger_App_RadioFault(2U);
    }
}

void Lora_App_Init(void)
{
    lora_dio1_irq = 0U;
    lora_state = LORA_STATE_RETRY_INIT;
    lora_next_action_ms = 0U;
    lora_tx_started_ms = 0U;
    lora_packet_counter = 0U;
    lora_try_init();
}

void Lora_App_Task(void)
{
    uint32_t now = HAL_GetTick();

    if (lora_dio1_irq != 0U)
    {
        lora_handle_irq();
    }

    if ((lora_state == LORA_STATE_RETRY_INIT) &&
        ((int32_t)(now - lora_next_action_ms) >= 0))
    {
        lora_try_init();
    }
    else if ((lora_state == LORA_STATE_IDLE) &&
             ((int32_t)(now - lora_next_action_ms) >= 0))
    {
        lora_start_telemetry_packet();
    }
    else if ((lora_state == LORA_STATE_TRANSMITTING) &&
             ((now - lora_tx_started_ms) >
              (LORA_APP_TX_TIMEOUT_MS + 500U)))
    {
        (void)SX1268_Standby();
        (void)lora_sleep();
        lora_state = LORA_STATE_IDLE;
        lora_next_action_ms = now + LORA_APP_TX_PERIOD_MS;
        printf("[LoRa] Software TX timeout: check PA0/DIO1 wiring and EXTI\r\n");
        RunLogger_App_RadioFault(8U);
    }
}

void Lora_App_Dio1Irq(void)
{
    lora_dio1_irq = 1U;
}

uint8_t Lora_App_IsReady(void)
{
    return (lora_state != LORA_STATE_RETRY_INIT) ? 1U : 0U;
}

uint8_t Lora_App_CanStop(void)
{
    /* 仅无线模块已休眠且无待处理 DIO1 中断时允许 MCU 进入 STOP。 */
    return (uint8_t)(lora_state == LORA_STATE_IDLE && lora_sleeping != 0U && lora_dio1_irq == 0U);
}

uint32_t Lora_App_GetNextWakeDelayMs(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t deadline;

    if (lora_dio1_irq != 0U)
    {
        return 1U;
    }

    if (lora_state == LORA_STATE_TRANSMITTING)
    {
        deadline = lora_tx_started_ms + LORA_APP_TX_TIMEOUT_MS + 500U;
    }
    else
    {
        deadline = lora_next_action_ms;
    }

    if ((int32_t)(now - deadline) >= 0)
    {
        return 1U;
    }

    return deadline - now;
}
