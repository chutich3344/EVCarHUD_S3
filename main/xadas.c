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
 * 14/11 nguoi dung: it chu, khong che noi dung dang hien. Chi 1 o nho dat vao cho trong o goc tren (x 140..212, giua
 * dong ho / "Chi co GPS" va mui ten re), chi co so met:
 *   an toan / mat GPS: chu mau (xanh la / xanh duong) tren nen toi; khong co so: "--" xam
 *   gan (level 2): o to vang; qua gan (level 3): o to do chop 2 Hz + vien do man hinh, tit 2 tieng (xe < 25 km/h: im)
 * Qua 3 s khong co khung: bo so, hien "--"; 1 phut sau an o. */
#define CH_CX 176
#define CH_MAXW 72
#define CH_H 26
static lv_obj_t *s_chip, *s_chip_l, *s_edge;
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
    bool show = s_on && !ui_settings_shown();
    vis(s_chip, show);
    vis(s_edge, show && s_lvl == 2);
    if (!show) return;
    char b[16];
    int c = 0;
    if (s_live && s_f.state == XA_ST_MEASURE && s_f.dist != XA_NONE16) {
        snprintf(b, sizeof b, "%u %s", (unsigned)s_f.dist, s_f.imperial ? "ft" : "m");
        c = s_f.level == XA_LV_SAFE ? 1 : s_f.level == XA_LV_NEAR ? 2 : s_f.level == XA_LV_CLOSE ? 3 : s_f.level == XA_LV_NOGPS ? 4 : 0;
    } else {
        snprintf(b, sizeof b, "--");                          /* cho / khong co xe / dung do / mat tin hieu */
        c = s_live && (s_f.state == XA_ST_HOT || s_f.state == XA_ST_NOIMG) ? 5 : 0;
    }
    static const uint32_t cd[] = {0x8C96A0, 0x34D399, 0xFFD400, 0xFF4D4F, 0x35D6EF, 0xFFD400};
    static const uint32_t cl[] = {0x4B5563, 0x047857, 0xB45309, 0xBE123C, 0x0E7490, 0xB45309};
    uint32_t col = (night ? cd : cl)[c], bg = night ? 0x000000 : 0xFFFFFF, fg = col;
    lv_opa_t bo = 170;
    if (s_lvl == 1) { bg = 0xFFD400; fg = 0x14112B; bo = LV_OPA_COVER; col = bg; }
    if (s_lvl == 2) { bg = s_dim ? 0x7A0A12 : 0xE11D2A; fg = 0xFFFFFF; bo = LV_OPA_COVER; col = bg; }
    if (strcmp(lv_label_get_text(s_chip_l), b)) lv_label_set_text(s_chip_l, b);
    int w = lv_text_get_width(b, strlen(b), &ui_font_txt16, 0) + 14;
    if (w < 44) w = 44;
    if (w > CH_MAXW) w = CH_MAXW;
    lv_obj_set_size(s_chip, w, CH_H);
    lv_obj_set_pos(s_chip, CH_CX - w / 2, 4);
    lv_obj_set_style_bg_color(s_chip, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(s_chip, bo, 0);
    lv_obj_set_style_border_color(s_chip, lv_color_hex(col), 0);
    lv_obj_set_style_text_color(s_chip_l, lv_color_hex(fg), 0);
    lv_obj_center(s_chip_l);
    lv_obj_move_foreground(s_chip);
    if (s_lvl == 2) {
        lv_obj_set_style_border_opa(s_edge, s_dim ? 70 : LV_OPA_COVER, 0);
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

    vis(s_chip, false);
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
