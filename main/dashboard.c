/* EVCarHUD - giao dien the bo goc cho man hinh 2.8" ngang 320x240 (LVGL 9), giong ban mo phong tren evcarhud.com.
 * Ba man, tu chon theo du lieu tu dien thoai:
 *
 *  Man cho (chua co du lieu, hoac dung yen > 30 s ma khong dan duong):
 *  +----------------------------------------------------------+
 *  | EVCarHUD                 Cho ket noi Waze qua dien thoai |  / "Da ket noi dien thoai"
 *  |      +-----------+   +-----------+                       |
 *  |      |    11     |   |    26     |                       |  dong ho lat (gio tu dien thoai)
 *  |      +-----------+   +-----------+                       |
 *  | Chu Nhat, 25 Thang 10                      GPS san sang  |
 *  +----------------------------------------------------------+
 *
 *  Chay thuong (dang chay, chua dat lo trinh):      Dan duong:
 *  +--------------+---------------------------+     +------------------------------------+
 *  |              |   ( 50 )      [bien]      |     | [mui ten] 600m Di thang | Tiep theo |
 *  |      35      |                           |     +-----------+------------------------+
 *  |     km/h     |  Gioi han    Khu dan cu   |     |    32     |  ( 50 )      [bien]    |
 *  +--------------+--+------------------------+     +-----------+------------------------+
 *  | Canh sat        | Giam toc                |     | Canh sat  | Den 20:05              |
 *  +-----------------+-------------------------+     +-----------+------------------------+
 *
 * Le 10 px, khe 8 px, bo goc 10 px, vien 1 px. Chua co du lieu Waze (chi co toc do GPS): bien gioi han "-",
 * the canh bao "Duong thoang". Mau so toc do: xanh la, vang khi cach gioi han <= 5 km/h, do khi vuot (kem vien
 * do nhap nhay). Cham man hinh hoac bam BOOT: Tu dong -> Ngay -> Dem. Giu tay 1 giay: xoay 180 do. */
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
#define TOP_H    (ROW1_H + G + ROW2_H)      /* chay thuong: the toc do + bien bao cao het 2 hang tren */
#define BW       ((UI_W - 2 * M - G) / 2)   /* chay thuong: 2 the duoi chia doi */
#define NEAR_KMH 5
#define MOVE_KMH 3                          /* >= 3 km/h: dang chay */
#define STOP_MS  30000                      /* dung yen 30 s (khong dan duong): ve man cho */

typedef struct {
    lv_color_t bg, card, border, fg, mut;
    lv_color_t accent, accent_bg, accent_bd, orange, orange_bg, ok, warn, over;
    lv_color_t alert_bg, alert_bd, alert_tile, sign_txt, ring;
    lv_color_t flip_top, flip_top2, flip_bot, flip_line;
    lv_opa_t shadow;
} theme_t;

#define C(h) LV_COLOR_MAKE(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF)
static const theme_t TH_DAY = {
    .bg = C(0xF3F3EF), .card = C(0xFFFFFF), .border = C(0xE4E4DF), .fg = C(0x111827), .mut = C(0x6B7280),
    .accent = C(0x0E7490), .accent_bg = C(0xDDF3F9), .accent_bd = C(0x9BD8E8),
    .orange = C(0xB45309), .orange_bg = C(0xFCEBD7), .ok = C(0x16A34A), .warn = C(0xD97706), .over = C(0xDC2626),
    .alert_bg = C(0xFEF0F1), .alert_bd = C(0xF4AFBA), .alert_tile = C(0xFDD3DA), .sign_txt = C(0x3B82F6),
    .ring = C(0xEF4444), .flip_top = C(0xFFFFFF), .flip_top2 = C(0xFFFFFF), .flip_bot = C(0xF1F0EC), .flip_line = C(0xDCDCDA),
    .shadow = 18,
};
static const theme_t TH_NIGHT = {
    .bg = C(0x05080F), .card = C(0x111827), .border = C(0x1F2937), .fg = C(0xF9FAFB), .mut = C(0x9CA3AF),
    .accent = C(0x22D3EE), .accent_bg = C(0x0E3440), .accent_bd = C(0x155E75),
    .orange = C(0xFBBF24), .orange_bg = C(0x3A2A10), .ok = C(0x22C55E), .warn = C(0xFACC15), .over = C(0xEF4444),
    .alert_bg = C(0x2A1014), .alert_bd = C(0x7F1D1D), .alert_tile = C(0x4A161C), .sign_txt = C(0x60A5FA),
    .ring = C(0xEF4444), .flip_top = C(0x1E293B), .flip_top2 = C(0x0F172A), .flip_bot = C(0x0C111D), .flip_line = C(0x020408),
    .shadow = 0,
};

