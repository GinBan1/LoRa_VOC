#include "oled_display.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "lora_receiver.h"
#include "onenet.h"

#define OLED_I2C_PORT             I2C_NUM_0
#define OLED_PIN_SDA              GPIO_NUM_21
#define OLED_PIN_SCL              GPIO_NUM_22
#define OLED_I2C_FREQUENCY_HZ     400000U
#define OLED_WIDTH                128U
#define OLED_HEIGHT               64U
#define OLED_PAGE_COUNT           (OLED_HEIGHT / 8U)
#define OLED_TEXT_COLUMNS         21U
#define OLED_TIMEOUT_MS           100U

static const char *TAG = "OLED";
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_oled_device;
static uint8_t s_framebuffer[OLED_WIDTH * OLED_PAGE_COUNT];
static bool s_initialized;

/* Public-domain 5x7 ASCII glyphs, space (0x20) through underscore (0x5F). */
static const uint8_t s_font5x7[64][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
    {0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},
    {0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40}
};

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

static void clear_framebuffer(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

static void draw_character(uint8_t column, uint8_t page, char character)
{
    uint8_t glyph_index;
    size_t framebuffer_index;

    if ((column >= OLED_TEXT_COLUMNS) || (page >= OLED_PAGE_COUNT))
    {
        return;
    }

    character = (char)toupper((unsigned char)character);
    if ((character < ' ') || (character > '_'))
    {
        character = '?';
    }
    glyph_index = (uint8_t)(character - ' ');
    framebuffer_index = (size_t)page * OLED_WIDTH + (size_t)column * 6U;

    memcpy(&s_framebuffer[framebuffer_index], s_font5x7[glyph_index], 5U);
    s_framebuffer[framebuffer_index + 5U] = 0x00U;
}

static void draw_text(uint8_t page, const char *text)
{
    if ((text == NULL) || (page >= OLED_PAGE_COUNT))
    {
        return;
    }

    for (uint8_t column = 0U;
         (column < OLED_TEXT_COLUMNS) && (text[column] != '\0');
         ++column)
    {
        draw_character(column, page, text[column]);
    }
}

static void set_pixel(uint16_t x, uint16_t y)
{
    size_t framebuffer_index;
    uint8_t pixel_mask;

    if ((x >= OLED_WIDTH) || (y >= OLED_HEIGHT))
    {
        return;
    }

    framebuffer_index = ((size_t)y / 8U) * OLED_WIDTH + (size_t)x;
    pixel_mask = (uint8_t)(1U << (y & 0x07U));
    s_framebuffer[framebuffer_index] |= pixel_mask;
}

/*
 * Draw one 5x7 glyph at 2x scale inside a 12x16 character cell.
 * set_pixel() clips every write, so a truncated string can never overrun the
 * framebuffer even when a formatted counter grows wider than the display.
 */
static void draw_character_2x(uint16_t x, uint16_t y, char character)
{
    uint8_t glyph_index;

    character = (char)toupper((unsigned char)character);
    if ((character < ' ') || (character > '_'))
    {
        character = '?';
    }
    glyph_index = (uint8_t)(character - ' ');

    for (uint8_t glyph_column = 0U; glyph_column < 5U; ++glyph_column)
    {
        uint8_t glyph_bits = s_font5x7[glyph_index][glyph_column];

        for (uint8_t glyph_row = 0U; glyph_row < 7U; ++glyph_row)
        {
            if ((glyph_bits & (uint8_t)(1U << glyph_row)) != 0U)
            {
                uint16_t pixel_x = x + (uint16_t)glyph_column * 2U;
                uint16_t pixel_y = y + (uint16_t)glyph_row * 2U;

                set_pixel(pixel_x,      pixel_y);
                set_pixel(pixel_x + 1U, pixel_y);
                set_pixel(pixel_x,      pixel_y + 1U);
                set_pixel(pixel_x + 1U, pixel_y + 1U);
            }
        }
    }
}

static void draw_text_2x(uint16_t x, uint16_t y, const char *text)
{
    if (text == NULL)
    {
        return;
    }

    while ((*text != '\0') && (x < OLED_WIDTH))
    {
        draw_character_2x(x, y, *text);
        x += 12U;
        ++text;
    }
}

static void draw_text_2x_compact(uint16_t x, uint16_t y, const char *text)
{
    if (text == NULL)
    {
        return;
    }

    while ((*text != '\0') && (x < OLED_WIDTH))
    {
        draw_character_2x(x, y, *text);
        x += 11U;
        ++text;
    }
}

static const char *ai_state_text(uint8_t state)
{
    switch (state)
    {
        case TELEMETRY_AI_WARMUP:
        case TELEMETRY_AI_COLLECT:
            return "WARMUP";
        case TELEMETRY_AI_NORMAL:
            return "NORMAL";
        case TELEMETRY_AI_WARNING:
            return "WARNING";
        case TELEMETRY_AI_UNCERTAIN:
            return "UNCERT";
        case TELEMETRY_AI_ERROR:
        default:
            return "ERROR";
    }
}

static void draw_centered_text_2x(uint16_t y, const char *text)
{
    size_t text_length = strlen(text);
    size_t text_width = (text_length == 0U)
        ? 0U
        : ((text_length - 1U) * 12U + 10U);
    uint16_t x = (text_width < OLED_WIDTH)
        ? (uint16_t)((OLED_WIDTH - text_width) / 2U)
        : 0U;

    draw_text_2x(x, y, text);
}

static void draw_centered_text_2x_compact(uint16_t y, const char *text)
{
    size_t text_length = strlen(text);
    size_t text_width = (text_length == 0U)
        ? 0U
        : ((text_length - 1U) * 11U + 10U);
    uint16_t x = (text_width < OLED_WIDTH)
        ? (uint16_t)((OLED_WIDTH - text_width) / 2U)
        : 0U;

    draw_text_2x_compact(x, y, text);
}

static void render_screen(void)
{
    lora_receiver_snapshot_t snapshot;
    char line[32];
    const char *display_state;

    lora_receiver_get_snapshot(&snapshot);
    clear_framebuffer();

    snprintf(line, sizeof(line), "NET:%s LORA:%s",
             OneNet_Ready ? "OK" : "OFF",
             snapshot.radio_ready ? "OK" : "ERR");
    draw_text(0U, line);

    if (snapshot.telemetry_valid &&
        ((snapshot.telemetry.flags & TELEMETRY_FLAG_BME680_VALID) != 0U))
    {
        if (snapshot.telemetry.humidity_x10 < 1000U)
        {
            snprintf(line, sizeof(line), "T%.1f H%.1f",
                     (double)snapshot.telemetry.temperature_x10 / 10.0,
                     (double)snapshot.telemetry.humidity_x10 / 10.0);
        }
        else
        {
            snprintf(line, sizeof(line), "T%.1f H%.0f",
                     (double)snapshot.telemetry.temperature_x10 / 10.0,
                     (double)snapshot.telemetry.humidity_x10 / 10.0);
        }
    }
    else
    {
        snprintf(line, sizeof(line), "T--.- H--.-");
    }
    draw_centered_text_2x_compact(10U, line);

    if (!snapshot.telemetry_valid)
    {
        display_state = "WAIT";
    }
    else if (!snapshot.telemetry_fresh)
    {
        display_state = "STALE";
    }
    else if ((snapshot.telemetry.flags & TELEMETRY_FLAG_AI_VALID) == 0U)
    {
        if (snapshot.telemetry.ai_state == TELEMETRY_AI_WARMUP ||
            snapshot.telemetry.ai_state == TELEMETRY_AI_COLLECT)
        {
            display_state = "WARMUP";
        }
        else
        {
            display_state = "WAIT";
        }
    }
    else
    {
        display_state = ai_state_text(snapshot.telemetry.ai_state);
    }
    draw_centered_text_2x(30U, display_state);

    if (snapshot.telemetry_valid &&
        ((snapshot.telemetry.flags & TELEMETRY_FLAG_SGP40_VALID) != 0U))
    {
        snprintf(line, sizeof(line), "V%u R%d",
                 (unsigned int)snapshot.telemetry.voc_index,
                 snapshot.rssi_dbm);
    }
    else if (snapshot.telemetry_valid)
    {
        snprintf(line, sizeof(line), "V-- R%d", snapshot.rssi_dbm);
    }
    else
    {
        snprintf(line, sizeof(line), "V-- R--");
    }
    draw_centered_text_2x_compact(49U, line);
}

esp_err_t oled_display_init(void)
{
    static const uint8_t initialization[] = {
        0xAEU,       /* display off */
        0xD5U, 0x80U,
        0xA8U, 0x3FU,
        0xD3U, 0x00U,
        0x40U,
        0x8DU, 0x14U,
        0x20U, 0x02U, /* page addressing mode */
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
