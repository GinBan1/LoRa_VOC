#include "oled_display.h"

#include <stdbool.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lora_receiver.h"
#include "oled_dashboard.h"
#include "onenet.h"

#define OLED_I2C_PORT             I2C_NUM_0
#define OLED_PIN_SDA              GPIO_NUM_21
#define OLED_PIN_SCL              GPIO_NUM_22
#define OLED_I2C_FREQUENCY_HZ     400000U
#define OLED_WIDTH                128U
#define OLED_HEIGHT               64U
#define OLED_PAGE_COUNT           (OLED_HEIGHT / 8U)
#define OLED_TIMEOUT_MS           100U

static const char *TAG = "OLED";
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_oled_device;
static uint8_t s_framebuffer[OLED_WIDTH * OLED_PAGE_COUNT];
static bool s_initialized;


static esp_err_t send_command(uint8_t command)
{
    uint8_t message[2] = {0x00U, command};
    return i2c_master_transmit(s_oled_device, message, sizeof(message), OLED_TIMEOUT_MS);
}

static esp_err_t send_commands(const uint8_t *commands, size_t count)
{
    for (size_t i = 0U; i < count; ++i)
    {
        esp_err_t error = send_command(commands[i]);
        if (error != ESP_OK)
        {
            return error;
        }
    }
    return ESP_OK;
}

static esp_err_t flush_framebuffer(void)
{
    uint8_t page_data[OLED_WIDTH + 1U];

    page_data[0] = 0x40U;
    for (uint8_t page = 0U; page < OLED_PAGE_COUNT; ++page)
    {
        const uint8_t address_commands[] = {
            (uint8_t)(0xB0U + page),
            0x00U,
            0x10U
        };
        esp_err_t error = send_commands(address_commands, sizeof(address_commands));
        if (error != ESP_OK)
        {
            return error;
        }

        memcpy(&page_data[1], &s_framebuffer[page * OLED_WIDTH], OLED_WIDTH);
        error = i2c_master_transmit(s_oled_device,
                                    page_data,
                                    sizeof(page_data),
                                    OLED_TIMEOUT_MS);
        if (error != ESP_OK)
        {
            return error;
        }
    }
    return ESP_OK;
}

static void render_screen(void)
{
    lora_receiver_snapshot_t snapshot;
    oled_dashboard_view_t view;

    lora_receiver_get_snapshot(&snapshot);
    view.radio_ready = snapshot.radio_ready;
    /* 云图标表示已连线，不代表每帧已入库。 */
    view.cloud_ready = OneNet_Ready;
    view.telemetry_valid = snapshot.telemetry_valid;
    view.telemetry_fresh = snapshot.telemetry_fresh;
    view.rx_age_ms = (uint32_t)((uint32_t)(esp_timer_get_time() / 1000) -
                              snapshot.last_rx_ms);
    view.rssi_dbm = snapshot.rssi_dbm;
    view.telemetry = snapshot.telemetry;
    (void)oled_dashboard_render(s_framebuffer, sizeof(s_framebuffer), &view);
}

esp_err_t oled_display_init(void)
{
    static const uint8_t initialization[] = {
        0xAEU,       /* 关闭显示。 */
        0xD5U, 0x80U,
        0xA8U, 0x3FU,
        0xD3U, 0x00U,
        0x40U,
        0x8DU, 0x14U,
        0x20U, 0x02U, /* 使用页寻址。 */
        0xA1U,
        0xC8U,
        0xDAU, 0x12U,
        0x81U, 0x7FU,
        0xD9U, 0xF1U,
        0xDBU, 0x40U,
        0xA4U,
        0xA6U,
        0x2EU,
        0xAFU
    };
    i2c_master_bus_config_t bus_config = {
        .i2c_port = OLED_I2C_PORT,
        .sda_io_num = OLED_PIN_SDA,
        .scl_io_num = OLED_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz = OLED_I2C_FREQUENCY_HZ,
    };
    uint8_t address = 0x3CU;
    esp_err_t error;

    if (s_initialized)
    {
        return ESP_OK;
    }

    error = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (error != ESP_OK)
    {
        return error;
    }

    error = i2c_master_probe(s_i2c_bus, address, OLED_TIMEOUT_MS);
    if (error != ESP_OK)
    {
        address = 0x3DU;
        error = i2c_master_probe(s_i2c_bus, address, OLED_TIMEOUT_MS);
    }
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "SSD1306 not found at 0x3C or 0x3D");
        return ESP_ERR_NOT_FOUND;
    }

    device_config.device_address = address;
    error = i2c_master_bus_add_device(s_i2c_bus, &device_config, &s_oled_device);
    if (error != ESP_OK)
    {
        return error;
    }
    error = send_commands(initialization, sizeof(initialization));
    if (error != ESP_OK)
    {
        return error;
    }

    s_initialized = true;
    render_screen();
    error = flush_framebuffer();
    if (error == ESP_OK)
    {
        ESP_LOGI(TAG, "SSD1306 ready at I2C address 0x%02X", address);
    }
    return error;
}

void oled_display_task(void)
{
    if (!s_initialized)
    {
        return;
    }

    render_screen();
    esp_err_t error = flush_framebuffer();
    if (error != ESP_OK)
    {
        ESP_LOGW(TAG, "display refresh failed: %s", esp_err_to_name(error));
    }
}
