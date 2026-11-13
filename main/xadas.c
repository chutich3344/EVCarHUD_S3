/* xADAS HUD Link - xem xadas.h. Ban 2.8" (CYD 320x240): giai ma giong P4; giao dien rieng ve tren lv_layer_top (xadas_ui_*). */
#include "xadas.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "dashboard.h"
#ifndef XADAS_NO_BLE
#include "esp_log.h"
static const char *TAG = "xadas";
#endif

bool xadas_parse(const uint8_t *b, size_t n, xadas_frame_t *f)
{
    if (!b || n < 10 || b[0] != 1) return false;
    f->seq = b[1];
    f->state = b[2];
    f->level = b[3];
    f->imperial = b[4] & 0x01;
    f->test = b[4] & 0x02;
    f->speed = b[5];
    f->dist = (uint16_t)(b[6] | b[7] << 8);
    f->min = (uint16_t)(b[8] | b[9] << 8);
    if (f->state > XA_ST_NOIMG) f->state = XA_ST_WAIT;            /* state la: hien nhu Cho */
    return true;
}

int xadas_obs_level(const xadas_frame_t *f)
{
    if (!f || f->state != XA_ST_MEASURE || f->dist == XA_NONE16) return 0;
    return f->level == XA_LV_CLOSE ? 2 : f->level == XA_LV_NEAR ? 1 : 0;   /* OBS_DANGER / OBS_CAUTION */
}

void xadas_chip_text(const xadas_frame_t *f, bool live, char *out, size_t n, int *color)
{
    /* mau: 0 xam, 1 xanh la, 2 vang, 3 do, 4 xanh duong, 5 vang canh bao he thong (= UI_XA_* trong dashboard.h) */
    if (!live) {
        snprintf(out, n, "Mất xADAS");
        *color = 0;
        return;
    }
    const char *u = f->imperial ? "ft" : "m";
    switch (f->state) {
    case XA_ST_MEASURE:
        if (f->dist == XA_NONE16) {
            snprintf(out, n, "— %s", u);
            *color = 0;
        } else {
            snprintf(out, n, "%s%u %s", f->test ? "THỬ " : "", (unsigned)f->dist, u);
            *color = f->level == XA_LV_SAFE ? 1 : f->level == XA_LV_NEAR ? 2 : f->level == XA_LV_CLOSE ? 3
                   : f->level == XA_LV_NOGPS ? 4 : 0;
            if (f->level > XA_LV_NOGPS) *color = 0;
        }
        return;
    case XA_ST_HOT:
        snprintf(out, n, "xADAS nóng");
        *color = 5;
        return;
    case XA_ST_NOIMG:
        snprintf(out, n, "xADAS mất hình");
        *color = 5;
        return;
    case XA_ST_SLOW:
        snprintf(out, n, "xADAS dừng");
        *color = 0;
        return;
    default:
        snprintf(out, n, "xADAS");
        *color = 0;
        return;
    }
}

#include "ui_fonts.h"
/* ===== Giao dien 2.8" (lv_layer_top, de len moi man) =====
 * Chip: o tron goc giua tren (x 112..208, y 4..30): so met mau theo muc xADAS (khoang trong o ca man chay va man dan duong).
 * Gan (level 2): dai vang duoi day man "XE TRƯỚC 57 m · tối thiểu 55 m", im lang.
 * Qua gan (level 3): dai do chop 2 Hz + vien do, tit 2 tieng (nhac lai 4 s mot lan; xe < 25 km/h: im).
 * Qua 3 s khong co khung: bo so, chip "Mất xADAS"; 1 phut sau an chip. */
#define CH_W 96
#define CH_H 26
#define BAR_H 46
static lv_obj_t *s_chip, *s_chip_l, *s_bar, *s_bar_t, *s_bar_d, *s_bar_u, *s_edge;
static lv_timer_t *s_tmr;
static xadas_frame_t s_f;
static bool s_on, s_live, s_dim;
static int s_lvl, s_tick;                         /* 0 khong, 1 gan, 2 qua gan */
static uint32_t s_rx, s_lost, s_beep;

