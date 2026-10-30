/* Cai dat luu trong NVS (con lai sau khi tat may). Nap firmware bang file gop (dia chi 0x0) se xoa cac gia tri nay. */
#pragma once
#include <stdbool.h>

int settings_get_mode(void);          /* UI_MODE_*: Tu dong / Ngay / Dem */
void settings_set_mode(int mode);
bool settings_get_demo(void);         /* che do trinh dien (du lieu gia) */
void settings_set_demo(bool on);
