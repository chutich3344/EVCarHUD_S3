/* EVCarHUD - giao dien the bo goc cho man hinh 2.8" ngang 320x240 (LVGL 9), do theo anh mau:
 *
 *  +----------------------------------------------------------+
 *  | [mui ten]  600m Di thang                  | Tiep theo  |  |  the dan duong
 *  |            Nguyen Trai                    | -> Re trai |  |
 *  +--------------------+-------------------------------------+
 *  |       TOC DO       |    ( 50 )            [bien]         |  the toc do | the bien bao
 *  |         32         |    Gioi han        Khu dan cu       |
 *  |        km/h        |                                     |
 *  +--------------------+-------------------------------------+
 *  | [icon] Canh sat    | [dong ho] Den 20:05                 |  the canh bao | the gio den
 *  |        Con 900m    |           6,7 km · 18 phut          |
 *  +----------------------------------------------------------+
 *
 * Le 10 px, khe giua cac the 8 px, cot trai 132 px, cot phai 160 px, bo goc 10 px, vien 1 px.
 * The bien bao: bien duong gan nhat (khu dan cu, cam re, cam vuot...). The canh bao: su kien gan nhat
 * (canh sat, camera, tai nan, ket xe...). Mau so toc do: xanh la, vang khi cach gioi han <= 5 km/h,
 * do khi vuot (kem vien do nhap nhay quanh man hinh). Cham man hinh hoac bam BOOT: Tu dong -> Ngay -> Dem.
 * Giu tay tren man hinh 1 giay: xoay giao dien 180 do. */
#include "dashboard.h"
#include "dashboard_signs.h"
#include "ui_fonts.h"
#include <stdio.h>
#include <string.h>

#define M        10                         /* le man hinh */
#define G        8                          /* khe giua the */
#define R_CARD   10
#define ROW1_Y   M
#define ROW1_H   58
#define ROW2_Y   (ROW1_Y + ROW1_H + G)
#define ROW2_H   88
#define ROW3_Y   (ROW2_Y + ROW2_H + G)
#define ROW3_H   (UI_H - ROW3_Y - M)
#define LW       132                        /* cot trai: toc do, canh bao */
#define RX       (M + LW + G)
#define RW       (UI_W - RX - M)            /* cot phai: bien bao, gio den */
#define TILE     34                         /* o icon trong the canh bao / gio den */
#define TXT_X    (8 + TILE + 8)
#define NEAR_KMH 5

typedef struct {
    lv_color_t bg, card, border, fg, mut;
    lv_color_t accent, accent_bg, accent_bd, orange, orange_bg, ok, warn, over;
    lv_color_t alert_bg, alert_bd, alert_tile, sign_txt, ring;
    lv_opa_t shadow;
} theme_t;

#define C(h) LV_COLOR_MAKE(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF)
static const theme_t TH_DAY = {
    .bg = C(0xF3F3EF), .card = C(0xFFFFFF), .border = C(0xE4E4DF), .fg = C(0x111827), .mut = C(0x6B7280),
    .accent = C(0x0E7490), .accent_bg = C(0xDDF3F9), .accent_bd = C(0x9BD8E8),
    .orange = C(0xB45309), .orange_bg = C(0xFCEBD7), .ok = C(0x16A34A), .warn = C(0xD97706), .over = C(0xDC2626),
    .alert_bg = C(0xFEF0F1), .alert_bd = C(0xF4AFBA), .alert_tile = C(0xFDD3DA), .sign_txt = C(0x3B82F6),
    .ring = C(0xEF4444), .shadow = 18,
};
static const theme_t TH_NIGHT = {
    .bg = C(0x05080F), .card = C(0x111827), .border = C(0x1F2937), .fg = C(0xF9FAFB), .mut = C(0x9CA3AF),
    .accent = C(0x22D3EE), .accent_bg = C(0x0E3440), .accent_bd = C(0x155E75),
    .orange = C(0xFBBF24), .orange_bg = C(0x3A2A10), .ok = C(0x22C55E), .warn = C(0xFACC15), .over = C(0xEF4444),
    .alert_bg = C(0x2A1014), .alert_bd = C(0x7F1D1D), .alert_tile = C(0x4A161C), .sign_txt = C(0x60A5FA),
    .ring = C(0xEF4444), .shadow = 0,
};

