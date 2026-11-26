/* Chup anh giao dien 320x240 tren PC (khong can board): chay che do trinh dien qua bo giai ma HLP that
 * va luu vai khung hinh ra file BMP. Xem tools/pc_preview/build.sh. */
#include "lvgl.h"
#include "dashboard.h"
#include "demo.h"
#include "gofa.h"
#include <string.h>
#include "dashboard_hlp.h"
#include <stdio.h>
#include <stdlib.h>
#include "xadas.h"
void board_beep(int times) { printf("(tit %d)\n", times); }
static void xa(uint8_t st, uint8_t lv, uint8_t sp, uint16_t d, uint16_t mn)   /* 1 khung xADAS HUD Link */
{
    uint8_t b[10] = {1, 0, st, lv, 0, sp, (uint8_t)d, (uint8_t)(d >> 8), (uint8_t)mn, (uint8_t)(mn >> 8)};
    xadas_frame_t f;
    if (xadas_parse(b, sizeof b, &f)) xadas_ui_frame(&f);
}
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


/* Gia lap nguon VietMap Live (VML) tren giao dien 2.8": du lieu dang giai duoc tu goi 40FE
 * (mui ten + met + ten duong, "Toc do tiep theo" + met). Bien tiep theo dua vao canh bao code 8 (giam toc). */
static int frame_no;
static const char *outdir;
static void shot(const char *name)
{
    char p[300];
    if (name) { snprintf(p, sizeof p, "%s/vml_%s.bmp", outdir, name); save_bmp(p); }
    snprintf(p, sizeof p, "%s/frames/f%04d.bmp", outdir, frame_no++); save_bmp(p);
}
static void hold(int n) { for (int i = 0; i < n; i++) { run(100); shot(NULL); } }

int main(int argc, char **argv)
{
    outdir = argc > 1 ? argv[1] : ".";
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(UI_W, UI_H);
    static uint16_t buf[UI_W * UI_H];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);

    lv_screen_load(ui_splash_create("v3.0", UI_SRC_VML));
    run(800);
    shot("1_man_chao"); hold(15);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
    dashboard_create(scr);
    ui_set_mode(UI_MODE_NIGHT);
    ui_set_source(UI_SRC_VML);
    ui_set_time(22, 55, 4);
    ui_set_date(26, 11);
    ui_set_clock_synced(true);
    run(500);
    shot("2_cho_ket_noi"); hold(10);
    ui_set_phone(true);
    run(300);
    shot("3_da_ket_noi"); hold(10);

    ui_set_signal(true);
    ui_set_waze(true);
    ui_set_speed(-1);
    for (int m = 1250; m >= 0; m -= 25) {
        ui_set_nav(true, 3, m, 0, "Đồng Văn Cống", 0);
        if (m <= 900 && m >= 300) {
            ui_alert_t a = {8, m - 300, 60};
            ui_set_alerts(&a, 1);
        } else {
            ui_set_alerts(NULL, 0);
        }
        run(100);
        if (m == 1200) shot("4_re_1km2");
        else if (m == 450) shot("5_re_va_bien_tiep");
        else shot(NULL);
    }
    ui_set_nav(false, 0, -1, 0, "Đồng Văn Cống", 0);
    run(300);
    shot("6_het_chi_dan"); hold(10);
    ui_set_mode(UI_MODE_DAY);
    ui_set_nav(true, 3, 450, 0, "Đồng Văn Cống", 0);
    { ui_alert_t a = {8, 150, 60}; ui_set_alerts(&a, 1); }
    run(400);
    shot("7_ban_ngay"); hold(10);
    return 0;
}
