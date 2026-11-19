#include "sx1268_radio.h"

#include "main.h"
#include "spi.h"

#define SX1268_CMD_SET_SLEEP                0x84U
#define SX1268_CMD_SET_STANDBY              0x80U
#define SX1268_CMD_SET_TX                   0x83U
#define SX1268_CMD_SET_PACKET_TYPE          0x8AU
#define SX1268_CMD_SET_RF_FREQUENCY         0x86U
#define SX1268_CMD_SET_TX_PARAMS            0x8EU
#define SX1268_CMD_SET_PA_CONFIG            0x95U
#define SX1268_CMD_SET_REGULATOR_MODE       0x96U
#define SX1268_CMD_SET_DIO3_TCXO_CTRL       0x97U
#define SX1268_CMD_CALIBRATE                0x89U
#define SX1268_CMD_CALIBRATE_IMAGE          0x98U
#define SX1268_CMD_SET_BUFFER_BASE          0x8FU
#define SX1268_CMD_SET_MODULATION_PARAMS    0x8BU
#define SX1268_CMD_SET_PACKET_PARAMS        0x8CU
#define SX1268_CMD_SET_DIO_IRQ_PARAMS       0x08U
#define SX1268_CMD_CLEAR_IRQ_STATUS         0x02U
#define SX1268_CMD_GET_IRQ_STATUS           0x12U
#define SX1268_CMD_GET_STATUS               0xC0U
#define SX1268_CMD_WRITE_BUFFER             0x0EU
#define SX1268_CMD_WRITE_REGISTER           0x0DU
#define SX1268_CMD_SET_DIO2_RF_SWITCH       0x9DU
#define SX1268_CMD_SET_FALLBACK_MODE        0x93U

#define SX1268_PACKET_TYPE_LORA             0x01U
#define SX1268_STANDBY_RC                   0x00U
#define SX1268_SLEEP_WARM_START             0x04U
#define SX1268_REGULATOR_DCDC               0x01U
#define SX1268_TCXO_VOLTAGE_2V2             0x03U
#define SX1268_RAMP_200_US                   0x04U
#define SX1268_FALLBACK_STDBY_RC            0x20U
#define SX1268_SYNC_WORD_ADDRESS            0x0740U
#define SX1268_PRIVATE_SYNC_WORD            0x1424U

#define SX1268_XTAL_FREQ_HZ                 32000000ULL
#define SX1268_BUSY_TIMEOUT_MS              100U
#define SX1268_SPI_TIMEOUT_MS               100U

static uint8_t sx1268_is_sleeping = 0U;

static sx1268_result_t sx1268_wait_while_busy(uint32_t timeout_ms);

sx1268_result_t SX1268_Wakeup(void)
{
    uint8_t tx[2] = {SX1268_CMD_GET_STATUS, 0x00U};
    uint8_t rx[2] = {0U};
    HAL_StatusTypeDef hal_result;
    sx1268_result_t result;

    if (sx1268_is_sleeping == 0U)
    {
        return SX1268_OK;
    }

    /* 拉低 NSS 唤醒 SX126x，保持低电平发送 GetStatus 验证 SPI。 */
    HAL_GPIO_WritePin(LoRa_CS_GPIO_Port, LoRa_CS_Pin, GPIO_PIN_RESET);
    for (volatile uint32_t delay = 0U; delay < 2000U; delay++)
    {
        __NOP();
    }
    hal_result = HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2U,
                                         SX1268_SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(LoRa_CS_GPIO_Port, LoRa_CS_Pin, GPIO_PIN_SET);

    if (hal_result != HAL_OK)
    {
        return SX1268_SPI_ERROR;
    }

    result = sx1268_wait_while_busy(SX1268_BUSY_TIMEOUT_MS);
    if (result == SX1268_OK)
    {
        sx1268_is_sleeping = 0U;
    }
    return result;
}

