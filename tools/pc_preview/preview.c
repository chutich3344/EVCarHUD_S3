/* Chup anh giao dien 320x240 tren PC (khong can board): chay che do trinh dien qua bo giai ma HLP that
 * va luu vai khung hinh ra file BMP. Xem tools/pc_preview/build.sh. */
#include "lvgl.h"
#include "dashboard.h"
#include "demo.h"
#include "dashboard_hlp.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint16_t fb[UI_W * UI_H];
static uint32_t now_ms;
static uint32_t tick_cb(void) { return now_ms; }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * UI_W + x] = *src++;
    lv_display_flush_ready(d);
}

static void run(int ms)
{
    for (int t = 0; t < ms; t += 10) {
        now_ms += 10;
        lv_timer_handler();
    }
}

static void save_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int row = UI_W * 3, size = 54 + row * UI_H;
    uint8_t h[54] = {'B', 'M'};
    memcpy(h + 2, &size, 4);
    h[10] = 54; h[14] = 40;
    int w = UI_W, hh = -UI_H;                       /* chieu cao am: tu tren xuong */
    memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4);
    h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (int i = 0; i < UI_W * UI_H; i++) {
        uint16_t c = fb[i];
        uint8_t bgr[3] = {(uint8_t)((c & 0x1F) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c >> 11) << 3)};
        fwrite(bgr, 1, 3, f);
    }
    fclose(f);
    printf("da luu %s\n", path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char p[256];
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(UI_W, UI_H);
    static uint16_t buf[UI_W * UI_H];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);

    lv_screen_load(ui_splash_create("v1.1"));          /* man chao logo */
    run(800);
    snprintf(p, sizeof p, "%s/00_man_chao.bmp", dir); save_bmp(p);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
    dashboard_create(scr);
    hlp_dash_init(NULL, NULL);                     /* tren board: link_init() goi */
    run(400);

    /* Kich ban chay thu giong trang web: moi muc chup Ngay roi Dem */
    static const int MODES[2] = {UI_MODE_DAY, UI_MODE_NIGHT};
    static const char *const MN[2] = {"ngay", "dem"};
    for (int i = 0; i < 2; i++) {                   /* 1. chua ket noi dien thoai */
        ui_set_mode(MODES[i]);
        ui_set_time(11, 26, 0);
        ui_set_date(25, 10);
        run(300);
        snprintf(p, sizeof p, "%s/1_cho_chua_ket_noi_%s.bmp", dir, MN[i]); save_bmp(p);
    }
    ui_set_phone(true);
    for (int i = 0; i < 2; i++) {                   /* 2. da ket noi dien thoai */
        ui_set_mode(MODES[i]);
        run(300);
        snprintf(p, sizeof p, "%s/2_cho_da_ket_noi_%s.bmp", dir, MN[i]); save_bmp(p);
    }

    /* 4..7: trinh dien qua bo giai ma HLP that (vong 60 s) */
    static const struct { int at_ms, mode; const char *name; } shots[] = {
        {5000, UI_MODE_DAY, "4_chay_chua_waze_ngay"},
        {7000, UI_MODE_NIGHT, "4_chay_chua_waze_dem"},
        {12000, UI_MODE_DAY, "5_chay_co_waze_ngay"},
        {16500, UI_MODE_NIGHT, "5_chay_co_waze_dem"},
        {21000, UI_MODE_DAY, "7_chi_duong_ngay"},
        {31000, UI_MODE_NIGHT, "7_chi_duong_camera_dem"},
        {42000, UI_MODE_NIGHT, "7_chi_duong_vong_xoay_dem"},
        {46500, UI_MODE_NIGHT, "7_vuot_toc_do_dem"},
    };
    demo_start();
    int cur = 0;
    for (size_t i = 0; i < sizeof shots / sizeof shots[0]; i++) {
        ui_set_mode(shots[i].mode);
        demo_step((shots[i].at_ms - cur + 60000) % 60000);
        cur = shots[i].at_ms;
        run(300);
        snprintf(p, sizeof p, "%s/%s.bmp", dir, shots[i].name); save_bmp(p);
    }
    demo_stop();

    /* 6. chi duong nhung chua co du lieu Waze: dat thang vao giao dien */
    for (int i = 0; i < 2; i++) {
        ui_set_mode(MODES[i]);
        ui_set_signal(true);
        ui_set_phone(true);
        ui_set_waze(false);
        ui_set_speed(38);
        ui_set_speed_limit(0);
        ui_set_alerts(NULL, 0);
        ui_set_nav(true, 2, 350, 0, "Nguyễn Hữu Thọ", 0);
        ui_set_route("20:05", 6700, 18);
        run(300);
        snprintf(p, sizeof p, "%s/6_chi_duong_chua_waze_%s.bmp", dir, MN[i]); save_bmp(p);
    }
    return 0;
}
