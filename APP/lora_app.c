#include "lora_app.h"

#include "main.h"
#include "sx1268_radio.h"
#include "telemetry_packet.h"

#include <stdio.h>
#include <string.h>

typedef enum
{
    LORA_STATE_RETRY_INIT = 0,
    LORA_STATE_IDLE,
    LORA_STATE_TRANSMITTING
} lora_state_t;

static volatile uint8_t lora_dio1_irq = 0U;
static lora_state_t lora_state = LORA_STATE_RETRY_INIT;
static uint32_t lora_next_action_ms = 0U;
static uint32_t lora_tx_started_ms = 0U;
static uint32_t lora_packet_counter = 0U;

static void lora_try_init(void)
{
    sx1268_result_t result;
    uint8_t status = 0U;

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
    }
    else
    {
        lora_state = LORA_STATE_RETRY_INIT;
        lora_next_action_ms = HAL_GetTick() + 5000U;
        printf("[LoRa] Init failed: %s (status=0x%02X), retry in 5 s\r\n",
               SX1268_ResultString(result), status);
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
        lora_next_action_ms = HAL_GetTick() + LORA_APP_TX_PERIOD_MS;
        return;
    }

    /* Clear the software flag before SetTx so a fast DIO1 edge is not lost. */
    lora_dio1_irq = 0U;
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
    }
}

static void lora_handle_irq(void)
{
    uint16_t irq = 0U;
    sx1268_result_t result;

    lora_dio1_irq = 0U;
    result = SX1268_GetAndClearIrq(&irq);
    (void)SX1268_Standby();

    if (result != SX1268_OK)
    {
        lora_state = LORA_STATE_RETRY_INIT;
        lora_next_action_ms = HAL_GetTick() + 5000U;
        printf("[LoRa] IRQ read failed: %s\r\n",
               SX1268_ResultString(result));
        return;
    }

    if ((irq & SX1268_IRQ_TX_DONE) != 0U)
    {
        lora_state = LORA_STATE_IDLE;
        lora_next_action_ms = HAL_GetTick() + LORA_APP_TX_PERIOD_MS;
        printf("[LoRa] TX done #%lu, IRQ=0x%04X\r\n",
               lora_packet_counter, irq);
    }
    else if ((irq & SX1268_IRQ_TIMEOUT) != 0U)
    {
        lora_state = LORA_STATE_IDLE;
        lora_next_action_ms = HAL_GetTick() + LORA_APP_TX_PERIOD_MS;
        printf("[LoRa] Radio timeout, IRQ=0x%04X\r\n", irq);
    }
    else
    {
        lora_state = LORA_STATE_IDLE;
        lora_next_action_ms = HAL_GetTick() + LORA_APP_TX_PERIOD_MS;
        printf("[LoRa] Unexpected DIO1 IRQ=0x%04X\r\n", irq);
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
        lora_state = LORA_STATE_IDLE;
        lora_next_action_ms = now + LORA_APP_TX_PERIOD_MS;
        printf("[LoRa] Software TX timeout: check PA0/DIO1 wiring and EXTI\r\n");
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