static sx1268_result_t sx1268_wait_while_busy(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (HAL_GPIO_ReadPin(LoRa_BUSY_GPIO_Port, LoRa_BUSY_Pin) == GPIO_PIN_SET)
    {
        if ((HAL_GetTick() - start) >= timeout_ms)
        {
            return SX1268_BUSY_TIMEOUT;
        }
    }

    return SX1268_OK;
}

static sx1268_result_t sx1268_transfer(uint8_t *tx, uint8_t *rx, uint16_t length)
{
    sx1268_result_t result;
    HAL_StatusTypeDef hal_result;

    result = SX1268_Wakeup();
    if (result != SX1268_OK)
    {
        return result;
    }

    result = sx1268_wait_while_busy(SX1268_BUSY_TIMEOUT_MS);
    if (result != SX1268_OK)
    {
        return result;
    }

    HAL_GPIO_WritePin(LoRa_CS_GPIO_Port, LoRa_CS_Pin, GPIO_PIN_RESET);
    hal_result = HAL_SPI_TransmitReceive(&hspi1, tx, rx, length,
                                         SX1268_SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(LoRa_CS_GPIO_Port, LoRa_CS_Pin, GPIO_PIN_SET);

    if (hal_result != HAL_OK)
    {
        return SX1268_SPI_ERROR;
    }

    /* 休眠时 BUSY 保持高电平，唤醒不等待 BUSY。 */
    if (tx[0] == SX1268_CMD_SET_SLEEP)
    {
        return SX1268_OK;
    }

    return sx1268_wait_while_busy(SX1268_BUSY_TIMEOUT_MS);
}

static sx1268_result_t sx1268_write_command(uint8_t command,
                                             const uint8_t *data,
                                             uint8_t length)
{
    uint8_t tx[16] = {0};
    uint8_t rx[16] = {0};
    uint8_t i;

    if (length > 15U)
    {
        return SX1268_BAD_PARAM;
    }

    tx[0] = command;
    for (i = 0U; i < length; i++)
    {
        tx[i + 1U] = data[i];
    }

    return sx1268_transfer(tx, rx, (uint16_t)length + 1U);
}

static sx1268_result_t sx1268_write_register(uint16_t address,
                                              const uint8_t *data,
                                              uint8_t length)
{
    uint8_t tx[16] = {0};
    uint8_t rx[16] = {0};
    uint8_t i;

    if ((length == 0U) || (length > 13U))
    {
        return SX1268_BAD_PARAM;
    }

    tx[0] = SX1268_CMD_WRITE_REGISTER;
    tx[1] = (uint8_t)(address >> 8);
    tx[2] = (uint8_t)address;
    for (i = 0U; i < length; i++)
    {
        tx[i + 3U] = data[i];
    }

    return sx1268_transfer(tx, rx, (uint16_t)length + 3U);
}

static sx1268_result_t sx1268_write_buffer(const uint8_t *data, uint8_t length)
{
    uint8_t tx[258] = {0};
    uint8_t rx[258] = {0};
    uint16_t i;

    if ((data == 0) || (length == 0U))
    {
        return SX1268_BAD_PARAM;
    }

    tx[0] = SX1268_CMD_WRITE_BUFFER;
    tx[1] = 0x00U;
    for (i = 0U; i < length; i++)
    {
        tx[i + 2U] = data[i];
    }

    return sx1268_transfer(tx, rx, (uint16_t)length + 2U);
}

static void sx1268_hardware_reset(void)
{
    sx1268_is_sleeping = 0U;
    SX1268_FrontEndIdle();
    HAL_GPIO_WritePin(LoRa_CS_GPIO_Port, LoRa_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LoRa_NRST_GPIO_Port, LoRa_NRST_Pin, GPIO_PIN_RESET);
    HAL_Delay(2U);
    HAL_GPIO_WritePin(LoRa_NRST_GPIO_Port, LoRa_NRST_Pin, GPIO_PIN_SET);
    HAL_Delay(10U);
}

static sx1268_result_t sx1268_set_rf_frequency(uint32_t frequency_hz)
{
    uint32_t frequency_word;
    uint8_t parameters[4];

    frequency_word = (uint32_t)(((uint64_t)frequency_hz << 25) /
                                SX1268_XTAL_FREQ_HZ);
    parameters[0] = (uint8_t)(frequency_word >> 24);
    parameters[1] = (uint8_t)(frequency_word >> 16);
    parameters[2] = (uint8_t)(frequency_word >> 8);
    parameters[3] = (uint8_t)frequency_word;

    return sx1268_write_command(SX1268_CMD_SET_RF_FREQUENCY, parameters, 4U);
}

static sx1268_result_t sx1268_configure_lora(uint32_t frequency_hz,
                                              int8_t power_dbm)
{
    sx1268_result_t result;
    uint8_t parameters[8];
    uint8_t sync_word[2];

    parameters[0] = SX1268_STANDBY_RC;
    result = sx1268_write_command(SX1268_CMD_SET_STANDBY, parameters, 1U);
    if (result != SX1268_OK) return result;

    /* E22-400M22S 由 DIO3 供电给 32 MHz TCXO；10 ms 对应 640 个计时单位。 */
    parameters[0] = SX1268_TCXO_VOLTAGE_2V2;
    parameters[1] = 0x00U;
    parameters[2] = 0x02U;
    parameters[3] = 0x80U;
    result = sx1268_write_command(SX1268_CMD_SET_DIO3_TCXO_CTRL, parameters, 4U);
    if (result != SX1268_OK) return result;

    parameters[0] = SX1268_REGULATOR_DCDC;
    result = sx1268_write_command(SX1268_CMD_SET_REGULATOR_MODE, parameters, 1U);
    if (result != SX1268_OK) return result;

    parameters[0] = 0x7FU;
    result = sx1268_write_command(SX1268_CMD_CALIBRATE, parameters, 1U);
    if (result != SX1268_OK) return result;

    /* 470～510 MHz 频段的 Semtech 校准参数。 */
    parameters[0] = 0x75U;
    parameters[1] = 0x81U;
    result = sx1268_write_command(SX1268_CMD_CALIBRATE_IMAGE, parameters, 2U);
    if (result != SX1268_OK) return result;

    parameters[0] = SX1268_PACKET_TYPE_LORA;
    result = sx1268_write_command(SX1268_CMD_SET_PACKET_TYPE, parameters, 1U);
    if (result != SX1268_OK) return result;

    result = sx1268_set_rf_frequency(frequency_hz);
    if (result != SX1268_OK) return result;

    /* SX1268 的 ≤ +14 dBm 功放配置；本工程默认 +10 dBm。 */
    parameters[0] = 0x02U;
    parameters[1] = 0x02U;
    parameters[2] = 0x00U;
    parameters[3] = 0x01U;
    result = sx1268_write_command(SX1268_CMD_SET_PA_CONFIG, parameters, 4U);
    if (result != SX1268_OK) return result;

    parameters[0] = (uint8_t)power_dbm;
    parameters[1] = SX1268_RAMP_200_US;
    result = sx1268_write_command(SX1268_CMD_SET_TX_PARAMS, parameters, 2U);
    if (result != SX1268_OK) return result;

    /* SF7、125 kHz 带宽、4/5 编码率，不启用低数据率优化。 */
    parameters[0] = 0x07U;
    parameters[1] = 0x04U;
    parameters[2] = 0x01U;
    parameters[3] = 0x00U;
    result = sx1268_write_command(SX1268_CMD_SET_MODULATION_PARAMS,
                                  parameters, 4U);
    if (result != SX1268_OK) return result;

    parameters[0] = 0x00U;
    parameters[1] = 0x00U;
    result = sx1268_write_command(SX1268_CMD_SET_BUFFER_BASE, parameters, 2U);
    if (result != SX1268_OK) return result;

    sync_word[0] = (uint8_t)(SX1268_PRIVATE_SYNC_WORD >> 8);
    sync_word[1] = (uint8_t)SX1268_PRIVATE_SYNC_WORD;
    result = sx1268_write_register(SX1268_SYNC_WORD_ADDRESS, sync_word, 2U);
    if (result != SX1268_OK) return result;

    /* E22 使用独立 TXEN/RXEN，关闭 DIO2 射频开关控制。 */
    parameters[0] = 0x00U;
    result = sx1268_write_command(SX1268_CMD_SET_DIO2_RF_SWITCH,
                                  parameters, 1U);
    if (result != SX1268_OK) return result;

    parameters[0] = SX1268_FALLBACK_STDBY_RC;
    result = sx1268_write_command(SX1268_CMD_SET_FALLBACK_MODE,
                                  parameters, 1U);
    if (result != SX1268_OK) return result;

    /* 将发射完成与超时中断映射到 DIO1。 */
    parameters[0] = 0x02U;
    parameters[1] = 0x01U;
    parameters[2] = 0x02U;
    parameters[3] = 0x01U;
    parameters[4] = 0x00U;
    parameters[5] = 0x00U;
    parameters[6] = 0x00U;
    parameters[7] = 0x00U;
    result = sx1268_write_command(SX1268_CMD_SET_DIO_IRQ_PARAMS,
                                  parameters, 8U);
    if (result != SX1268_OK) return result;

    parameters[0] = (uint8_t)(SX1268_IRQ_ALL >> 8);
    parameters[1] = (uint8_t)SX1268_IRQ_ALL;
    return sx1268_write_command(SX1268_CMD_CLEAR_IRQ_STATUS, parameters, 2U);
}

sx1268_result_t SX1268_Init(uint32_t frequency_hz, int8_t power_dbm,
                            uint8_t *status)
{
    sx1268_result_t result;

    if ((frequency_hz < 410000000UL) || (frequency_hz > 493000000UL) ||
        (power_dbm < -9) || (power_dbm > 14))
    {
        return SX1268_BAD_PARAM;
    }

    sx1268_hardware_reset();
    result = sx1268_wait_while_busy(SX1268_BUSY_TIMEOUT_MS);
    if (result != SX1268_OK)
    {
        return result;
    }

    result = SX1268_GetStatus(status);
    if (result != SX1268_OK)
    {
        return result;
    }

    result = sx1268_configure_lora(frequency_hz, power_dbm);
    if (result != SX1268_OK)
    {
        return result;
    }

    SX1268_FrontEndIdle();
    return SX1268_GetStatus(status);
}

sx1268_result_t SX1268_GetStatus(uint8_t *status)
{
    uint8_t tx[2] = {SX1268_CMD_GET_STATUS, 0x00U};
    uint8_t rx[2] = {0};
    sx1268_result_t result;

    if (status == 0)
    {
        return SX1268_BAD_PARAM;
    }

    result = sx1268_transfer(tx, rx, 2U);
    if (result == SX1268_OK)
    {
        *status = rx[1];
        if ((*status == 0x00U) || (*status == 0xFFU))
        {
            return SX1268_NOT_FOUND;
        }
    }

    return result;
}

sx1268_result_t SX1268_StartTx(const uint8_t *payload, uint8_t length,
                               uint32_t timeout_ms)
{
    sx1268_result_t result;
    uint32_t radio_timeout;
    uint8_t parameters[6];

    if ((payload == 0) || (length == 0U) || (timeout_ms == 0U))
    {
        return SX1268_BAD_PARAM;
    }

    result = sx1268_write_buffer(payload, length);
    if (result != SX1268_OK) return result;

    /* 8 符号前导码、显式包头、启用 CRC、正常 IQ。 */
    parameters[0] = 0x00U;
    parameters[1] = 0x08U;
    parameters[2] = 0x00U;
    parameters[3] = length;
    parameters[4] = 0x01U;
    parameters[5] = 0x00U;
    result = sx1268_write_command(SX1268_CMD_SET_PACKET_PARAMS,
                                  parameters, 6U);
    if (result != SX1268_OK) return result;

    parameters[0] = (uint8_t)(SX1268_IRQ_ALL >> 8);
    parameters[1] = (uint8_t)SX1268_IRQ_ALL;
    result = sx1268_write_command(SX1268_CMD_CLEAR_IRQ_STATUS,
                                  parameters, 2U);
    if (result != SX1268_OK) return result;

    HAL_GPIO_WritePin(LoRa_RXEN_GPIO_Port, LoRa_RXEN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LoRa_TXEN_GPIO_Port, LoRa_TXEN_Pin, GPIO_PIN_SET);

    /* SetTx 超时单位为 15.625 µs，每毫秒 64 个计时单位。 */
    radio_timeout = timeout_ms * 64UL;
    if (radio_timeout > 0xFFFFFFUL)
    {
        radio_timeout = 0xFFFFFFUL;
    }
    parameters[0] = (uint8_t)(radio_timeout >> 16);
    parameters[1] = (uint8_t)(radio_timeout >> 8);
    parameters[2] = (uint8_t)radio_timeout;

    result = sx1268_write_command(SX1268_CMD_SET_TX, parameters, 3U);
    if (result != SX1268_OK)
    {
        SX1268_FrontEndIdle();
    }

    return result;
}

sx1268_result_t SX1268_GetAndClearIrq(uint16_t *irq)
{
    uint8_t tx[4] = {SX1268_CMD_GET_IRQ_STATUS, 0x00U, 0x00U, 0x00U};
    uint8_t rx[4] = {0};
    uint8_t parameters[2];
    sx1268_result_t result;

    if (irq == 0)
    {
        return SX1268_BAD_PARAM;
    }

    result = sx1268_transfer(tx, rx, 4U);
    if (result != SX1268_OK)
    {
        return result;
    }

    *irq = ((uint16_t)rx[2] << 8) | rx[3];
    parameters[0] = rx[2];
    parameters[1] = rx[3];
    return sx1268_write_command(SX1268_CMD_CLEAR_IRQ_STATUS, parameters, 2U);
}

sx1268_result_t SX1268_Standby(void)
{
    uint8_t parameter = SX1268_STANDBY_RC;
    sx1268_result_t result;

    result = sx1268_write_command(SX1268_CMD_SET_STANDBY, &parameter, 1U);
    SX1268_FrontEndIdle();
    return result;
}

sx1268_result_t SX1268_SleepWarm(void)
{
    uint8_t parameter = SX1268_SLEEP_WARM_START;
    sx1268_result_t result;

    if (sx1268_is_sleeping != 0U)
    {
        return SX1268_OK;
    }

    SX1268_FrontEndIdle();
    result = sx1268_write_command(SX1268_CMD_SET_SLEEP, &parameter, 1U);
    if (result == SX1268_OK)
    {
        sx1268_is_sleeping = 1U;
        /* 等待模块保存暖启动配置后再返回。 */
        HAL_Delay(2U);
    }
    return result;
}

void SX1268_FrontEndIdle(void)
{
    HAL_GPIO_WritePin(LoRa_TXEN_GPIO_Port, LoRa_TXEN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LoRa_RXEN_GPIO_Port, LoRa_RXEN_Pin, GPIO_PIN_RESET);
}

const char *SX1268_ResultString(sx1268_result_t result)
{
    switch (result)
    {
        case SX1268_OK:           return "OK";
        case SX1268_BUSY_TIMEOUT: return "BUSY_TIMEOUT";
        case SX1268_SPI_ERROR:    return "SPI_ERROR";
        case SX1268_BAD_PARAM:    return "BAD_PARAM";
        case SX1268_NOT_FOUND:    return "NOT_FOUND";
        default:                  return "UNKNOWN";
    }
}