static const char *const MODE_NAME[3] = {"Tự động", "Ban ngày", "Ban đêm"};
static const theme_t *th = &TH_DAY;

/* --- doi tuong --- */
static lv_obj_t *scr;
static lv_obj_t *card_nav, *nav_tile, *img_turn, *lbl_exit, *lbl_dist, *lbl_act, *lbl_street;
static lv_obj_t *card_next, *lbl_next_cap, *img_next, *lbl_next;
static lv_obj_t *card_spd, *lbl_speed, *lbl_unit;
static lv_obj_t *card_sign, *sign_ring, *lbl_limit, *lbl_limit_cap, *img_sign, *lbl_sign, *lbl_nolimit;
static lv_obj_t *card_al, *al_tile, *img_al, *lbl_al, *lbl_al_dist;
static lv_obj_t *card_eta, *eta_tile, *clk_face, *clk_h_hand, *clk_m_hand, *lbl_eta, *lbl_eta2;
static lv_obj_t *nosig, *lbl_nosig, *lbl_hint;
static lv_obj_t *frame, *toast, *lbl_toast;
static lv_timer_t *blink_tmr, *toast_tmr;

/* --- trang thai --- */
static int speed = -2, limit = -1, level = -1;
static bool nav_on, has_alert, has_route;
static int clk_h = -1, clk_m = -1;
static int mode = UI_MODE_AUTO, ambient = -1;
static bool night;
static void (*mode_cb)(int);
static void (*night_cb)(bool);
static void (*level_cb)(int);
static void (*rotate_cb)(void);

