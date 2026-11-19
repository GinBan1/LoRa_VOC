#ifndef APP_CLK_H
#define APP_CLK_H

/* 84 MHz 设置只调整启动时的 AHB 分频。 */
#ifndef APP_HCLK_MHZ
#define APP_HCLK_MHZ 168
#endif
#if APP_HCLK_MHZ != 168 && APP_HCLK_MHZ != 84
#error APP_HCLK_MHZ_must_be_168_or_84
#endif

#endif /* APP_CLK_H */
