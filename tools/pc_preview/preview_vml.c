/* Chup anh giao dien 320x240 tren PC (khong can board): chay che do trinh dien qua bo giai ma HLP that
 * va luu vai khung hinh ra file BMP. Xem tools/pc_preview/build.sh. */
#include "lvgl.h"
#include <math.h>
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


/* Anh gia lap thay JPEG cua VML: mui ten re phai (A8), bien 60 tron do so 60, camera (RGB565) */
static uint8_t arr_px[60*60];
static uint16_t sign_px[60*60*3/2], cam_px[60*60];
static lv_image_dsc_t arr_d, sign_d, cam_d;
static void mkdsc(lv_image_dsc_t *d, void *px, lv_color_format_t cf, int bpp)
{
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC; d->header.cf = cf; d->header.w = 60; d->header.h = 60;
    d->header.stride = 60 * (cf == LV_COLOR_FORMAT_RGB565A8 ? 2 : bpp); d->data_size = 3600 * bpp; d->data = px;
}
static void mk_images(void)
{
    for (int y = 0; y < 60; y++) for (int x = 0; x < 60; x++) {
        int a = 0;
        if (x >= 18 && x <= 30 && y >= 26 && y <= 56) a = 255;                 /* than doc */
        if (y >= 20 && y <= 32 && x >= 18 && x <= 40) a = 255;                 /* than ngang */
        if (x >= 38 && x <= 54 && abs(y - 26) <= 54 - x) a = 255;             /* dau mui ten */
        arr_px[y*60+x] = a;
        int dx = x - 30, dy = y - 30, r2 = dx*dx + dy*dy;
        sign_px[y*60+x] = r2 <= 29*29 ? (r2 >= 23*23 ? 0xF800 : 0xFFFF) : 0x0000;
        uint16_t c = 0x0000;
        if (x >= 10 && x <= 44 && y >= 20 && y <= 42) c = 0x2104;               /* than camera */
        if (x >= 44 && x <= 54 && y >= 24 && y <= 38) c = 0x2104;
        if ((x-26)*(x-26)+(y-31)*(y-31) <= 64) c = 0x5DFF;
        if (y >= 46 && y <= 50 && x >= 10 && x <= 50) c = 0xFE60;
        cam_px[y*60+x] = c;
    }
    /* chu "60" tren bien: 2 o so vuong don gian */
    for (int y = 21; y <= 39; y++) for (int x = 17; x <= 43; x++) {
        int lx = x < 30 ? x - 17 : x - 31, inl = lx >= 0 && lx <= 11;
        if (!inl) continue;
        bool edge = lx <= 2 || lx >= 9 || y <= 23 || y >= 37 || (x < 30 && y >= 29 && y <= 31);
        if (x < 30 && lx >= 9 && y < 29) edge = false;
        if (edge) sign_px[y*60+x] = 0x0000;
    }
    mkdsc(&arr_d, arr_px, LV_COLOR_FORMAT_A8, 1);
    uint8_t *al = (uint8_t *)(sign_px + 3600);
    for (int y = 0; y < 60; y++) for (int x = 0; x < 60; x++) {
        float dx = x + 0.5f - 30, dy = y + 0.5f - 30, dd = 30 - sqrtf(dx*dx + dy*dy);
        al[y*60+x] = dd >= 1 ? 255 : dd <= 0 ? 0 : (uint8_t)(dd * 255);
    }
    mkdsc(&sign_d, sign_px, LV_COLOR_FORMAT_RGB565A8, 3);
    mkdsc(&cam_d, cam_px, LV_COLOR_FORMAT_RGB565, 2);
}

int main(int argc, char **argv)
{
    mk_images();
    outdir = argc > 1 ? argv[1] : ".";
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(UI_W, UI_H);
    static uint16_t buf[UI_W * UI_H];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);

    lv_screen_load(ui_splash_create("2.9", UI_SRC_VML));
    run(800);
    shot("1_man_chao"); hold(15);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
    dashboard_create(scr);
    ui_set_mode(UI_MODE_NIGHT);
    ui_set_source(UI_SRC_VML);
    ui_set_time(22, 55, 4);
    ui_set_date(26, 11);
    ui_set_clock_synced(false);
    run(500);
    shot("2_cho_ket_noi"); hold(10);
    ui_set_phone(true);
    run(300);
    shot("3_da_ket_noi"); hold(10);

    ui_set_signal(true);
    ui_set_waze(true);
    ui_set_speed(-1);
    ui_set_keep_on(true);
    ui_set_speed_limit(50);
    ui_set_nav_icon(&arr_d);
    for (int m = 1250; m >= 0; m -= 25) {
        ui_set_nav(true, 3, m, 0, "Mai Chí Thọ", 0);
        if (m <= 900 && m >= 360) {
            ui_alert_t a = {8, m - 300, -1};
            ui_set_alert_icon(&sign_d);
            ui_set_alerts(&a, 1);
        } else if (m < 360 && m >= 200) {
            ui_alert_t a = {3, m - 120, 0};
            ui_set_alert_icon(&cam_d);
            ui_set_alerts(&a, 1);
        } else {
            ui_set_alerts(NULL, 0);
            ui_set_alert_icon(NULL);
        }
        if (m == 300) ui_set_speed_limit(40);
        run(100);
        if (m == 1200) shot("4_re_1km2");
        else if (m == 500) shot("5_re_va_bien_tiep");
        else if (m == 250) shot("6_camera_bien_hien_tai");
        else shot(NULL);
    }
    ui_set_nav(false, 0, -1, 0, "Mai Chí Thọ", 0);
    ui_set_nav_icon(NULL);
    run(300);
    shot("7_het_chi_dan"); hold(10);
    ui_set_mode(UI_MODE_DAY);
    ui_set_nav_icon(&arr_d);
    ui_set_nav(true, 3, 450, 0, "Mai Chí Thọ", 0);
    { ui_alert_t a = {8, 150, -1}; ui_set_alert_icon(&sign_d); ui_set_alerts(&a, 1); }
    run(400);
    shot("8_ban_ngay"); hold(10);
    return 0;
}