static const char *const MODE_NAME[3] = {"Tự động", "Ban ngày", "Ban đêm"};
static const char *const WDAY_NAME[7] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
enum { SCR_STANDBY = 0, SCR_CRUISE, SCR_NAV };
static const theme_t *th = &TH_DAY;

/* --- doi tuong --- */
static lv_obj_t *scr;
static lv_obj_t *card_nav, *nav_tile, *img_turn, *lbl_exit, *lbl_dist, *lbl_act, *lbl_street;
static lv_obj_t *card_next, *lbl_next_cap, *img_next, *lbl_next;
static lv_obj_t *card_spd, *lbl_speed, *lbl_unit;
static lv_obj_t *card_sign, *sign_ring, *lbl_limit, *lbl_limit_cap, *img_sign, *lbl_sign;
static lv_obj_t *card_al, *al_tile, *img_al, *lbl_al, *lbl_al_dist;
static lv_obj_t *card_eta, *eta_tile, *clk_face, *clk_h_hand, *clk_m_hand, *img_tip, *lbl_eta, *lbl_eta2;
static lv_obj_t *stby, *lbl_sb_brand, *lbl_sb_stat, *flip[2], *flip_bot[2], *flip_line[2], *lbl_clk[2];
static lv_obj_t *sb_bar, *lbl_date, *lbl_gps;
static lv_obj_t *dg_h, *dg_c, *dg_m;        /* dong ho dien tu 24 gio (chua ket noi dien thoai) */
static lv_timer_t *colon_tmr;
static lv_obj_t *frame, *toast, *lbl_toast;
static lv_timer_t *blink_tmr, *toast_tmr;

/* --- trang thai --- */
static int speed = -2, limit = -1, level = -1;
static bool nav_on, has_alert, has_route;
static int clk_h = -1, clk_m = -1, day = -1, month = -1, wday = -1;
static int screen = -1;
static bool sig_ok, phone, waze = true, moving, clk_synced;
static uint32_t stop_tick;                  /* 0: khong dung yen */
static ui_alert_t al_list[4];               /* canh bao vua nhan: ve lai khi doi man (the doi kich thuoc) */
static int al_n, ev_code;                   /* ev_code: ma su kien gan nhat, 0 = khong co */
static char eta_txt[8], street_now[64];
static int eta_rm, eta_rmin;
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

static bool is_camera(int code)
{
    return code == 2 || code == 3 || (code >= 40 && code <= 46);
}

