/* Che do trinh dien: du lieu Waze gia di qua bo giai ma HLP that (xem demo.c). Goi trong khoa LVGL. */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void demo_start(void);
void demo_stop(void);
bool demo_running(void);
void demo_step(int ms);      /* nhay toi ms sau (chi de chup anh xem truoc tren PC) */

#ifdef __cplusplus
}
#endif