/* ======================= tien ich ======================= */
static lv_obj_t *mk_box(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *mk_card(lv_obj_t *p, int x, int y, int w, int h, int r)
{
    lv_obj_t *o = mk_box(p, x, y, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, int x, int y, const char *txt)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *mk_label_w(lv_obj_t *p, const lv_font_t *f, int x, int y, int w, lv_text_align_t al)
{
    lv_obj_t *l = mk_label(p, f, x, y, "");
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, al, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

static lv_obj_t *mk_image(lv_obj_t *p, int x, int y, int scale)
{
    lv_obj_t *im = lv_image_create(p);
    lv_obj_set_pos(im, x, y);
    lv_image_set_pivot(im, 0, 0);
    lv_image_set_scale(im, scale);
    return im;
}

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* khoang cach tu dinh label toi chan chu */
static int baseline(const lv_font_t *f) { return f->line_height - f->base_line; }

static int text_w(const char *s, const lv_font_t *f) { return lv_text_get_width(s, strlen(s), f, 0); }

/* 350 -> "350m", 1240 -> "1,2 km", 12400 -> "12 km" (nhu mau: met viet lien, km co cach) */
static void fmt_dist(char *out, size_t cap, int m)
{
    if (m < 0) out[0] = 0;
    else if (m < 1000) snprintf(out, cap, "%dm", m < 50 ? m : (m + 5) / 10 * 10);
    else if (m < 10000) snprintf(out, cap, "%d,%d km", m / 1000, (m % 1000 + 50) / 100 % 10);
    else snprintf(out, cap, "%d km", (m + 500) / 1000);
}

static const char *turn_name(int trn)
{
    switch (trn) {
    case 1: return "Đi thẳng";
    case 2: return "Rẽ trái";
    case 3: return "Rẽ phải";
    case 4: case 13: case 15: return "Giữ trái";
    case 5: case 14: case 16: return "Giữ phải";
    case 6: return "Rẽ gắt trái";
    case 7: return "Rẽ gắt phải";
    case 8: case 9: return "Quay đầu";
    case 10: case 11: case 12: case 19: case 20: return "Vòng xuyến";
    case 17: return "Đến nơi";
    default: return "";
    }
}

static const char *turn_short(int trn)                  /* the "Tiep theo" hep */
{
    switch (trn) {
    case 6: return "Rẽ trái";
    case 7: return "Rẽ phải";
    case 10: case 11: case 12: case 19: case 20: return "Vòng xoay";
    default: return turn_name(trn);
    }
}

/* Ten canh bao theo ma HLP `alr` (cung nhom voi sign_for_alert) */
static const char *alert_name(int code)
{
    switch (code) {
    case 1: return "Cảnh sát";
    case 2: return "Bắn tốc độ";
    case 3: return "Camera đèn";
    case 5: return "Tai nạn";
    case 6: return "Kẹt xe";
    case 7: case 38: return "Cấm đi vào";
    case 8: return "Giảm tốc";
    case 9: return "Cấm vượt";
    case 10: return "Hết cấm vượt";
    case 11: return "Đường sắt";
    case 12: return "Trạm thu phí";
    case 14: return "Công trình";
    case 15: return "Ổ gà";
    case 16: case 50: case 51: case 52: case 53: case 54: case 55: return "Thời tiết xấu";
    case 19: case 20: case 21: case 35: case 36: return "Thông tin";
    case 22: case 25: return "Hết hạn chế";
    case 23: return "Khu dân cư";
    case 24: return "Hết khu dân cư";
    case 26: return "Cấm ô tô";
    case 27: return "Cấm xe máy";
    case 28: case 39: case 66: case 67: case 68: case 69: case 72: return "Cấm rẽ trái";
    case 29: case 65: case 70: case 71: case 73: return "Cấm rẽ phải";
    case 30: case 74: return "Cấm quay đầu";
    case 31: return "Cấm đi thẳng";
    case 32: return "Đi thẳng";
    case 33: return "Chỉ rẽ phải";
    case 34: return "Chỉ rẽ trái";
    case 37: return "Một chiều";
    case 40: case 41: case 42: case 43: case 44: case 45: case 46: return "Camera";
    case 56: return "Gờ giảm tốc";
    case 57: return "Người đi bộ";
    case 59: return "Đường cong";
    case 61: case 75: return "Đèn tín hiệu";
    default: return "Nguy hiểm";
    }
}

/* Bien bao tren duong (hien o the bien bao) khac su kien (hien o the canh bao) */
static bool is_road_sign(int code)
{
    switch (code) {
    case 7: case 9: case 10: case 11: case 12: case 37: case 38: case 56: case 57: case 59: case 61:
        return true;
    default:
        return (code >= 22 && code <= 34) || (code >= 65 && code <= 75);
    }
}

/* ======================= mau sac ======================= */
static void paint_speed(void)
{
    lv_color_t c = level == UI_LEVEL_OVER ? th->over : level == UI_LEVEL_NEAR ? th->warn : speed >= 0 ? th->ok : th->mut;
    lv_obj_set_style_text_color(lbl_speed, c, 0);
    lv_obj_set_style_border_color(card_spd, level == UI_LEVEL_OVER ? th->over : th->border, 0);
    lv_obj_set_style_border_width(card_spd, level == UI_LEVEL_OVER ? 2 : 1, 0);
}

static void paint_alert_card(void)
{
    lv_obj_set_style_bg_color(card_al, has_alert ? th->alert_bg : th->card, 0);
    lv_obj_set_style_border_color(card_al, has_alert ? th->alert_bd : th->border, 0);
    lv_obj_set_style_text_color(lbl_al_dist, has_alert ? th->orange : th->mut, 0);
}

static void card_colors(lv_obj_t *o)
{
    lv_obj_set_style_bg_color(o, th->card, 0);
    lv_obj_set_style_border_color(o, th->border, 0);
    lv_obj_set_style_shadow_width(o, th->shadow ? 8 : 0, 0);    /* bong mo nhe nhu mau (chi ban ngay) */
    lv_obj_set_style_shadow_offset_y(o, 2, 0);
    lv_obj_set_style_shadow_color(o, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(o, th->shadow, 0);
}

static void apply_theme(void)
{
    lv_obj_set_style_bg_color(scr, th->bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t *cards[] = {card_nav, card_next, card_spd, card_sign, card_al, card_eta};
    for (size_t i = 0; i < sizeof cards / sizeof cards[0]; i++) card_colors(cards[i]);
    lv_obj_set_style_shadow_width(card_next, 0, 0);

    lv_obj_t *fg[] = {lbl_dist, lbl_street, lbl_al, lbl_eta, lbl_unit, lbl_limit_cap};
    for (size_t i = 0; i < sizeof fg / sizeof fg[0]; i++) lv_obj_set_style_text_color(fg[i], th->fg, 0);
    lv_obj_t *mut[] = {lbl_next_cap, lbl_nolimit};
    for (size_t i = 0; i < sizeof mut / sizeof mut[0]; i++) lv_obj_set_style_text_color(mut[i], th->mut, 0);
    lv_obj_set_style_text_color(lbl_act, th->accent, 0);
    lv_obj_set_style_text_color(lbl_exit, th->accent, 0);
    lv_obj_set_style_text_color(lbl_eta2, th->accent, 0);
    lv_obj_set_style_text_color(lbl_next, th->orange, 0);
    lv_obj_set_style_text_color(lbl_sign, th->sign_txt, 0);

    lv_obj_set_style_bg_color(nav_tile, th->accent_bg, 0);
    lv_obj_set_style_border_color(nav_tile, th->accent_bd, 0);
    lv_obj_set_style_image_recolor(img_turn, th->accent, 0);
    lv_obj_set_style_image_recolor(img_next, th->orange, 0);
    lv_obj_set_style_bg_color(eta_tile, th->orange_bg, 0);
    lv_obj_set_style_bg_color(clk_face, th->orange, 0);
    lv_obj_set_style_bg_color(clk_h_hand, th->orange_bg, 0);
    lv_obj_set_style_bg_color(clk_m_hand, th->orange_bg, 0);
    lv_obj_set_style_bg_color(al_tile, th->alert_tile, 0);
    lv_obj_set_style_border_color(sign_ring, th->ring, 0);

    lv_obj_set_style_bg_color(nosig, th->card, 0);
    lv_obj_set_style_border_color(nosig, th->over, 0);
    lv_obj_set_style_text_color(lbl_nosig, th->over, 0);
    lv_obj_set_style_text_color(lbl_hint, th->mut, 0);
    lv_obj_set_style_bg_color(toast, th->fg, 0);
    lv_obj_set_style_text_color(lbl_toast, th->bg, 0);
    lv_obj_set_style_border_color(frame, th->over, 0);
    paint_speed();
    paint_alert_card();
}

static void update_night(void)
{
    bool n;
    if (mode == UI_MODE_DAY) n = false;
    else if (mode == UI_MODE_NIGHT) n = true;
    else if (ambient >= 0) n = ambient != 0;
    else n = clk_h >= 0 && (clk_h < 6 || clk_h >= 18);            /* chua co gio: de ban ngay */
    const theme_t *want = n ? &TH_NIGHT : &TH_DAY;
    if (want == th) return;
    night = n;
    th = want;
    apply_theme();
    if (night_cb) night_cb(night);
}

/* ======================= canh bao toc do ======================= */
static void blink_cb(lv_timer_t *t)
{
    (void)t;
    show(frame, lv_obj_has_flag(frame, LV_OBJ_FLAG_HIDDEN));
}

static void update_level(void)
{
    int lv = UI_LEVEL_OK;
    if (limit > 0 && speed >= 0) {
        if (speed > limit) lv = UI_LEVEL_OVER;
        else if (speed >= limit - NEAR_KMH) lv = UI_LEVEL_NEAR;
    }
    if (lv != level) {
        level = lv;
        if (lv == UI_LEVEL_OVER) {
            show(frame, true);
            lv_obj_move_foreground(frame);
            lv_timer_resume(blink_tmr);
        } else {
            lv_timer_pause(blink_tmr);
            show(frame, false);
        }
        if (level_cb) level_cb(lv);
    }
    paint_speed();
}

/* ======================= cham man hinh ======================= */
static void toast_hide_cb(lv_timer_t *t)
{
    (void)t;
    show(toast, false);
    lv_timer_pause(toast_tmr);
}

void ui_toast(const char *text)
{
    lv_label_set_text(lbl_toast, text);
    lv_obj_center(lbl_toast);
    show(toast, true);
    lv_obj_move_foreground(toast);
    lv_timer_reset(toast_tmr);
    lv_timer_resume(toast_tmr);
}

static void on_click(lv_event_t *e)
{
    (void)e;
    ui_cycle_mode();
}

static void on_long_press(lv_event_t *e)
{
    (void)e;
    if (!rotate_cb) return;
    rotate_cb();
    ui_toast("Đã xoay màn hình");
}

/* ======================= dung giao dien ======================= */
static void build_nav(void)
{
    card_nav = mk_card(scr, M, ROW1_Y, UI_W - 2 * M, ROW1_H, R_CARD);
    nav_tile = mk_card(card_nav, 8, 8, 40, 40, 9);
    img_turn = mk_image(nav_tile, 5, 5, 112);                      /* icon 64 px -> 28 px */
    lv_obj_set_style_image_recolor_opa(img_turn, LV_OPA_COVER, 0);
    lbl_exit = mk_label(nav_tile, &ui_font_cap12, 28, 0, "");      /* so loi ra vong xuyen */
    lbl_dist = mk_label(card_nav, &ui_font_dist24, 58, 3, "");
    lbl_act = mk_label(card_nav, &ui_font_txt14, 58, 13, "");
    lbl_street = mk_label_w(card_nav, &ui_font_txt16, 58, 31, 140, LV_TEXT_ALIGN_LEFT);

    card_next = mk_card(card_nav, 0, 7, 90, 42, 8);                /* x, rong: theo chu (ui_set_nav) */
    lbl_next_cap = mk_label(card_next, &ui_font_sm12, 0, 4, "Tiếp theo");
    img_next = mk_image(card_next, 0, 21, 72);                     /* 64 px -> 18 px */
    lv_obj_set_style_image_recolor_opa(img_next, LV_OPA_COVER, 0);
    lbl_next = mk_label(card_next, &ui_font_txt14, 0, 20, "");
}

static void build_speed(void)
{
    card_spd = mk_card(scr, M, ROW2_Y, LW, ROW2_H, R_CARD);
    int w = LW - 2;
    lbl_speed = mk_label_w(card_spd, &ui_font_speed62, 0, 9, w, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(lbl_speed, LV_LABEL_LONG_CLIP);
    lbl_unit = mk_label_w(card_spd, &ui_font_txt14, 0, 61, w, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(lbl_unit, "km/h");
}

static void build_sign(void)
{
    card_sign = mk_card(scr, RX, ROW2_Y, RW, ROW2_H, R_CARD);
    int half = (RW - 2) / 2;
    sign_ring = mk_box(card_sign, (half - 52) / 2 + 4, 8, 52, 52);
    lv_obj_set_style_radius(sign_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sign_ring, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sign_ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sign_ring, 5, 0);
    lv_obj_set_style_border_opa(sign_ring, LV_OPA_COVER, 0);
    lbl_limit = mk_label(sign_ring, &ui_font_sign24, 0, 0, "");
    lv_obj_set_style_text_color(lbl_limit, lv_color_hex(0x111827), 0);
    lbl_limit_cap = mk_label_w(card_sign, &ui_font_sm12, 4, 66, half, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(lbl_limit_cap, "Giới hạn");
    lbl_nolimit = mk_label_w(card_sign, &ui_font_txt14, 4, 26, half, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(lbl_nolimit, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lbl_nolimit, "Chưa có\ngiới hạn");

    img_sign = mk_image(card_sign, half + (half - 48) / 2 - 4, 10, 256);   /* bien 48 px */
    lbl_sign = mk_label_w(card_sign, &ui_font_sm12, half - 4, 66, half + 8, LV_TEXT_ALIGN_CENTER);
}

static lv_obj_t *mk_tile(lv_obj_t *card)
{
    lv_obj_t *t = mk_box(card, 8, (ROW3_H - 2 - TILE) / 2, TILE, TILE);
    lv_obj_set_style_radius(t, 8, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    return t;
}

static void build_alert(void)
{
    card_al = mk_card(scr, M, ROW3_Y, LW, ROW3_H, R_CARD);
    al_tile = mk_tile(card_al);
    img_al = mk_image(al_tile, 2, 2, 160);                        /* 48 px -> 30 px */
    lbl_al = mk_label_w(card_al, &ui_font_txt16, TXT_X, 8, LW - TXT_X - 6, LV_TEXT_ALIGN_LEFT);
    lbl_al_dist = mk_label_w(card_al, &ui_font_mono14, TXT_X, 28, LW - TXT_X - 6, LV_TEXT_ALIGN_LEFT);
}

static void build_eta(void)
{
    card_eta = mk_card(scr, RX, ROW3_Y, RW, ROW3_H, R_CARD);
    eta_tile = mk_tile(card_eta);
    clk_face = mk_box(eta_tile, 6, 6, 22, 22);                    /* dong ho: mat tron dac + 2 kim sang */
    lv_obj_set_style_radius(clk_face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(clk_face, LV_OPA_COVER, 0);
    clk_m_hand = mk_box(eta_tile, 16, 10, 2, 8);
    clk_h_hand = mk_box(eta_tile, 16, 16, 6, 2);
    lv_obj_set_style_bg_opa(clk_m_hand, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(clk_h_hand, LV_OPA_COVER, 0);
    lbl_eta = mk_label_w(card_eta, &ui_font_txt16, TXT_X, 8, RW - TXT_X - 6, LV_TEXT_ALIGN_LEFT);
    lbl_eta2 = mk_label_w(card_eta, &ui_font_mono14, TXT_X, 28, RW - TXT_X - 6, LV_TEXT_ALIGN_LEFT);
}

void dashboard_create(lv_obj_t *screen)
{
    scr = screen;
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_click, LV_EVENT_SHORT_CLICKED, NULL);   /* cham: Ngay/Dem; giu 1 giay: xoay */
    lv_obj_add_event_cb(scr, on_long_press, LV_EVENT_LONG_PRESSED, NULL);

    build_nav();
    build_speed();
    build_sign();
    build_alert();
    build_eta();

    /* vien do khi vuot toc do */
    frame = mk_box(scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_border_width(frame, 4, 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
    show(frame, false);
    blink_tmr = lv_timer_create(blink_cb, 400, NULL);
    lv_timer_pause(blink_tmr);

    /* mat tin hieu: che hang giua */
    nosig = mk_card(scr, M, ROW2_Y, UI_W - 2 * M, ROW2_H, R_CARD);
    lv_obj_set_style_border_width(nosig, 2, 0);
    lbl_nosig = mk_label_w(nosig, &ui_font_txt16, 0, 14, UI_W - 2 * M - 4, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(lbl_nosig, "MẤT TÍN HIỆU");
    lbl_hint = mk_label_w(nosig, &ui_font_txt14, 10, 40, UI_W - 2 * M - 24, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(lbl_hint, LV_LABEL_LONG_WRAP);
    show(nosig, false);

    /* thong bao nho */
    toast = mk_box(scr, 60, 102, UI_W - 120, 36);
    lv_obj_set_style_radius(toast, 18, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lbl_toast = mk_label(toast, &ui_font_txt14, 0, 0, "");
    show(toast, false);
    toast_tmr = lv_timer_create(toast_hide_cb, 2000, NULL);
    lv_timer_pause(toast_tmr);

    th = NULL;
    update_night();
    ui_set_speed(-1);
    ui_set_speed_limit(0);
    ui_set_nav(false, 0, -1, 0, "", 0);
    ui_set_route("", 0, 0);
    ui_set_alerts(NULL, 0);
}

/* ======================= toc do / dong ho ======================= */
void ui_set_speed(int kmh)
{
    if (kmh == speed) return;
    speed = kmh;
    char b[12];
    if (kmh < 0) snprintf(b, sizeof b, "--");
    else snprintf(b, sizeof b, "%d", kmh % 1000);
    lv_label_set_text(lbl_speed, b);
    update_level();
}

int ui_get_speed(void) { return speed; }

void ui_set_speed_limit(int kmh)
{
    if (kmh == limit) return;
    limit = kmh;
    show(sign_ring, kmh > 0);
    show(lbl_limit_cap, kmh > 0);
    show(lbl_nolimit, kmh <= 0);
    if (kmh > 0) {
        char b[12];
        snprintf(b, sizeof b, "%d", kmh % 1000);
        lv_obj_set_style_text_font(lbl_limit, kmh >= 100 ? &ui_font_txt16 : &ui_font_sign24, 0);   /* 3 chu so: nho lai cho vua vong */
        lv_label_set_text(lbl_limit, b);
        lv_obj_center(lbl_limit);
    }
    update_level();
}

static void refresh_eta_clock(void)
{
    char b[24];
    if (clk_h >= 0) snprintf(b, sizeof b, "Bây giờ %02d:%02d", clk_h % 100, clk_m % 100);
    else snprintf(b, sizeof b, "Bây giờ --:--");
    lv_label_set_text(lbl_eta, b);
    lv_label_set_text(lbl_eta2, "Chưa có tuyến");
}

void ui_set_time(int hour, int minute, int wday)
{
    (void)wday;
    clk_h = hour;
    clk_m = minute;
    if (!has_route) refresh_eta_clock();
    update_night();
}

bool ui_get_clock(int *h, int *m)
{
    if (clk_h < 0) return false;
    *h = clk_h;
    *m = clk_m;
    return true;
}

/* ======================= dan duong ======================= */
static int layout_next(int turn2)                    /* the "Tiep theo" om sat chu, can phai; tra ve mep trai */
{
    const lv_image_dsc_t *arr2 = turn2 ? arrow_for_turn(turn2) : NULL;
    int inner = UI_W - 2 * M - 2;
    show(card_next, arr2 != NULL);
    if (!arr2) return inner - 8;
    const char *t = turn_short(turn2);
    lv_image_set_src(img_next, arr2);
    lv_label_set_text(lbl_next, t);
    int tw = text_w(t, &ui_font_txt14), cw = text_w("Tiếp theo", &ui_font_sm12);
    int w = 10 + 18 + 2 + tw + 10;
    if (w < cw + 20) w = cw + 20;
    lv_obj_set_width(card_next, w);
    lv_obj_set_x(card_next, inner - 8 - w);
    lv_obj_set_x(lbl_next_cap, w - 2 - 10 - cw);
    lv_obj_set_x(lbl_next, w - 2 - 10 - tw);
    lv_obj_set_x(img_next, w - 2 - 10 - tw - 2 - 18);
    return inner - 8 - w - 8;
}

void ui_set_nav(bool active, int turn, int dist_m, int exit_n, const char *street, int turn2)
{
    const lv_image_dsc_t *arr = active ? arrow_for_turn(turn) : NULL;
    nav_on = active && arr;
    if (arr) {
        lv_image_set_src(img_turn, arr);
        lv_image_set_scale(img_turn, 112);
        lv_obj_set_pos(img_turn, 5, 5);
        lv_obj_set_style_image_recolor_opa(img_turn, LV_OPA_COVER, 0);
    } else {                                                       /* khong dan duong: mui ten vi tri */
        lv_image_set_src(img_turn, &icon_nav_marker);
        lv_image_set_scale(img_turn, 160);
        lv_obj_set_pos(img_turn, 4, 4);
        lv_obj_set_style_image_recolor_opa(img_turn, LV_OPA_TRANSP, 0);
    }

    char b[24];
    fmt_dist(b, sizeof b, nav_on ? dist_m : -1);
    lv_label_set_text(lbl_dist, b);
    show(lbl_dist, nav_on && b[0]);

    if (nav_on && exit_n > 0) {
        char e[8];
        snprintf(e, sizeof e, "%d", exit_n % 100);
        lv_label_set_text(lbl_exit, e);
    }
    show(lbl_exit, nav_on && exit_n > 0);

    int right = layout_next(nav_on ? turn2 : 0);

    /* kieu re canh khoang cach (cung chan chu); khong dan duong: "Dang di tren" o cho khoang cach */
    const char *act = nav_on ? turn_name(turn) : "Đang đi trên";
    lv_label_set_text(lbl_act, act);
    int ax = 58, ay = 9;
    if (nav_on && b[0]) {
        ax = 58 + text_w(b, &ui_font_dist24) + 7;
        ay = lv_obj_get_y(lbl_dist) + baseline(&ui_font_dist24) - baseline(&ui_font_txt14);   /* cung chan chu voi so met */
    }
    lv_obj_set_pos(lbl_act, ax, ay);
    show(lbl_act, act[0] && ax + text_w(act, &ui_font_txt14) <= right);   /* khong du cho: bo chu, mui ten da noi kieu re */

    lv_obj_set_width(lbl_street, right - 58);
    lv_label_set_text(lbl_street, street ? street : "");
}

void ui_set_route(const char *eta, int remaining_m, int remaining_min)
{
    char d[16], b[48];
    has_route = (eta && eta[0]) || remaining_m > 0;
    if (!has_route) {
        refresh_eta_clock();
        return;
    }
    if (eta && eta[0]) snprintf(b, sizeof b, "Đến %s", eta);
    else snprintf(b, sizeof b, "Còn lại");
    lv_label_set_text(lbl_eta, b);
    fmt_dist(d, sizeof d, remaining_m > 0 ? remaining_m : -1);
    int room = RW - TXT_X - 6;
    if (d[0] && remaining_min > 0) {
        if (remaining_min >= 60) snprintf(b, sizeof b, "%s · %dg%02d", d, remaining_min / 60, remaining_min % 60);
        else {
            snprintf(b, sizeof b, "%s · %d phút", d, remaining_min);
            if (text_w(b, &ui_font_mono14) > room) snprintf(b, sizeof b, "%s · %d'", d, remaining_min);
        }
    } else if (d[0]) {
        snprintf(b, sizeof b, "Còn %s", d);
    } else if (remaining_min > 0) {
        snprintf(b, sizeof b, "Còn %d phút", remaining_min);
    } else {
        b[0] = 0;
    }
    lv_label_set_text(lbl_eta2, b);
}

void ui_set_alerts(const ui_alert_t *alerts, int n)
{
    const ui_alert_t *ev = NULL, *sign = NULL;
    for (int i = 0; alerts && i < n; i++) {
        if (is_road_sign(alerts[i].code)) {
            if (!sign) sign = &alerts[i];
        } else if (!ev) {
            ev = &alerts[i];
        }
    }

    has_alert = ev != NULL;
    show(al_tile, has_alert);
    int x = has_alert ? TXT_X : 12;
    lv_obj_set_x(lbl_al, x);
    lv_obj_set_x(lbl_al_dist, x);
    lv_obj_set_width(lbl_al, LW - x - 6);
    lv_obj_set_width(lbl_al_dist, LW - x - 6);
    if (ev) {
        char b[24], d[16];
        lv_image_set_src(img_al, sign_for_alert(ev->code));
        const char *name = alert_name(ev->code);
        lv_obj_set_style_text_font(lbl_al, text_w(name, &ui_font_txt16) > LW - x - 6 ? &ui_font_txt14 : &ui_font_txt16, 0);
        lv_label_set_text(lbl_al, name);
        fmt_dist(d, sizeof d, ev->dist);
        if (ev->code == 8 && ev->value > 0) snprintf(b, sizeof b, "Còn %d km/h", ev->value);
        else if (d[0]) snprintf(b, sizeof b, "Còn %s", d);
        else b[0] = 0;
        lv_label_set_text(lbl_al_dist, b);
        lv_obj_set_style_text_font(lbl_al_dist, &ui_font_mono14, 0);
    } else {
        lv_obj_set_style_text_font(lbl_al, &ui_font_txt16, 0);
        lv_label_set_text(lbl_al, "Đường thoáng");
        lv_obj_set_style_text_font(lbl_al_dist, &ui_font_txt14, 0);
        lv_label_set_text(lbl_al_dist, "Không cảnh báo");
    }
    paint_alert_card();

    show(img_sign, sign != NULL);
    show(lbl_sign, sign != NULL);
    if (sign) {
        lv_image_set_src(img_sign, sign_for_alert(sign->code));
        lv_label_set_text(lbl_sign, alert_name(sign->code));
    }
}

void ui_set_chip(int kind, const char *text)
{
    (void)kind; (void)text;                                       /* camera / cam vuot da nam trong the canh bao */
}

void ui_set_signal(bool ok)
{
    show(nosig, !ok);
    if (!ok) {
        lv_obj_move_foreground(nosig);
        ui_set_speed(-1);
        ui_set_speed_limit(0);
        ui_set_nav(false, 0, -1, 0, "", 0);
        ui_set_route("", 0, 0);
        ui_set_alerts(NULL, 0);
    }
}

void ui_set_gps(int lvl) { (void)lvl; }

void ui_set_hint(const char *text)
{
    lv_label_set_text(lbl_hint, text ? text : "");
}

/* ======================= Ngay / Dem ======================= */
void ui_set_mode(int m)
{
    if (m < UI_MODE_AUTO || m > UI_MODE_NIGHT) m = UI_MODE_AUTO;
    mode = m;
    update_night();
}

int ui_get_mode(void) { return mode; }

void ui_cycle_mode(void)
{
    ui_set_mode((mode + 1) % 3);
    char b[40];
    snprintf(b, sizeof b, "Chế độ: %s", MODE_NAME[mode]);
    ui_toast(b);
    if (mode_cb) mode_cb(mode);
}

void ui_set_ambient(int dark)
{
    if (dark == ambient) return;
    ambient = dark;
    update_night();
}

void ui_set_mode_hook(void (*cb)(int)) { mode_cb = cb; }
void ui_set_night_hook(void (*cb)(bool)) { night_cb = cb; if (cb) cb(night); }
void ui_set_level_hook(void (*cb)(int)) { level_cb = cb; }
void ui_set_rotate_hook(void (*cb)(void)) { rotate_cb = cb; }