static void render_alerts(void);
static void render_eta(void);
static void render_standby(void);

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
    lv_obj_t *cards[] = {card_nav, card_next, card_spd, card_sign, card_al, card_eta, sb_bar};
    for (size_t i = 0; i < sizeof cards / sizeof cards[0]; i++) card_colors(cards[i]);
    lv_obj_set_style_shadow_width(card_next, 0, 0);

    lv_obj_t *fg[] = {lbl_dist, lbl_street, lbl_al, lbl_eta, lbl_unit, lbl_limit_cap, lbl_sb_brand, lbl_clk[0], lbl_date, dg_h};
    for (size_t i = 0; i < sizeof fg / sizeof fg[0]; i++) lv_obj_set_style_text_color(fg[i], th->fg, 0);
    lv_obj_set_style_text_color(lbl_next_cap, th->mut, 0);
    lv_obj_t *acc[] = {lbl_act, lbl_exit, lbl_eta2, lbl_clk[1], lbl_gps, dg_c, dg_m};
    for (size_t i = 0; i < sizeof acc / sizeof acc[0]; i++) lv_obj_set_style_text_color(acc[i], th->accent, 0);
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

    lv_obj_set_style_bg_color(stby, th->bg, 0);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(flip[i], th->flip_top, 0);
        lv_obj_set_style_bg_grad_color(flip[i], th->flip_top2, 0);
        lv_obj_set_style_border_color(flip[i], th->border, 0);
        lv_obj_set_style_shadow_opa(flip[i], th->shadow, 0);
        lv_obj_set_style_bg_color(flip_bot[i], th->flip_bot, 0);
        lv_obj_set_style_bg_color(flip_line[i], th->flip_line, 0);
    }
    lv_obj_set_style_bg_color(toast, th->fg, 0);
    lv_obj_set_style_text_color(lbl_toast, th->bg, 0);
    lv_obj_set_style_border_color(frame, th->over, 0);
    paint_speed();
    paint_alert_card();
    render_standby();
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

/* ======================= bo tri theo man ======================= */
/* The toc do: so 62 px khi dan duong, 88 px khi chay thuong; so va "km/h" can giua the. */
static void layout_speed(void)
{
    bool big = screen == SCR_CRUISE;
    int w = lv_obj_get_width(card_spd) - 2;
    lv_obj_set_width(lbl_speed, w);
    lv_obj_set_width(lbl_unit, w);
    lv_obj_set_style_text_font(lbl_speed, big ? &ui_font_speed88 : &ui_font_speed62, 0);
    lv_obj_set_y(lbl_speed, big ? 22 : 9);
    lv_obj_set_y(lbl_unit, big ? 112 : 61);
}

/* The bien bao: bien gioi han (luon co, "-" khi chua biet) + bien duong gan nhat neu co; mot bien thi can giua. */
static void layout_signs(void)
{
    bool big = screen == SCR_CRUISE;
    int w = lv_obj_get_width(card_sign) - 2, h = lv_obj_get_height(card_sign) - 2;
    int R = big ? 62 : 52, S = big ? 56 : 48;
    bool two = !lv_obj_has_flag(img_sign, LV_OBJ_FLAG_HIDDEN);
    int half = w / 2;
    int cx1 = two ? half / 2 + 4 : w / 2, cx2 = half + half / 2 - 4;
    int y = (h - (R + 6 + 14)) / 2;
    lv_obj_set_size(sign_ring, R, R);
    lv_obj_set_style_border_width(sign_ring, big ? 6 : 5, 0);
    lv_obj_set_pos(sign_ring, cx1 - R / 2, y);
    const lv_font_t *f = limit >= 100 ? (big ? &ui_font_sign24 : &ui_font_txt16) : (big ? &ui_font_sign32 : &ui_font_sign24);
    lv_obj_set_style_text_font(lbl_limit, f, 0);                   /* 3 chu so: nho lai cho vua vong */
    lv_obj_center(lbl_limit);
    lv_obj_set_width(lbl_limit_cap, half);
    lv_obj_set_pos(lbl_limit_cap, cx1 - half / 2, y + R + 6);
    lv_image_set_scale(img_sign, (uint32_t)(S * 256 / 48));       /* bien goc 48 px */
    lv_obj_set_pos(img_sign, cx2 - S / 2, y + (R - S) / 2);
    lv_obj_set_width(lbl_sign, half + 8);
    lv_obj_set_pos(lbl_sign, cx2 - half / 2 - 4, y + R + 6);
}

static void place(lv_obj_t *o, int x, int y, int w, int h)
{
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
}

