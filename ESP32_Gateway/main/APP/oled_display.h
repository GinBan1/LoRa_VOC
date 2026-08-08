#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oled_display_init(void);
void oled_display_task(void);

#ifdef __cplusplus
}
#endif

#endif /* OLED_DISPLAY_H */