static lv_obj_t *mk(lv_obj_t *p)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void vis(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void paint(void)
{
    if (!s_chip) return;
    bool night = ui_is_night();
    bool set = ui_settings_shown();
    char b[24];
    int c = 0;
    xadas_chip_text(&s_f, s_live, b, sizeof b, &c);
    vis(s_chip, s_on && !set);
    if (s_on && !set) {
        static const uint32_t cd[] = {0x8C96A0, 0x34D399, 0xFFD400, 0xFF4D4F, 0x35D6EF, 0xFFD400};
        static const uint32_t cl[] = {0x4B5563, 0x047857, 0xB45309, 0xBE123C, 0x0E7490, 0xB45309};
        uint32_t col = (night ? cd : cl)[c >= 0 && c <= 5 ? c : 0];
        if (strcmp(lv_label_get_text(s_chip_l), b)) lv_label_set_text(s_chip_l, b);
        int w = lv_text_get_width(b, strlen(b), &ui_font_txt16, 0) + 16;
        if (w < 56) w = 56;
        lv_obj_set_size(s_chip, w, CH_H);
        lv_obj_set_pos(s_chip, (UI_W - w) / 2, 4);
        lv_obj_set_style_bg_color(s_chip, lv_color_hex(night ? 0x000000 : 0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(s_chip, 200, 0);
        lv_obj_set_style_border_color(s_chip, lv_color_hex(col), 0);
        lv_obj_set_style_text_color(s_chip_l, lv_color_hex(col), 0);
        lv_obj_center(s_chip_l);
        lv_obj_move_foreground(s_chip);
    }
    bool bar = s_lvl > 0 && !set;
    vis(s_bar, bar);
    vis(s_edge, bar && s_lvl == 2);
    if (bar) {
        bool red = s_lvl == 2;
        uint32_t bg = red ? (s_dim ? 0x7A0A12 : 0xE11D2A) : (night ? 0xFFD400 : 0xF5B800);
        uint32_t fg = red ? 0xFFFFFF : 0x14112B;
        lv_obj_set_style_bg_color(s_bar, lv_color_hex(bg), 0);
        lv_obj_set_style_text_color(s_bar_t, lv_color_hex(fg), 0);
        lv_obj_set_style_text_color(s_bar_d, lv_color_hex(fg), 0);
        const char *u = s_f.imperial ? "ft" : "m";
        char t[64], d[16];
        if (s_f.min != XA_NONE16) snprintf(t, sizeof t, "%s\ntối thiểu %u %s", red ? "QUÁ GẦN XE TRƯỚC" : "GẦN XE TRƯỚC", s_f.min, u);
        else snprintf(t, sizeof t, "%s", red ? "QUÁ GẦN XE TRƯỚC" : "GẦN XE TRƯỚC");
        snprintf(d, sizeof d, "%u", s_f.dist);              /* num30 chi co chu so: don vi o nhan rieng */
        lv_label_set_text(s_bar_t, t);
        lv_label_set_text(s_bar_d, d);
        lv_label_set_text(s_bar_u, u);
        lv_obj_set_style_text_color(s_bar_u, lv_color_hex(fg), 0);
        lv_obj_align(s_bar_u, LV_ALIGN_RIGHT_MID, -12, 4);
        lv_obj_align_to(s_bar_d, s_bar_u, LV_ALIGN_OUT_LEFT_BOTTOM, -4, 5);
        lv_obj_set_style_border_opa(s_edge, s_dim ? 70 : LV_OPA_COVER, 0);
        lv_obj_move_foreground(s_bar);
        lv_obj_move_foreground(s_edge);
    }
}

static bool city(void)
{
    int sp = s_f.speed != XA_NONE8 ? (s_f.imperial ? s_f.speed * 16 / 10 : s_f.speed) : ui_get_speed();
    return sp < 25;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_on) return;
    if (s_live && lv_tick_elaps(s_rx) >= XA_STALE_MS) {          /* 3 s khong co khung: bo so cu */
        s_live = false;
        s_lost = lv_tick_get();
        s_lvl = 0;
        paint();
    } else if (!s_live && lv_tick_elaps(s_lost) >= 60000) {
        s_on = false;                                         /* tat xADAS lau roi: an chip */
        paint();
        return;
    }
    if (s_lvl == 2 && ++s_tick >= 5) {                           /* chop 2 Hz (nhip 50 ms) + nhac tieng */
        s_tick = 0;
        s_dim = !s_dim;
        if (!city() && lv_tick_elaps(s_beep) >= 4000) {
            s_beep = lv_tick_get();
            board_beep(2);
        }
        paint();
    }
}

void xadas_ui_init(void)
{
    if (s_chip) return;
    lv_obj_t *top = lv_layer_top();
    s_chip = mk(top);
    lv_obj_set_style_radius(s_chip, 8, 0);
    lv_obj_set_style_border_width(s_chip, 2, 0);
    s_chip_l = lv_label_create(s_chip);
    lv_obj_set_style_text_font(s_chip_l, &ui_font_txt16, 0);
    lv_label_set_text(s_chip_l, "");

    s_edge = mk(top);
    lv_obj_set_size(s_edge, UI_W, UI_H);
    lv_obj_set_style_border_width(s_edge, 5, 0);
    lv_obj_set_style_border_color(s_edge, lv_color_hex(0xFF3D4F), 0);

    s_bar = mk(top);
    lv_obj_set_size(s_bar, UI_W, BAR_H);
    lv_obj_set_pos(s_bar, 0, UI_H - BAR_H);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    s_bar_t = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_bar_t, &ui_font_txt14, 0);
    lv_obj_set_style_text_line_space(s_bar_t, -2, 0);
    lv_obj_align(s_bar_t, LV_ALIGN_LEFT_MID, 12, 0);
    s_bar_d = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_bar_d, &ui_font_num30, 0);
    s_bar_u = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_bar_u, &ui_font_txt16, 0);
    vis(s_chip, false);
    vis(s_bar, false);
    vis(s_edge, false);
    s_tmr = lv_timer_create(tick_cb, 50, NULL);
}