static void set_screen(int s)
{
    if (s == screen) return;
    screen = s;
    show(stby, s == SCR_STANDBY);
    show(card_nav, s == SCR_NAV);
    if (s == SCR_CRUISE) {
        place(card_spd, M, M, LW, TOP_H);
        place(card_sign, RX, M, RW, TOP_H);
        place(card_al, M, ROW3_Y, BW, ROW3_H);
        place(card_eta, M + BW + G, ROW3_Y, UI_W - 2 * M - BW - G, ROW3_H);
    } else {
        place(card_spd, M, ROW2_Y, LW, ROW2_H);
        place(card_sign, RX, ROW2_Y, RW, ROW2_H);
        place(card_al, M, ROW3_Y, LW, ROW3_H);
        place(card_eta, RX, ROW3_Y, RW, ROW3_H);
    }
    lv_obj_update_layout(scr);                                     /* kich thuoc moi truoc khi can chu ben trong */
    int tx = s == SCR_CRUISE ? 6 : 8;                              /* chay thuong: the duoi hep hon, sat le */
    lv_obj_set_x(al_tile, tx);
    lv_obj_set_x(eta_tile, tx);
    layout_speed();
    layout_signs();
    render_alerts();
    render_eta();
    if (s == SCR_STANDBY) {
        lv_obj_move_foreground(stby);
        render_standby();
    }
    lv_obj_move_foreground(frame);
    lv_obj_move_foreground(toast);
}

