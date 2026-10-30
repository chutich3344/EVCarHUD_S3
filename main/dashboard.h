/* EVCarHUD S3 - giao dien LVGL 9 cho man hinh 2.8" ngang 320x240.
 * Giao dien chi nhan du lieu qua cac ham ui_set_*, khong goi truc tiep phan cung hay giao thuc
 * (dashboard_hlp.c giai ma Waze Mod HLP/1 roi goi vao day). Moi ham phai goi trong khoa LVGL (board_ui_lock). */
#ifndef DASHBOARD_H
#define DASHBOARD_H

#if defined(__has_include)
#  if __has_include("lvgl.h")
#    include "lvgl.h"
#  else
#    include "lvgl/lvgl.h"
#  endif
#else
#  include "lvgl.h"
#endif
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_W 320
#define UI_H 240

/* Canh bao sap toi (ma theo HLP `alr`, 0..75). dist/value = -1 neu khong co. */
typedef struct {
    int code;
    int dist;      /* met */
    int value;     /* km/h, chi co voi SPEED_DROP (8) */
} ui_alert_t;

enum { UI_CHIP_NONE = 0, UI_CHIP_CAMERA = 1, UI_CHIP_NOPASS = 2 };
enum { UI_GPS_GOOD = 0, UI_GPS_WEAK, UI_GPS_LOST };
enum { UI_MODE_AUTO = 0, UI_MODE_DAY = 1, UI_MODE_NIGHT = 2 };
enum { UI_LEVEL_OK = 0, UI_LEVEL_NEAR = 1, UI_LEVEL_OVER = 2 };   /* toc do so voi gioi han */

void dashboard_create(lv_obj_t *screen);

/* --- toc do / dong ho --- */
void ui_set_speed(int kmh);
void ui_set_speed_limit(int kmh);                 /* 0 = chua biet: an bien, khong canh bao */
void ui_set_time(int hour, int minute, int wday); /* wday: 0 = Chu nhat ... 6 = Thu bay, -1 = chua biet */
int ui_get_speed(void);
bool ui_get_clock(int *h, int *m);                /* false: chua co gio tu dien thoai */

/* --- du lieu dan duong (Waze HLP) --- */
void ui_set_nav(bool active, int turn, int dist_m, int exit_n, const char *street, int turn2);
void ui_set_route(const char *eta, int remaining_m, int remaining_min);   /* eta rong va remaining <= 0: hien dong ho */
void ui_set_alerts(const ui_alert_t *alerts, int n);   /* gan -> xa; n = 0: khong co canh bao */
void ui_set_chip(int kind, const char *text);
void ui_set_signal(bool ok);                           /* false: hien "MAT TIN HIEU" */
void ui_set_gps(int level);
void ui_set_hint(const char *text);                    /* dong chu nho duoi banner mat tin hieu */

/* --- Ngay / Dem ---
 * Tu dong: theo cam bien anh sang neu board co (ui_set_ambient), khong thi theo gio (6:00-18:00 la ngay).
 * Cham man hinh hoac bam BOOT: Tu dong -> Ngay -> Dem. */
void ui_set_mode(int mode);
int ui_get_mode(void);
void ui_cycle_mode(void);
void ui_set_ambient(int dark);                         /* 1 toi, 0 sang, -1 khong co cam bien */
void ui_set_mode_hook(void (*cb)(int mode));           /* nguoi dung doi che do: de luu NVS */
void ui_set_night_hook(void (*cb)(bool night));        /* giao dien vua doi sang Ngay/Dem: board chinh den nen */
void ui_set_level_hook(void (*cb)(int level));         /* UI_LEVEL_*: board bat den LED / loa */
void ui_toast(const char *text);                       /* thong bao nho 2 giay o giua man hinh */

#ifdef __cplusplus
}
#endif
#endif
