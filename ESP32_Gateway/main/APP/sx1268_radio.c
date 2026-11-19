#include "sx1268_radio.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LORA_SPI_HOST                 SPI2_HOST
#define LORA_PIN_MOSI                 GPIO_NUM_23
#define LORA_PIN_MISO                 GPIO_NUM_19
#define LORA_PIN_SCK                  GPIO_NUM_18
#define LORA_PIN_NSS                  GPIO_NUM_27
#define LORA_PIN_BUSY                 GPIO_NUM_34
#define LORA_PIN_TXEN                 GPIO_NUM_32
#define LORA_PIN_RXEN                 GPIO_NUM_33
#define LORA_PIN_NRST                 GPIO_NUM_25
#define LORA_PIN_DIO1                 GPIO_NUM_26

#define SX1268_FREQUENCY_HZ           470300000UL
#define SX1268_XTAL_FREQUENCY_HZ      32000000ULL
#define SX1268_PRIVATE_SYNC_WORD      0x1424U
#define SX1268_SYNC_WORD_ADDRESS      0x0740U
#define SX1268_BUSY_TIMEOUT_MS        150U

#define CMD_SET_STANDBY               0x80U
#define CMD_SET_RX                    0x82U
#define CMD_SET_PACKET_TYPE           0x8AU
#define CMD_SET_RF_FREQUENCY          0x86U
#define CMD_SET_MODULATION_PARAMS     0x8BU
#define CMD_SET_PACKET_PARAMS         0x8CU
#define CMD_SET_BUFFER_BASE_ADDRESS   0x8FU
#define CMD_SET_REGULATOR_MODE        0x96U
#define CMD_SET_DIO3_TCXO_CTRL        0x97U
#define CMD_CALIBRATE                 0x89U
#define CMD_CALIBRATE_IMAGE           0x98U
#define CMD_SET_DIO_IRQ_PARAMS        0x08U
#define CMD_CLEAR_IRQ_STATUS          0x02U
#define CMD_GET_IRQ_STATUS            0x12U
#define CMD_GET_RX_BUFFER_STATUS      0x13U
#define CMD_GET_PACKET_STATUS         0x14U
#define CMD_GET_STATUS                0xC0U
#define CMD_READ_BUFFER               0x1EU
#define CMD_WRITE_REGISTER            0x0DU
#define CMD_SET_DIO2_RF_SWITCH        0x9DU
#define CMD_SET_FALLBACK_MODE         0x93U

#define IRQ_RX_DONE                   0x0002U
#define IRQ_HEADER_ERR                0x0020U
#define IRQ_CRC_ERR                   0x0040U
#define IRQ_TIMEOUT                   0x0200U
#define IRQ_RX_MASK                   (IRQ_RX_DONE | IRQ_HEADER_ERR | IRQ_CRC_ERR | IRQ_TIMEOUT)
#define IRQ_ALL                       0x03FFU

static spi_device_handle_t s_spi;
static bool s_initialized;

