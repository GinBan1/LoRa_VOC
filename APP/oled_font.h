/*
 * oled_font.h
 *
 *  Created on: Nov 17, 2024
 *      Author: 阿妈粽
 */

#ifndef INC_OLED_FONT_H_
#define INC_OLED_FONT_H_

#include "bsp.h"

extern const unsigned char F6x8[][6];
extern const unsigned char F8X16[];
extern const unsigned char Hzk[][32];
extern unsigned char BMP1[];
extern const unsigned char Cursor[][32];

/* 汉字索引 */
#define HZ_DU       0   /* 独 */
#define HZ_JIAO     1   /* 角 */
#define HZ_SHOU     2   /* 兽 */
#define HZ_WEN      3   /* 温 */
#define HZ_DU2      4   /* 度 */
#define HZ_SHI      5   /* 湿 */
#define HZ_YAN      6   /* 烟 */
#define HZ_WU       7   /* 雾 */
#define HZ_YE       8   /* 液 */
#define HZ_HUA      9   /* 化 */
#define HZ_QI      10   /* 气 */
#define HZ_TONG    11   /* 通 */
#define HZ_FENG    12   /* 风 */
#define HZ_SHUI    13   /* 水 */
#define HZ_BEng    14   /* 泵 */
#define HZ_JING    15   /* 经 */
#define HZ_WEI     16   /* 纬 */
#define HZ_QUE     17   /* 确 */
#define HZ_REN     18   /* 认 */
#define HZ_XUAN    19   /* 选 */
#define HZ_ZE      20   /* 择 */
#define HZ_YU      21   /* 阈 */
#define HZ_ZHI     22   /* 值 */
#define RIGHT1     0    /* - */
#define RIGHT2     1    /* > */

#endif /* INC_OLED_FONT_H_ */
