/*
 * oled.c
 *
 *  Created on: Nov 17, 2024
 *      Author: 阿妈粽
 */

#include "oled.h"
#include "app_voc_service.h"
#include "oled_node.h"

Environment_Data_t g_env_data = {0};

static const uint8_t init_cmd[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0xA1, 0xC8, 0xDA,
    0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0x8D, 0x14, 0xAF
};

static void oled_cmd(uint8_t cmd)
{
    HAL_I2C_Mem_Write(&hi2c1, OLED_I2C_ADDR, 0x00U, I2C_MEMADD_SIZE_8BIT, &cmd, 1, 0x100U);
}

/* 上电等待后初始化 SSD1306。 */
void OLED_Init(void)
{
    HAL_Delay(100U);
    for (uint8_t i = 0; i < sizeof(init_cmd); ++i)
        oled_cmd(init_cmd[i]);
}

/* 清空八页显存。 */
void OLED_Clear(void)
{
    uint8_t data = 0U;
    for (uint8_t page = 0; page < 8U; ++page)
    {
        oled_cmd((uint8_t)(0xB0U + page));
        oled_cmd(0x00U);
        oled_cmd(0x10U);
        for (uint8_t col = 0; col < 128U; ++col)
            HAL_I2C_Mem_Write(&hi2c1, OLED_I2C_ADDR, 0x40U, I2C_MEMADD_SIZE_8BIT, &data, 1, 0x100U);
    }
}

/* 打开升压电路与显示。 */
void OLED_Display_On(void)
{
    oled_cmd(0x8DU);
    oled_cmd(0x14U);
    oled_cmd(0xAFU);
}

/* 关闭升压电路与显示，不切断模块供电。 */
void OLED_Display_Off(void)
{
    oled_cmd(0x8DU);
    oled_cmd(0x10U);
    oled_cmd(0xAEU);
}

static void oled_pos(uint8_t x, uint8_t page)
{
    oled_cmd((uint8_t)(0xB0U + page));
    oled_cmd((uint8_t)(((x & 0xF0U) >> 4) | 0x10U));
    oled_cmd(x & 0x0FU);
}

/* 读取节点状态并刷新 OLED 仪表盘。 */
void lcd_proc(void)
{
    static uint8_t oled_buffer[OLED_NODE_BUFFER_SIZE];
    AppVocServiceSnapshot event;
    AppVocAiView ai;
    OLED_Node_View_t view = {0};
    uint8_t page;
    uint32_t now = HAL_GetTick();

    voc_svc_read(&event, now);
    voc_svc_ai_view(&ai, now);
    view.temperature = event.temperature;
    view.humidity = event.humidity;
    view.pressure = event.pressure;
    view.voc_index = event.voc_index;
    view.environment_valid = event.bme_valid;
    view.voc_valid = event.sgp_valid;
    view.fresh = (uint8_t)((event.sample_sequence != 0U) &&
                 ((uint32_t)(now - event.sample_ms) <= 2500U));
    view.event_mode = 2U;
    view.event_state = ai.state;
    view.event_ready = ai.ready;
    view.event_voc_mean = ai.voc_mean;
    view.event_score = ai.score;
    view.warmup_seconds = event.warmup_seconds;

    if (OLED_Node_Render(&view, oled_buffer, sizeof(oled_buffer)) == 0U) return;
    for (page = 0U; page < 8U; ++page)
    {
        oled_pos(0U, page);
        if (HAL_I2C_Mem_Write(&hi2c1, OLED_I2C_ADDR, 0x40U, I2C_MEMADD_SIZE_8BIT,
                              &oled_buffer[(uint16_t)page * OLED_NODE_WIDTH],
                              OLED_NODE_WIDTH, 0x100U) != HAL_OK)
        {
            break;
        }
    }
}

void oled_task(void)
{
    lcd_proc();
}