static esp_err_t wait_while_busy(uint32_t timeout_ms)
{
    const int64_t deadline = esp_timer_get_time() + ((int64_t)timeout_ms * 1000);

    while (gpio_get_level(LORA_PIN_BUSY) != 0)
    {
        if (esp_timer_get_time() >= deadline)
        {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}

static esp_err_t radio_transfer(const uint8_t *tx, uint8_t *rx, size_t length)
{
    spi_transaction_t transaction = {0};
    esp_err_t error;

    if ((s_spi == NULL) || (tx == NULL) || (length == 0U))
    {
        return ESP_ERR_INVALID_STATE;
    }

    error = wait_while_busy(SX1268_BUSY_TIMEOUT_MS);
    if (error != ESP_OK)
    {
        return error;
    }

    transaction.length = length * 8U;
    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;
    error = spi_device_polling_transmit(s_spi, &transaction);
    if (error != ESP_OK)
    {
        return error;
    }

    return wait_while_busy(SX1268_BUSY_TIMEOUT_MS);
}

static esp_err_t write_command(uint8_t command, const uint8_t *data, size_t length)
{
    uint8_t tx[16] = {0};
    uint8_t rx[16] = {0};

    if (length > (sizeof(tx) - 1U))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    tx[0] = command;
    if ((data != NULL) && (length > 0U))
    {
        memcpy(&tx[1], data, length);
    }
    return radio_transfer(tx, rx, length + 1U);
}

static esp_err_t write_register(uint16_t address, const uint8_t *data, size_t length)
{
    uint8_t tx[16] = {0};
    uint8_t rx[16] = {0};

    if ((data == NULL) || (length == 0U) || (length > (sizeof(tx) - 3U)))
    {
        return ESP_ERR_INVALID_ARG;
    }

    tx[0] = CMD_WRITE_REGISTER;
    tx[1] = (uint8_t)(address >> 8);
    tx[2] = (uint8_t)address;
    memcpy(&tx[3], data, length);
    return radio_transfer(tx, rx, length + 3U);
}

static esp_err_t get_status(uint8_t *status)
{
    uint8_t tx[2] = {CMD_GET_STATUS, 0x00U};
    uint8_t rx[2] = {0};
    esp_err_t error;

    if (status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    error = radio_transfer(tx, rx, sizeof(tx));
    if (error == ESP_OK)
    {
        *status = rx[1];
        if ((*status == 0x00U) || (*status == 0xFFU))
        {
            return ESP_ERR_NOT_FOUND;
        }
    }
    return error;
}

static esp_err_t get_irq_status(uint16_t *irq_status)
{
    uint8_t tx[4] = {CMD_GET_IRQ_STATUS, 0x00U, 0x00U, 0x00U};
    uint8_t rx[4] = {0};
    esp_err_t error;

    if (irq_status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    error = radio_transfer(tx, rx, sizeof(tx));
    if (error == ESP_OK)
    {
        *irq_status = ((uint16_t)rx[2] << 8) | rx[3];
    }
    return error;
}

static esp_err_t clear_irq_status(uint16_t irq_status)
{
    uint8_t parameters[2] = {
        (uint8_t)(irq_status >> 8),
        (uint8_t)irq_status
    };
    return write_command(CMD_CLEAR_IRQ_STATUS, parameters, sizeof(parameters));
}

static esp_err_t set_rf_frequency(uint32_t frequency_hz)
{
    uint32_t frequency_word;
    uint8_t parameters[4];

    frequency_word = (uint32_t)(((uint64_t)frequency_hz << 25) /
                                SX1268_XTAL_FREQUENCY_HZ);
    parameters[0] = (uint8_t)(frequency_word >> 24);
    parameters[1] = (uint8_t)(frequency_word >> 16);
    parameters[2] = (uint8_t)(frequency_word >> 8);
    parameters[3] = (uint8_t)frequency_word;
    return write_command(CMD_SET_RF_FREQUENCY, parameters, sizeof(parameters));
}

static void hardware_reset(void)
{
    gpio_set_level(LORA_PIN_TXEN, 0);
    gpio_set_level(LORA_PIN_RXEN, 0);
    gpio_set_level(LORA_PIN_NRST, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(LORA_PIN_NRST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static esp_err_t configure_radio(void)
{
    uint8_t p[8];
    uint8_t sync_word[2] = {
        (uint8_t)(SX1268_PRIVATE_SYNC_WORD >> 8),
        (uint8_t)SX1268_PRIVATE_SYNC_WORD
    };
    esp_err_t error;

#define TRY(command_call) do { error = (command_call); if (error != ESP_OK) return error; } while (0)

    p[0] = 0x00U; /* STDBY_RC */
    TRY(write_command(CMD_SET_STANDBY, p, 1U));

    /* E22-400M22S 的 32 MHz TCXO 由 DIO3 提供 2.2 V。 */
    p[0] = 0x03U;
    p[1] = 0x00U;
    p[2] = 0x02U;
    p[3] = 0x80U; /* 等待 10 ms。 */
    TRY(write_command(CMD_SET_DIO3_TCXO_CTRL, p, 4U));

    p[0] = 0x01U; /* DCDC */
    TRY(write_command(CMD_SET_REGULATOR_MODE, p, 1U));

    p[0] = 0x7FU;
    TRY(write_command(CMD_CALIBRATE, p, 1U));
    p[0] = 0x75U;
    p[1] = 0x81U; /* 470–510 MHz 镜像校准。 */
    TRY(write_command(CMD_CALIBRATE_IMAGE, p, 2U));

    p[0] = 0x01U; /* 选择 LoRa 包类型。 */
    TRY(write_command(CMD_SET_PACKET_TYPE, p, 1U));
    TRY(set_rf_frequency(SX1268_FREQUENCY_HZ));

    /* SF7、带宽 125 kHz、编码率 4/5，不开 LDRO。 */
    p[0] = 0x07U;
    p[1] = 0x04U;
    p[2] = 0x01U;
    p[3] = 0x00U;
    TRY(write_command(CMD_SET_MODULATION_PARAMS, p, 4U));

    p[0] = 0x00U;
    p[1] = 0x00U;
    TRY(write_command(CMD_SET_BUFFER_BASE_ADDRESS, p, 2U));
    TRY(write_register(SX1268_SYNC_WORD_ADDRESS, sync_word, sizeof(sync_word)));

    /* 用 E22 的 TXEN/RXEN 控制射频开关，不由 DIO2 控制。 */
    p[0] = 0x00U;
    TRY(write_command(CMD_SET_DIO2_RF_SWITCH, p, 1U));
    p[0] = 0x20U; /* 操作后返回 STDBY_RC。 */
    TRY(write_command(CMD_SET_FALLBACK_MODE, p, 1U));

    /* 8 符号前导码，显式头，最多 255 字节，启用 CRC，IQ 不翻转。 */
    p[0] = 0x00U;
    p[1] = 0x08U;
    p[2] = 0x00U;
    p[3] = 0xFFU;
    p[4] = 0x01U;
    p[5] = 0x00U;
    TRY(write_command(CMD_SET_PACKET_PARAMS, p, 6U));

    p[0] = (uint8_t)(IRQ_RX_MASK >> 8);
    p[1] = (uint8_t)IRQ_RX_MASK;
    p[2] = p[0];
    p[3] = p[1]; /* 接收中断映射到 DIO1。 */
    p[4] = 0x00U;
    p[5] = 0x00U;
    p[6] = 0x00U;
    p[7] = 0x00U;
    TRY(write_command(CMD_SET_DIO_IRQ_PARAMS, p, 8U));
    TRY(clear_irq_status(IRQ_ALL));

#undef TRY
    return ESP_OK;
}

esp_err_t sx1268_radio_init(uint8_t *status)
{
    spi_bus_config_t bus_config = {
        .mosi_io_num = LORA_PIN_MOSI,
        .miso_io_num = LORA_PIN_MISO,
        .sclk_io_num = LORA_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SX1268_RX_MAX_PAYLOAD + 3U,
    };
    spi_device_interface_config_t device_config = {
        .clock_speed_hz = 8 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = LORA_PIN_NSS,
        .queue_size = 1,
    };
    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << LORA_PIN_TXEN) |
                        (1ULL << LORA_PIN_RXEN) |
                        (1ULL << LORA_PIN_NRST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << LORA_PIN_BUSY) |
                        (1ULL << LORA_PIN_DIO1),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t error;

    if (status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_initialized)
    {
        return get_status(status);
    }

    ESP_RETURN_ON_ERROR(gpio_config(&output_config), "SX1268", "output GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_config(&input_config), "SX1268", "input GPIO config failed");
    gpio_set_level(LORA_PIN_TXEN, 0);
    gpio_set_level(LORA_PIN_RXEN, 0);
    gpio_set_level(LORA_PIN_NRST, 1);

    error = spi_bus_initialize(LORA_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if ((error != ESP_OK) && (error != ESP_ERR_INVALID_STATE))
    {
        return error;
    }
    error = spi_bus_add_device(LORA_SPI_HOST, &device_config, &s_spi);
    if (error != ESP_OK)
    {
        return error;
    }

    hardware_reset();
    ESP_RETURN_ON_ERROR(get_status(status), "SX1268", "radio not found");
    ESP_RETURN_ON_ERROR(configure_radio(), "SX1268", "radio profile failed");
    s_initialized = true;
    ESP_RETURN_ON_ERROR(sx1268_radio_start_receive(), "SX1268", "RX start failed");
    return get_status(status);
}

esp_err_t sx1268_radio_start_receive(void)
{
    uint8_t timeout[3] = {0xFFU, 0xFFU, 0xFFU}; /* 连续接收。 */

    if (!s_initialized && (s_spi == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    gpio_set_level(LORA_PIN_TXEN, 0);
    gpio_set_level(LORA_PIN_RXEN, 1);
    return write_command(CMD_SET_RX, timeout, sizeof(timeout));
}

static esp_err_t read_received_packet(sx1268_rx_packet_t *packet)
{
    uint8_t status_tx[4] = {CMD_GET_RX_BUFFER_STATUS, 0x00U, 0x00U, 0x00U};
    uint8_t status_rx[4] = {0};
    uint8_t packet_tx[5] = {CMD_GET_PACKET_STATUS, 0x00U, 0x00U, 0x00U, 0x00U};
    uint8_t packet_rx[5] = {0};
    uint8_t read_tx[SX1268_RX_MAX_PAYLOAD + 3U] = {0};
    uint8_t read_rx[SX1268_RX_MAX_PAYLOAD + 3U] = {0};
    uint8_t payload_length;
    uint8_t start_pointer;
    esp_err_t error;

    error = radio_transfer(status_tx, status_rx, sizeof(status_tx));
    if (error != ESP_OK)
    {
        return error;
    }
    payload_length = status_rx[2];
    start_pointer = status_rx[3];

    read_tx[0] = CMD_READ_BUFFER;
    read_tx[1] = start_pointer;
    read_tx[2] = 0x00U;
    error = radio_transfer(read_tx, read_rx, (size_t)payload_length + 3U);
    if (error != ESP_OK)
    {
        return error;
    }
    memcpy(packet->data, &read_rx[3], payload_length);
    packet->length = payload_length;

    error = radio_transfer(packet_tx, packet_rx, sizeof(packet_tx));
    if (error != ESP_OK)
    {
        return error;
    }
    packet->rssi_dbm = -(int16_t)packet_rx[2] / 2;
    packet->snr_db = (float)(int8_t)packet_rx[3] / 4.0f;
    return ESP_OK;
}

esp_err_t sx1268_radio_poll(sx1268_rx_packet_t *packet, bool *packet_ready)
{
    uint16_t irq_status;
    esp_err_t error;

    if ((packet == NULL) || (packet_ready == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }
    *packet_ready = false;

    /* 清除对应中断后，DIO1 才会撤销。 */
    if (gpio_get_level(LORA_PIN_DIO1) == 0)
    {
        return ESP_OK;
    }

    error = get_irq_status(&irq_status);
    if (error != ESP_OK)
    {
        return error;
    }
    packet->irq_status = irq_status;

    if ((irq_status & IRQ_RX_DONE) != 0U &&
        (irq_status & (IRQ_HEADER_ERR | IRQ_CRC_ERR)) == 0U)
    {
        error = read_received_packet(packet);
        if (error == ESP_OK)
        {
            *packet_ready = true;
        }
    }
    else if ((irq_status & (IRQ_HEADER_ERR | IRQ_CRC_ERR)) != 0U)
    {
        error = ESP_ERR_INVALID_CRC;
    }

    (void)clear_irq_status(irq_status == 0U ? IRQ_ALL : irq_status);
    (void)sx1268_radio_start_receive();
    return error;
}

const char *sx1268_radio_error_name(esp_err_t error)
{
    switch (error)
    {
        case ESP_OK: return "OK";
        case ESP_ERR_TIMEOUT: return "BUSY_TIMEOUT";
        case ESP_ERR_NOT_FOUND: return "NOT_FOUND";
        case ESP_ERR_INVALID_CRC: return "CRC_OR_HEADER";
        case ESP_ERR_INVALID_STATE: return "INVALID_STATE";
        default: return esp_err_to_name(error);
    }
}
