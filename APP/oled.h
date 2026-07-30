/*
 * oled.h
 *
 *  Created on: Nov 17, 2024
 *      Author: 阿妈粽
 */

#ifndef INC_OLED_H_
#define INC_OLED_H_

#include "bsp.h"
#include "oled_font.h"
#include "stdarg.h"

#define OLED_I2C_ADDR    (0x3C << 1)
#define MAX_LINE  9      // 行数（根据你的屏幕行数调整）
#define MAX_COL   40     // 每行最大字符数（根据屏幕宽度和字体宽度调整）
#define FONT_H    16     // 字体高度（单位：像素）
#define FONT_W    8      // 字体宽度（单位：像素）
#define WHITE     0xFFFF // 默认前景色
#define BLACK     0x0000 // 默认背景色
#define CURSOR_COL 1     // 光标列
#define MENU_PAGE 3      // 菜单页数


// 趋势状态枚举
typedef enum {
    TREND_RAPID_WORSEN = 0,   // 快速恶化
    TREND_SLOW_RISE,          // 缓慢上升
    TREND_STABLE,             // 保持稳定
    TREND_GRADUAL_IMPROVE,    // 逐渐改善
    TREND_RAPID_RECOVER       // 快速恢复
} trend_state_t;

// 定义环境数据统一结构体
typedef struct {
    float temperature;  // 温度
    float humidity;     // 湿度
    float pressure;     // 气压
    int32_t voc_index;  // VOC指数
    uint16_t sgp40_raw;   // SGP40原始值(SRAW_VOC)
    uint8_t bme680_valid; // BME680数据是否有效 (1:有效 0:无效)
    uint8_t sgp40_valid;  // SGP40数据是否有效  (1:有效 0:无效)
} Environment_Data_t;

extern I2C_HandleTypeDef  hi2c1;

void OLED_WR_CMD(uint8_t cmd);
void OLED_WR_DATA(uint8_t data);
void OLED_Init(void);
void OLED_Clear(void);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Set_Pos(uint8_t x, uint8_t y);
void OLED_On(void);
void OLED_ShowNum(uint8_t x,uint8_t y,unsigned int num,uint8_t len,uint8_t size2,uint8_t Color_Turn);
void OLED_Showdecimal(uint8_t x,uint8_t y,float num,uint8_t z_len,uint8_t f_len,uint8_t size2, uint8_t Color_Turn);
void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t chr,uint8_t Char_Size,uint8_t Color_Turn);
void OLED_ShowString(uint8_t x,uint8_t y,char*chr,uint8_t Char_Size,uint8_t Color_Turn);
void OLED_ShowCHinese(uint8_t x,uint8_t y,uint8_t no,uint8_t Color_Turn);
void OLED_DrawBMP(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t *  BMP,uint8_t Color_Turn);
void OLED_HorizontalShift(uint8_t direction);
void OLED_Some_HorizontalShift(uint8_t direction,uint8_t start,uint8_t end);
void OLED_VerticalAndHorizontalShift(uint8_t direction);
void OLED_DisplayMode(uint8_t mode);
void OLED_IntensityControl(uint8_t intensity);
void my_oledprintf(uint8_t Line,char * format , ...);
void oled_cnprintf(uint8_t line, const uint8_t *hz, uint8_t hz_cnt, char *format, ...);
void OLED_ShowCentigrade(uint8_t x, uint8_t y, uint8_t Char_Size, uint8_t Color_Turn);
void OLED_ShowChineseString(uint8_t x, uint8_t y, uint8_t *chinese, uint8_t len, uint8_t Color_Turn);
void OLED_ShowCursor(uint8_t x, uint8_t y, uint8_t no, uint8_t Color_Turn);

void oled_task();
void trend_analyzer_update(float smooth_delta); // 趋势分析
void lcd_proc(void);            // 屏幕逻辑更新（只更新缓存）
extern uint8_t menu;
extern volatile float gas;
extern volatile float MQ5_gas;
extern double latitude_decimal;// 纬度
extern double longitude_decimal;// 经度
extern _Bool GPS_status;
//void oled_sprintf(uint16_t line, uint16_t col, const char *format, ...);        // 原始直接写屏函数
//void oled_sprintf_buffer(uint16_t line, uint16_t col, const char *format, ...); // 缓存更新函数

//void lcd_refresh_task(void);   // 异步刷新一行到实际屏幕

#endif /* INC_OLED_H_ */