/* Chua co du lieu -> man cho; dang dan duong -> dan duong; dang chay -> chay thuong; dung yen lau -> man cho. */
static void update_screen(void)
{
    set_screen(!sig_ok || (!nav_on && !moving) ? SCR_STANDBY : nav_on ? SCR_NAV : SCR_CRUISE);
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
        if (screen == SCR_CRUISE) render_eta();                    /* loi nhac "Giam toc do" */
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

void ui_rotate(void)
{
    if (!rotate_cb) return;
    rotate_cb();
    ui_toast("Đã xoay màn hình");
}

static void on_long_press(lv_event_t *e)
{
    (void)e;
    ui_rotate();
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
    lbl_speed = mk_label_w(card_spd, &ui_font_speed62, 0, 9, LW - 2, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(lbl_speed, LV_LABEL_LONG_CLIP);
    lbl_unit = mk_label_w(card_spd, &ui_font_txt14, 0, 61, LW - 2, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(lbl_unit, "km/h");
}

static void build_sign(void)
{
    card_sign = mk_card(scr, RX, ROW2_Y, RW, ROW2_H, R_CARD);
    sign_ring = mk_box(card_sign, 0, 0, 52, 52);
    lv_obj_set_style_radius(sign_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sign_ring, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sign_ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(sign_ring, LV_OPA_COVER, 0);
    lbl_limit = mk_label(sign_ring, &ui_font_sign24, 0, 0, "-");
    lv_obj_set_style_text_color(lbl_limit, lv_color_hex(0x111827), 0);
    lbl_limit_cap = mk_label_w(card_sign, &ui_font_sm12, 0, 0, 70, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(lbl_limit_cap, "Giới hạn");
    img_sign = mk_image(card_sign, 0, 0, 256);
    lbl_sign = mk_label_w(card_sign, &ui_font_sm12, 0, 0, 70, LV_TEXT_ALIGN_CENTER);
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
    img_tip = mk_image(eta_tile, 2, 2, 160);                      /* loi nhac camera: bien 48 px -> 30 px */
    show(img_tip, false);
    lbl_eta = mk_label_w(card_eta, &ui_font_txt16, TXT_X, 8, RW - TXT_X - 6, LV_TEXT_ALIGN_LEFT);
    lbl_eta2 = mk_label_w(card_eta, &ui_font_mono14, TXT_X, 28, RW - TXT_X - 6, LV_TEXT_ALIGN_LEFT);
}

/* Man cho: dong ho lat 2 the (gio | phut), dong trang thai ket noi o tren, ngay va GPS o duoi. */
#define FLIP_W 124
#define FLIP_H 104
#define FLIP_Y 44
static void build_standby(void)
{
    stby = mk_box(scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_bg_opa(stby, LV_OPA_COVER, 0);
    lbl_sb_brand = mk_label(stby, &ui_font_txt14, M + 2, 14, "EVCarHUD");
    lbl_sb_stat = mk_label_w(stby, &ui_font_sm12, 86, 16, UI_W - 86 - M - 2, LV_TEXT_ALIGN_RIGHT);
    int gap = 12, x0 = (UI_W - 2 * FLIP_W - gap) / 2;
    for (int i = 0; i < 2; i++) {
        flip[i] = mk_card(stby, x0 + i * (FLIP_W + gap), FLIP_Y, FLIP_W, FLIP_H, 14);
        lv_obj_set_style_bg_grad_dir(flip[i], LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_clip_corner(flip[i], true, 0);
        lv_obj_set_style_shadow_width(flip[i], 6, 0);
        lv_obj_set_style_shadow_offset_y(flip[i], 2, 0);
        lv_obj_set_style_shadow_color(flip[i], lv_color_black(), 0);
        flip_bot[i] = mk_box(flip[i], 0, FLIP_H / 2 - 1, FLIP_W - 2, FLIP_H / 2 - 1);   /* nua duoi toi hon */
        lv_obj_set_style_bg_opa(flip_bot[i], LV_OPA_COVER, 0);
        lbl_clk[i] = mk_label_w(flip[i], &ui_font_clock60, 0, 0, FLIP_W - 2, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_y(lbl_clk[i], (FLIP_H - 2) / 2 - baseline(&ui_font_clock60) + 22);   /* giua chu so (cao ~44 px) nam tren khe lat */
        flip_line[i] = mk_box(flip[i], 0, FLIP_H / 2 - 2, FLIP_W - 2, 2);            /* khe lat giua the */
        lv_obj_set_style_bg_opa(flip_line[i], LV_OPA_COVER, 0);
    }
    /* chua ket noi dien thoai: dong ho dien tu 24 gio HH:MM, dau ":" nhap nhay moi nua giay */
    const lv_font_t *df = &ui_font_clock80;
    int w2 = text_w("00", df), wc = text_w(":", df), dx = (UI_W - 2 * w2 - wc) / 2;
    int dy = 112 - (baseline(df) - 29);                           /* giua chu so (cao ~58 px) o y = 112 */
    dg_h = mk_label_w(stby, df, dx, dy, w2, LV_TEXT_ALIGN_CENTER);
    dg_c = mk_label(stby, df, dx + w2, dy, ":");
    dg_m = mk_label_w(stby, df, dx + w2 + wc, dy, w2, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(dg_h, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(dg_m, LV_LABEL_LONG_CLIP);
    sb_bar = mk_card(stby, M, UI_H - M - 40, UI_W - 2 * M, 40, R_CARD);
    lbl_date = mk_label_w(sb_bar, &ui_font_txt14, 10, 10, 180, LV_TEXT_ALIGN_LEFT);
    lbl_gps = mk_label_w(sb_bar, &ui_font_txt14, UI_W - 2 * M - 2 - 10 - 110, 10, 110, LV_TEXT_ALIGN_RIGHT);
}

static void colon_cb(lv_timer_t *t)
{
    (void)t;
    if (screen != SCR_STANDBY || phone || clk_h < 0) lv_obj_set_style_text_opa(dg_c, LV_OPA_COVER, 0);
    else lv_obj_set_style_text_opa(dg_c, lv_obj_get_style_text_opa(dg_c, 0) ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

static void render_standby(void)
{
    if (!stby) return;
    char b[48];
    char hh[4] = "--", mm[4] = "--";
    if (clk_h >= 0) {
        snprintf(hh, sizeof hh, "%02d", clk_h % 24);
        snprintf(mm, sizeof mm, "%02d", clk_m % 60);
    }
    lv_label_set_text(lbl_clk[0], hh);
    lv_label_set_text(lbl_clk[1], mm);
    lv_label_set_text(dg_h, hh);
    lv_label_set_text(dg_m, mm);
    for (int i = 0; i < 2; i++) show(flip[i], phone);              /* da ket noi: dong ho lat; chua: dong ho dien tu */
    show(dg_h, !phone);
    show(dg_c, !phone);
    show(dg_m, !phone);
    if (day > 0 && month > 0 && wday >= 0) snprintf(b, sizeof b, "%s, %d Tháng %d", WDAY_NAME[wday % 7], day, month);
    else if (wday >= 0) snprintf(b, sizeof b, "%s", WDAY_NAME[wday % 7]);
    else snprintf(b, sizeof b, "Chờ giờ từ điện thoại");
    lv_label_set_text(lbl_date, b);
    lv_label_set_text(lbl_sb_stat, phone ? "Đã kết nối điện thoại" : "Chờ kết nối Waze qua điện thoại");
    lv_obj_set_style_text_color(lbl_sb_stat, th->accent, 0);
    lv_obj_set_style_text_opa(lbl_sb_stat, phone ? LV_OPA_COVER : LV_OPA_80, 0);
    const char *g = phone ? "GPS sẵn sàng" : (clk_h >= 0 && !clk_synced) ? "Giờ chưa chỉnh" : "Chưa có GPS";
    lv_label_set_text(lbl_gps, g);
    lv_obj_set_style_text_color(lbl_gps, phone ? th->accent : th->mut, 0);
}

/* Man chao: logo EVCarHUD (ui_logo.c, tao boi tools/logo/gen_logo.py) tren nen toi, hien dan ra; dong duoi: phien ban. */
extern const lv_image_dsc_t ui_logo;

lv_obj_t *ui_splash_create(const char *version)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x05080F), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &ui_logo);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -10);
    lv_obj_fade_in(img, 500, 0);
    lv_obj_t *v = lv_label_create(scr);
    lv_obj_set_style_text_font(v, &ui_font_sm12, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(0x9CA3AF), 0);
    if (version && version[0]) lv_label_set_text_fmt(v, "Phiên bản %s", version);
    else lv_label_set_text(v, "");
    lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0, -14);
    return scr;
}

void dashboard_create(lv_obj_t *screen_obj)
{
    scr = screen_obj;
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_click, LV_EVENT_SHORT_CLICKED, NULL);   /* cham: Ngay/Dem; giu 1 giay: xoay */
    lv_obj_add_event_cb(scr, on_long_press, LV_EVENT_LONG_PRESSED, NULL);

    build_nav();
    build_speed();
    build_sign();
    build_alert();
    build_eta();
    build_standby();
    colon_tmr = lv_timer_create(colon_cb, 500, NULL);

    /* vien do khi vuot toc do */
    frame = mk_box(scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_border_width(frame, 4, 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
    show(frame, false);
    blink_tmr = lv_timer_create(blink_cb, 400, NULL);
    lv_timer_pause(blink_tmr);

    /* thong bao nho */
    toast = mk_box(scr, 24, 102, UI_W - 48, 36);
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
    set_screen(SCR_STANDBY);
}

/* ======================= toc do / dong ho ======================= */
void ui_set_speed(int kmh)
{
    if (kmh >= MOVE_KMH) {                                         /* dang chay / dung yen lau: chon man */
        moving = true;
        stop_tick = 0;
    } else if (moving) {
        if (!stop_tick) stop_tick = lv_tick_get() | 1;
        else if (lv_tick_elaps(stop_tick) > STOP_MS) moving = false;
    }
    update_screen();
    if (kmh == speed) return;
    speed = kmh;
    char b[12];
    if (kmh < 0) snprintf(b, sizeof b, "--");
    else snprintf(b, sizeof b, "%d", kmh % 1000);
    lv_label_set_text(lbl_speed, b);
    update_level();
}

int ui_get_speed(void) { return speed; }

lv_obj_t *ui_screen(void) { return scr; }

void ui_set_speed_limit(int kmh)
{
    if (kmh == limit) return;
    limit = kmh;
    char b[12];
    if (kmh > 0) snprintf(b, sizeof b, "%d", kmh % 1000);
    else snprintf(b, sizeof b, "-");                               /* chua biet gioi han */
    lv_label_set_text(lbl_limit, b);
    layout_signs();
    update_level();
    if (screen == SCR_CRUISE) render_eta();
}

/* the gio den (dan duong) / loi nhac (chay thuong) */
static void fit_title(lv_obj_t *l, const char *t, int room)
{
    lv_obj_set_style_text_font(l, text_w(t, &ui_font_txt16) > room ? &ui_font_txt14 : &ui_font_txt16, 0);
    lv_label_set_text(l, t);
}

static int text_x(void) { return screen == SCR_CRUISE ? 6 + TILE + 6 : TXT_X; }

static void fit_sub(lv_obj_t *l, const char *t, int room)
{
    lv_obj_set_style_text_font(l, text_w(t, &ui_font_txt14) > room ? &ui_font_sm12 : &ui_font_txt14, 0);
    lv_label_set_text(l, t);
}

static void render_eta(void)
{
    int x = text_x(), room = lv_obj_get_width(card_eta) - x - 6;
    lv_obj_set_x(lbl_eta, x);
    lv_obj_set_x(lbl_eta2, x);
    char b[48], d[16];
    lv_obj_set_width(lbl_eta, room);
    lv_obj_set_width(lbl_eta2, room);
    lv_obj_set_style_text_font(lbl_eta2, &ui_font_mono14, 0);
    bool cam = false;

    if (screen == SCR_CRUISE) {                                   /* loi nhac khi chua dat lo trinh */
        if (!waze) {
            fit_title(lbl_eta, "Chỉ có GPS", room);
            fit_sub(lbl_eta2, "Chưa mở Waze", room);
        } else if (level == UI_LEVEL_OVER || is_camera(ev_code)) {
            cam = is_camera(ev_code);
            fit_title(lbl_eta, level == UI_LEVEL_OVER ? "Giảm tốc độ" : "Giảm tốc", room);
            if (limit > 0) snprintf(b, sizeof b, "Giới hạn %d", limit);
            else snprintf(b, sizeof b, "Sắp có camera");
            fit_sub(lbl_eta2, b, room);
        } else {
            if (clk_h >= 0) snprintf(b, sizeof b, "Bây giờ %02d:%02d", clk_h % 100, clk_m % 100);
            else snprintf(b, sizeof b, "Bây giờ --:--");
            fit_title(lbl_eta, b, room);
            fit_sub(lbl_eta2, street_now[0] ? street_now : "Thượng lộ bình an", room);
        }
    } else if (!has_route) {
        if (clk_h >= 0) snprintf(b, sizeof b, "Bây giờ %02d:%02d", clk_h % 100, clk_m % 100);
        else snprintf(b, sizeof b, "Bây giờ --:--");
        lv_obj_set_style_text_font(lbl_eta, &ui_font_txt16, 0);
        lv_label_set_text(lbl_eta, b);
        lv_label_set_text(lbl_eta2, "Chưa có tuyến");
    } else {
        if (eta_txt[0]) snprintf(b, sizeof b, "Đến %s", eta_txt);
        else snprintf(b, sizeof b, "Còn lại");
        lv_obj_set_style_text_font(lbl_eta, &ui_font_txt16, 0);
        lv_label_set_text(lbl_eta, b);
        fmt_dist(d, sizeof d, eta_rm > 0 ? eta_rm : -1);
        if (d[0] && eta_rmin > 0) {
            if (eta_rmin >= 60) snprintf(b, sizeof b, "%s · %dg%02d", d, eta_rmin / 60, eta_rmin % 60);
            else {
                snprintf(b, sizeof b, "%s · %d phút", d, eta_rmin);
                if (text_w(b, &ui_font_mono14) > room) snprintf(b, sizeof b, "%s · %d'", d, eta_rmin);
            }
        } else if (d[0]) {
            snprintf(b, sizeof b, "Còn %s", d);
        } else if (eta_rmin > 0) {
            snprintf(b, sizeof b, "Còn %d phút", eta_rmin);
        } else {
            b[0] = 0;
        }
        lv_label_set_text(lbl_eta2, b);
    }
    if (cam) lv_image_set_src(img_tip, &sign_camera);
    show(img_tip, cam);
    show(clk_face, !cam);
    show(clk_h_hand, !cam);
    show(clk_m_hand, !cam);
    lv_obj_set_style_bg_color(eta_tile, cam ? th->alert_tile : th->orange_bg, 0);
}

void ui_set_time(int hour, int minute, int wd)
{
    clk_h = hour;
    clk_m = minute;
    wday = wd;
    if (screen != SCR_NAV || !has_route) render_eta();
    render_standby();
    update_night();
}

void ui_set_clock_synced(bool synced)
{
    clk_synced = synced;
    render_standby();
}

void ui_set_date(int d, int m)
{
    day = d;
    month = m;
    render_standby();
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
    if (!nav_on) {                                                 /* chay thuong: ten duong hien o loi nhac */
        snprintf(street_now, sizeof street_now, "%s", street ? street : "");
        if (screen == SCR_CRUISE) render_eta();
    }
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
    update_screen();
}

void ui_set_route(const char *eta, int remaining_m, int remaining_min)
{
    has_route = (eta && eta[0]) || remaining_m > 0;
    snprintf(eta_txt, sizeof eta_txt, "%s", eta ? eta : "");
    eta_rm = remaining_m;
    eta_rmin = remaining_min;
    render_eta();
}

static void render_alerts(void)
{
    const ui_alert_t *ev = NULL, *sign = NULL;
    for (int i = 0; i < al_n; i++) {
        if (is_road_sign(al_list[i].code)) {
            if (!sign) sign = &al_list[i];
        } else if (!ev) {
            ev = &al_list[i];
        }
    }
    ev_code = ev ? ev->code : 0;

    int cw = lv_obj_get_width(card_al);
    has_alert = ev != NULL;
    show(al_tile, has_alert);
    int x = has_alert ? text_x() : 12;
    lv_obj_set_x(lbl_al, x);
    lv_obj_set_x(lbl_al_dist, x);
    lv_obj_set_width(lbl_al, cw - x - 6);
    lv_obj_set_width(lbl_al_dist, cw - x - 6);
    if (ev) {
        char b[24], d[16];
        lv_image_set_src(img_al, sign_for_alert(ev->code));
        const char *name = alert_name(ev->code);
        lv_obj_set_style_text_font(lbl_al, text_w(name, &ui_font_txt16) > cw - x - 6 ? &ui_font_txt14 : &ui_font_txt16, 0);
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

    bool had = !lv_obj_has_flag(img_sign, LV_OBJ_FLAG_HIDDEN);
    show(img_sign, sign != NULL);
    show(lbl_sign, sign != NULL);
    if (sign) {
        lv_image_set_src(img_sign, sign_for_alert(sign->code));
        lv_label_set_text(lbl_sign, alert_name(sign->code));
    }
    if (had != (sign != NULL)) layout_signs();                     /* 1 bien <-> 2 bien: can lai */
}

void ui_set_alerts(const ui_alert_t *alerts, int n)
{
    al_n = 0;
    for (int i = 0; alerts && i < n && al_n < (int)(sizeof al_list / sizeof al_list[0]); i++) al_list[al_n++] = alerts[i];
    int old_ev = ev_code;
    render_alerts();
    if (screen == SCR_CRUISE && ev_code != old_ev) render_eta();
}

void ui_set_chip(int kind, const char *text)
{
    (void)kind; (void)text;                                       /* camera / cam vuot da nam trong the canh bao */
}

void ui_set_signal(bool ok)
{
    sig_ok = ok;
    if (!ok) {
        moving = false;
        stop_tick = 0;
        ui_set_speed(-1);
        ui_set_speed_limit(0);
        ui_set_nav(false, 0, -1, 0, "", 0);
        ui_set_route("", 0, 0);
        ui_set_alerts(NULL, 0);
    }
    update_screen();
}

void ui_set_phone(bool connected)
{
    phone = connected;
    render_standby();
}

void ui_set_waze(bool on)
{
    if (on == waze) return;
    waze = on;
    if (screen == SCR_CRUISE) render_eta();
}

void ui_set_gps(int lvl) { (void)lvl; }

void ui_set_hint(const char *text) { (void)text; }               /* man cho da co dong "Cho ket noi Waze..." */

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