void xadas_ui_frame(const xadas_frame_t *f)
{
    if (!s_chip) return;
    int old = s_lvl;
    if (!f) {
        if (s_on && s_live && s_f.state != XA_ST_WAIT) {
            s_live = false;
            s_lost = lv_tick_get();
        } else {
            s_on = s_live = false;
        }
        s_lvl = 0;
    } else {
        s_f = *f;
        s_on = s_live = true;
        s_rx = lv_tick_get();
        s_lvl = xadas_obs_level(f);
    }
    if (s_lvl == 2 && old != 2 && !city()) {
        s_beep = lv_tick_get();
        board_beep(2);
    }
    if (s_lvl != 2) s_dim = false;
    paint();
}

#ifndef XADAS_NO_BLE
void xadas_on_write(const uint8_t *b, size_t n)
{
    xadas_frame_t f;
    if (!xadas_parse(b, n, &f)) {
        ESP_LOGW(TAG, "bo khung (%u byte, version %u)", (unsigned)n, n ? b[0] : 0);
        return;
    }
    static uint32_t cnt;
    if ((cnt++ % 20) == 0 || f.level == XA_LV_CLOSE)
        ESP_LOGI(TAG, "seq %u state %u level %u speed %u dist %u min %u", f.seq, f.state, f.level, f.speed, f.dist, f.min);
    board_ui_lock();
    xadas_ui_frame(&f);
    board_ui_unlock();
}

void xadas_link_down(void)
{
    board_ui_lock();
    xadas_ui_frame(NULL);
    board_ui_unlock();
}
#endif
