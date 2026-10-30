/* Phan cung ESP32-2432S028 ("Cheap Yellow Display" 2.8"): man hinh SPI 320x240 (ILI9341 hoac ST7789),
 * cam ung XPT2046, den nen, LED RGB, loa, quang tro. Chan GPIO trong menuconfig > EVCarHUD. */
#pragma once
#include <stdbool.h>
#include "lvgl.h"

lv_display_t *board_display_init(void);      /* LVGL + man hinh ngang 320x240 + cam ung; den nen van tat */
bool board_ui_lock(void);                    /* moi thao tac LVGL ngoai task LVGL phai nam trong lock/unlock */
void board_ui_unlock(void);
const char *board_lcd_name(void);            /* chip man hinh dang dung: "ILI9341" / "ST7789" */
void board_set_backlight(int percent);       /* 0..100 */
void board_set_led(bool r, bool g, bool b);  /* LED RGB tren board */
void board_beep(int times);                  /* tit ngan qua loa (khong chan: xep cho task rieng) */
int board_light_raw(void);                   /* quang tro 0..4095; -1 neu tat trong menuconfig */
