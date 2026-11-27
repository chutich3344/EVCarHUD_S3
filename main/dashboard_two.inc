/* Bo cuc 2 cot cho man Chay thuong + Dan duong (nguoi dung chot tren gia lap 16/11), 320x240:
 *
 *  +--[xADAS]---------------------------------[waze]--+   thanh tren 24 px: chip xADAS (doi mau theo muc), nguon
 *  | +--------------+  +-------------------------+    |   cao toc: bien CT, loi ra + km, tram dung nghi + km
 *  | | km/h   14:05 |  |  (mui ten | vach | bien) |    |   the bien: dan duong thi chia doi (mui ten + khoang cach)
 *  | |              |  +-------------------------+    |
 *  | |      45      |  +-------------------------+    |   o duoi: xADAS (gan / qua gan uu tien), canh bao phia
 *  | |              |  |  TOI THIEU 55 m   42 m  |    |   truoc, ten duong + quang con lai, xADAS an toan;
 *  | +--------------+  +-------------------------+    |   khong co gi: the bien cao het cot
 *  +--------------------------------------------------+
 * Dong ho nam ngang hang km/h trong o toc do. Ban 2.8" khong co tro ly Mi Mi. */
#define W_TOP_H 24
#define W_SPX   8
#define W_SPW   152
#define W_CX    168
#define W_CW    144
#define W_Y     28
#define W_H     204
#define W_HALF  98
#define W_DIVX  70                                   /* vach chia trong the bien (tinh tu mep trai the) */

static lv_obj_t *two, *w_top, *w_line, *w_chip, *w_chip_dot, *w_chip_l, *w_src, *w_src_l, *w_gps;
static lv_obj_t *w_ct, *w_ex, *w_ex_l, *w_rs, *w_rs_l;
static lv_obj_t *w_sp, *w_kmh, *w_clk, *w_num;
static lv_obj_t *w_sc, *w_arr, *w_tdist, *w_div, *w_ring, *w_ring_l;
static lv_obj_t *w_lo, *w_lo_lab, *w_lo_num, *w_lo_unit, *w_lo_img;
static lv_obj_t *w_limg;                                 /* ban VML: anh bien gioi han hien tai */
static lv_obj_t *w_bring, *w_bring_l, *w_blimg, *w_vtile, *w_vtxt;   /* ban VML khong co toc do: bien to o o trai */
/* trang thai xADAS do xadas.c bao (ui_xadas_state) */
static int xs_kind = -1, xs_col;
static bool xs_dim;
static char xs_num[12], xs_unit[4], xs_lab[32], xs_chip[24];
/* 23/11: den giao thong tu app LANE (ui_tlight, goi T1): o duoi = hop den 3 bong + so giay; tu bo sau 3 s khong co goi */
#define TL_STALE_MS 3000
static char tl_col;                                  /* 'R' 'Y' 'G', 0 = khong */
static int tl_sec = -1;
static uint32_t tl_tick;
static lv_obj_t *w_tl_house, *w_tl_lamp[3];
static lv_timer_t *w_tl_tmr;
static void render_two(void);
static void update_screen(void);
static bool tl_live(void) { return tl_col && lv_tick_elaps(tl_tick) < TL_STALE_MS; }
static void tl_tmr_cb(lv_timer_t *t)
{
    (void)t;
    static bool was;
    static int blink = -1;
    bool live = tl_live();
    int bl = live && tl_col == 'G' && tl_sec >= 0 && tl_sec <= 3 ? (int)((lv_tick_get() / 250) % 2) : -1;
    if (live != was) update_screen();                              /* het den: duoc ve man cho lai */
    if ((live != was || bl != blink) && (screen == SCR_CRUISE || screen == SCR_NAV)) render_two();
    was = live;
    blink = bl;
    if (!live) tl_col = 0;
}

typedef struct { uint32_t bg, card, bd, fg, dim, line; } wpal_t;
static const wpal_t W_NIGHT = {0x11131F, 0x1E2235, 0x2E3450, 0xFFFFFF, 0xA3ACB5, 0x2E3450};
static const wpal_t W_DAY = {0xECEDF1, 0xF7F8FA, 0xDADDE5, 0x14112B, 0x4B5563, 0xD5D8E0};

static bool two_hw(void)
{
    return limit >= 80 || strstr(street_now, "Cao tốc") || strstr(street_now, "cao tốc");
}

static void w_card(lv_obj_t *o)
{
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 1, 0);
}

static void build_two(void)
{
    two = mk_box(scr, 0, 0, UI_W, UI_H);
    w_top = mk_box(two, 0, 0, UI_W, W_TOP_H);
    w_line = mk_fill(two, 0, W_TOP_H - 1, UI_W, 1, 0);
    w_chip = mk_box(w_top, 8, 3, 50, 18);
    lv_obj_set_style_radius(w_chip, 5, 0);
    lv_obj_set_style_border_width(w_chip, 1, 0);
    lv_obj_set_style_border_opa(w_chip, LV_OPA_COVER, 0);
    w_chip_dot = mk_fill(w_chip, 5, 6, 6, 6, 3);
    w_chip_l = mk_label(w_chip, &ui_font_cap12, 15, 1, "");
    w_gps = mk_label_w(w_top, &ui_font_sm12, 70, (W_TOP_H - ui_font_sm12.line_height) / 2, 180, LV_TEXT_ALIGN_CENTER);
    w_ct = mk_fill(w_top, 0, 4, 28, 16, 4);                       /* cao toc: bien xanh "CT" vien trang */
    lv_obj_set_style_bg_color(w_ct, lv_color_hex(0x0B7A4B), 0);
    lv_obj_set_style_border_width(w_ct, 1, 0);
    lv_obj_set_style_border_color(w_ct, lv_color_white(), 0);
    lv_obj_set_style_border_opa(w_ct, LV_OPA_COVER, 0);
    lv_obj_t *ctl = mk_label(w_ct, &ui_font_cap12, 0, 0, "CT");
    lv_obj_set_style_text_color(ctl, lv_color_white(), 0);
    lv_obj_center(ctl);
    w_ex = mk_fill(w_top, 0, 5, 14, 14, 3);                       /* loi ra: o xanh la, mui ten cheo */
    lv_obj_set_style_bg_color(w_ex, lv_color_hex(0x0B7A4B), 0);
    lv_obj_t *ea = mk_image(w_ex, 1, 1, 12 * 256 / 64);
    lv_image_set_src(ea, &arrow_slight_right);
    lv_obj_set_style_image_recolor(ea, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(ea, LV_OPA_COVER, 0);
    w_ex_l = mk_label(w_top, &ui_font_cap12, 0, (W_TOP_H - ui_font_cap12.line_height) / 2, "");
    w_rs = mk_fill(w_top, 0, 5, 14, 14, 3);                       /* tram dung nghi: o xanh duong, coc */
    lv_obj_set_style_bg_color(w_rs, lv_color_hex(0x1D4ED8), 0);
    lv_obj_t *cup = mk_box(w_rs, 2, 5, 7, 6);
    lv_obj_set_style_border_width(cup, 2, 0);
    lv_obj_set_style_border_color(cup, lv_color_white(), 0);
    lv_obj_set_style_border_opa(cup, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cup, 1, 0);
    lv_obj_t *hd = mk_box(w_rs, 8, 6, 4, 4);
    lv_obj_set_style_border_width(hd, 1, 0);
    lv_obj_set_style_border_color(hd, lv_color_white(), 0);
    lv_obj_set_style_border_opa(hd, LV_OPA_COVER, 0);
    lv_obj_t *sm = mk_fill(w_rs, 4, 1, 2, 3, 1);
    lv_obj_set_style_bg_color(sm, lv_color_white(), 0);
    w_rs_l = mk_label(w_top, &ui_font_cap12, 0, (W_TOP_H - ui_font_cap12.line_height) / 2, "");
    w_src = mk_fill(w_top, 0, 4, 34, 16, 4);
    w_src_l = mk_label(w_src, &ui_font_cap12, 0, 0, "waze");

    w_sp = mk_box(two, W_SPX, W_Y, W_SPW, W_H);                   /* o toc do */
    w_card(w_sp);
    w_kmh = mk_label(w_sp, &ui_font_txt14, 9, 6, "km/h");
    w_clk = mk_label_w(w_sp, &ui_font_txt14, W_SPW - 2 - 9 - 60, 6, 60, LV_TEXT_ALIGN_RIGHT);
    w_num = mk_label_w(w_sp, &ui_font_speed88, 0, 0, W_SPW - 2, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(w_num, LV_LABEL_LONG_CLIP);
    w_bring = mk_fill(w_sp, 0, 0, 120, 120, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(w_bring, lv_color_white(), 0);
    lv_obj_set_style_border_color(w_bring, lv_color_hex(0xE11D48), 0);
    lv_obj_set_style_border_opa(w_bring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w_bring, 14, 0);
    w_bring_l = mk_label(w_bring, &ui_font_num54, 0, 0, "-");
    lv_obj_set_style_text_color(w_bring_l, lv_color_hex(0x111111), 0);
    w_blimg = mk_image(w_sp, 0, 0, 256);
    lv_image_set_inner_align(w_blimg, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_obj_add_flag(w_bring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w_blimg, LV_OBJ_FLAG_HIDDEN);

    w_sc = mk_box(two, W_CX, W_Y, W_CW, W_HALF);                  /* the bien */
    w_card(w_sc);
    w_arr = mk_image(w_sc, 0, 0, 40 * 256 / 64);
    lv_obj_set_style_image_recolor_opa(w_arr, LV_OPA_COVER, 0);
    w_tdist = mk_label_w(w_sc, &ui_font_txt16, 2, 0, W_DIVX - 4, LV_TEXT_ALIGN_CENTER);
    w_div = mk_fill(w_sc, W_DIVX, 14, 1, 60, 0);
    w_ring = mk_fill(w_sc, 0, 0, 80, 80, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(w_ring, lv_color_white(), 0);
    lv_obj_set_style_border_color(w_ring, lv_color_hex(0xE11D48), 0);
    lv_obj_set_style_border_opa(w_ring, LV_OPA_COVER, 0);
    w_ring_l = mk_label(w_ring, &ui_font_sign40, 0, 0, "-");
    lv_obj_set_style_text_color(w_ring_l, lv_color_hex(0x111111), 0);
    w_limg = mk_image(w_sc, 0, 0, 256);
    lv_image_set_inner_align(w_limg, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_obj_add_flag(w_limg, LV_OBJ_FLAG_HIDDEN);
    w_vtile = ui_partner_tile(w_sc, UI_SRC_VML, 44);
    w_vtxt = mk_label_w(w_sc, &ui_font_cap12, 0, 0, W_CW - 2, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(w_vtxt, "Chờ biển báo, camera");
    lv_obj_add_flag(w_vtile, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w_vtxt, LV_OBJ_FLAG_HIDDEN);

    w_lo = mk_box(two, W_CX, W_Y + W_HALF + 8, W_CW, W_HALF);    /* o duoi */
    w_card(w_lo);
    w_lo_lab = mk_label_w(w_lo, &ui_font_cap12, 4, 0, W_CW - 12, LV_TEXT_ALIGN_CENTER);
    w_lo_img = mk_image(w_lo, 0, 0, 36 * 256 / 48);
    w_lo_num = mk_label(w_lo, &ui_font_num54, 0, 0, "");
    w_lo_unit = mk_label(w_lo, &ui_font_txt16, 0, 0, "");
    w_tl_house = mk_fill(w_lo, 8, 6, 30, 82, 10);                  /* den giao thong: hop doc 3 bong */
    lv_obj_set_style_bg_color(w_tl_house, lv_color_hex(0x0E1020), 0);
    lv_obj_set_style_border_width(w_tl_house, 1, 0);
    lv_obj_set_style_border_color(w_tl_house, lv_color_hex(0x3A4060), 0);
    lv_obj_set_style_border_opa(w_tl_house, LV_OPA_COVER, 0);
    for (int i = 0; i < 3; i++) {
        w_tl_lamp[i] = mk_fill(w_tl_house, 3, 3 + i * 25, 22, 22, LV_RADIUS_CIRCLE);
        lv_obj_set_style_shadow_spread(w_tl_lamp[i], 1, 0);
    }
    show(w_tl_house, false);
    w_tl_tmr = lv_timer_create(tl_tmr_cb, 125, NULL);
    show(two, false);
}

/* so + don vi, canh giua ngang trong o; y_base: chan chu so (toa do trong o) */
static void w_num_unit(lv_obj_t *n, lv_obj_t *u, const char *num, const char *unit, const lv_font_t *nf, int cw, int y_base)
{
    lv_obj_set_style_text_font(n, nf, 0);
    lv_label_set_text(n, num);
    lv_label_set_text(u, unit);
    int nw = text_w(num, nf), uw = unit[0] ? text_w(unit, &ui_font_txt16) + 3 : 0;
    int x = (cw - nw - uw) / 2;
    lv_obj_set_pos(n, x, y_base - baseline(nf));
    lv_obj_set_pos(u, x + nw + 3, y_base - baseline(&ui_font_txt16));
    show(u, unit[0] != 0);
}

static void render_two(void)
{
    const wpal_t *p = night ? &W_NIGHT : &W_DAY;
    lv_color_t fg = lv_color_hex(p->fg);
    lv_obj_set_style_bg_color(scr, lv_color_hex(p->bg), 0);
    lv_obj_set_style_bg_color(w_line, lv_color_hex(p->line), 0);
    lv_obj_t *cards[3] = {w_sp, w_sc, w_lo};
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(cards[i], lv_color_hex(p->card), 0);
        lv_obj_set_style_border_color(cards[i], lv_color_hex(p->bd), 0);
        lv_obj_set_style_border_width(cards[i], 1, 0);
    }
    bool nv = screen == SCR_NAV;
    bool hw = two_hw();

    /* --- o duoi: chon noi dung --- */
    bool xa_on = xs_kind >= 0, xa_alert = xs_kind == 1 || xs_kind == 2;
    const ui_alert_t *ev = ev_now;                 /* cao toc: loi ra / tram dung da o thanh tren, khong lap lai o duoi */
    if (ev && hw) {
        ev = NULL;
        for (int i = 0; i < al_n && !ev; i++) {
            int c = al_list[i].code;
            if (!is_road_sign(c) && c != 19 && c != 20 && c != 21 && c != 35 && c != 36) ev = &al_list[i];
        }
    }
    int lo;                                        /* 0 khong, 1 xADAS, 2 canh bao, 3 ten duong + quang con lai, 4 den giao thong */
    if (xa_alert) lo = 1;
    else if (tl_live()) lo = 4;
    else if (ev) lo = 2;
    else if (nv) lo = 3;
    else if (xa_on) lo = 1;
    else lo = 0;

    /* --- thanh tren --- */
    show(w_chip, xa_on);
    int lx = 8;
    if (xa_on) {
        static const uint32_t cd[] = {0x8C96A0, 0x34D399, 0xFFD400, 0xFF6B6B, 0x35D6EF, 0xFFD400};
        static const uint32_t cl[] = {0x4B5563, 0x047857, 0xB45309, 0xBE123C, 0x0E7490, 0xB45309};
        uint32_t c = (night ? cd : cl)[xs_col >= 0 && xs_col <= 5 ? xs_col : 0];
        char b[32];
        if (lo == 1 || xs_kind == 3) snprintf(b, sizeof b, "%s", xs_kind == 3 ? xs_chip : "xADAS");
        else snprintf(b, sizeof b, "xADAS %s %s", xs_num, xs_unit);      /* so met khong co cho o duoi: len chip */
        lv_label_set_text(w_chip_l, b);
        lv_obj_set_style_text_color(w_chip_l, lv_color_hex(c), 0);
        lv_obj_set_style_border_color(w_chip, lv_color_hex(c), 0);
        lv_obj_set_style_bg_color(w_chip_dot, lv_color_hex(c), 0);
        int cw = 15 + text_w(b, &ui_font_cap12) + 6;
        lv_obj_set_width(w_chip, cw);
        lx += cw + 5;
    }
    bool g = src == UI_SRC_GOFA, vm = src == UI_SRC_VML;
    const char *sn = vm ? "VML" : g ? "GOFA" : "waze";
    lv_label_set_text(w_src_l, sn);
    int sw = text_w(sn, &ui_font_cap12) + 10;
    lv_obj_set_width(w_src, sw);
    lv_obj_center(w_src_l);
    int rx = UI_W - 8 - sw;
    lv_obj_set_x(w_src, rx);
    lv_obj_set_style_bg_color(w_src, lv_color_hex(phone ? (vm ? 0xE4252B : g ? 0x0B8F4D : 0x33CCFF) : (night ? 0x4A5068 : 0xB9BDCA)), 0);
    lv_obj_set_style_text_color(w_src_l, lv_color_hex(phone ? (vm || g ? 0xFFFFFF : 0x05263A) : (night ? 0xFFFFFF : 0x14112B)), 0);
    const ui_alert_t *ex = NULL, *rs = NULL;
    if (hw)
        for (int i = 0; i < al_n; i++) {
            int c = al_list[i].code;
            if (!ex && (c == 20 || c == 21 || c == 35 || c == 36)) ex = &al_list[i];
            if (!rs && c == 19) rs = &al_list[i];
        }
    lv_obj_t *pi[2] = {w_rs, w_ex}, *pl[2] = {w_rs_l, w_ex_l};
    const ui_alert_t *pa[2] = {rs, ex};
    for (int i = 0; i < 2; i++) {                                  /* tu phai sang trai: tram dung, loi ra */
        show(pi[i], pa[i] != NULL);
        show(pl[i], pa[i] != NULL);
        if (!pa[i]) continue;
        char num[12], b[16];
        const char *u;
        fmt_dist2(num, sizeof num, &u, pa[i]->dist);
        snprintf(b, sizeof b, "%s%s", num, u);
        lv_label_set_text(pl[i], b);
        lv_obj_set_style_text_color(pl[i], fg, 0);
        rx -= 6 + text_w(b, &ui_font_cap12);
        lv_obj_set_x(pl[i], rx);
        rx -= 2 + 14;
        lv_obj_set_x(pi[i], rx);
    }
    show(w_ct, hw);
    if (hw) {
        lv_obj_set_x(w_ct, lx);
        lx += 28 + 5;
    }
    bool vst = vm && waze && nv && street_nav[0];                  /* 27/11 ban VML: ten duong (tu chi dan re) o dong tren, canh chu VML */
    show(w_gps, !waze || vst);
    if (vst) {
        lv_label_set_text(w_gps, street_nav);
        lv_label_set_long_mode(w_gps, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(w_gps, &ui_font_txt14, 0);
        lv_obj_set_style_text_align(w_gps, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_y(w_gps, (W_TOP_H - ui_font_txt14.line_height) / 2);
        lv_obj_set_style_text_color(w_gps, fg, 0);
        lv_obj_set_x(w_gps, lx);
        lv_obj_set_width(w_gps, rx - 8 - lx > 0 ? rx - 8 - lx : 0);
    } else {
        lv_obj_set_style_text_font(w_gps, &ui_font_sm12, 0);
        lv_obj_set_style_text_align(w_gps, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_y(w_gps, (W_TOP_H - ui_font_sm12.line_height) / 2);
    }
    if (!waze) {
        lv_label_set_text(w_gps, "Chỉ có GPS");
        lv_obj_set_style_text_color(w_gps, fg, 0);
        lv_obj_set_x(w_gps, lx);
        lv_obj_set_width(w_gps, rx - 6 - lx > 0 ? rx - 6 - lx : 0);
    }

    /* --- o toc do --- */
    char b[12];
    if (speed < 0) snprintf(b, sizeof b, "--");
    else snprintf(b, sizeof b, "%d", speed % 1000);
    const lv_font_t *nf = &ui_font_speed110;
    if (text_w(b, nf) > W_SPW - 16) nf = &ui_font_speed88;
    if (text_w(b, nf) > W_SPW - 16) nf = &ui_font_speed72;
    lv_obj_set_style_text_font(w_num, nf, 0);
    lv_obj_set_style_text_letter_space(w_num, -2, 0);
    lv_label_set_text(w_num, b);
    int cap = baseline(nf) * 95 / 100;                             /* chu so Inter ~ 0,73 co chu */
    int base = 30 + (W_H - 30) / 2 + cap / 2;
    lv_obj_set_y(w_num, base - baseline(nf));
    lv_color_t sc = speed < 0 ? lv_color_hex(p->dim) : level == UI_LEVEL_OVER ? th->over : lv_color_hex(night ? 0xFFB020 : 0xD97706);
    lv_obj_set_style_text_color(w_num, sc, 0);
    lv_obj_set_style_text_color(w_kmh, fg, 0);
    bool bs = vm && speed < 0;                                     /* VML khong gui toc do: o trai la bien hien tai co to */
    if (clk_h >= 0 && (!vm || clk_synced)) snprintf(b, sizeof b, "%02d:%02d", clk_h % 24, clk_m % 60);
    else snprintf(b, sizeof b, vm ? "" : "--:--");                 /* VML khong gui gio: an gio chua chinh */
    lv_label_set_text(w_clk, b);
    show(w_num, !bs);
    lv_label_set_text(w_kmh, bs ? "Biển hiện tại" : "km/h");
    {
        bool bimg = bs && vml_limit_img && limit <= 0;
        show(w_bring, bs && !bimg);
        show(w_blimg, bimg);
        const int BR = 120, bx = (W_SPW - 2 - BR) / 2, by = 30 + (W_H - 30 - BR) / 2 - 4;
        if (bs && !bimg) {
            char lb[8];
            if (limit > 0) snprintf(lb, sizeof lb, "%d", limit % 1000);
            else snprintf(lb, sizeof lb, "-");
            lv_obj_set_pos(w_bring, bx, by);
            lv_obj_set_style_text_font(w_bring_l, limit <= 0 ? &ui_font_sign40 : limit >= 100 ? &ui_font_sign40 : &ui_font_num54, 0);
            lv_label_set_text(w_bring_l, lb);
            lv_obj_center(w_bring_l);
        }
        if (bimg) {
            lv_image_set_src(w_blimg, vml_limit_img); lv_obj_invalidate(w_blimg);
            lv_image_set_scale(w_blimg, (uint32_t)(BR * 256 / (int)vml_limit_img->header.w));
            lv_obj_set_pos(w_blimg, bx, by);
        }
    }
    lv_obj_set_style_text_color(w_clk, fg, 0);

    /* --- the bien --- */
    bool full = bs && !nv && lo == 2;                              /* v10 VML khong dan duong: the canh bao chiem ca cot phai */
    show(w_sc, !full);
    lv_obj_set_y(w_lo, full ? W_Y : W_Y + W_HALF + 8);
    lv_obj_set_height(w_lo, full ? W_H : W_HALF);
    int sh = lo ? W_HALF : W_H;
    lv_obj_set_height(w_sc, sh);
    show(w_arr, nv);
    show(w_tdist, nv);
    show(w_div, nv && !bs);
    show(w_vtile, bs && !nv);
    show(w_vtxt, bs && !nv);
    int R;
    if (bs) {                                                      /* bien da o o trai: the nay chi co mui ten re */
        R = 0;
        if (nv) {
            const lv_image_dsc_t *arr = vml_nav_img ? vml_nav_img : arrow_for_turn(turn_now);
            int A = lo ? 52 : 80;
            lv_image_set_src(w_arr, arr ? arr : &arrow_straight); lv_obj_invalidate(w_arr);
            lv_image_set_scale(w_arr, (uint32_t)(A * 256 / (arr ? (int)arr->header.w : 64)));
            lv_obj_set_style_image_recolor(w_arr, fg, 0);
            lv_obj_set_style_image_opa(w_arr, LV_OPA_COVER, 0);
            char num[12], d[20];
            const char *u;
            fmt_dist2(num, sizeof num, &u, dist_now);
            if (num[0]) snprintf(d, sizeof d, "%s %s", num, u);
            else d[0] = 0;
            lv_label_set_text(w_tdist, d);
            lv_obj_set_style_text_color(w_tdist, fg, 0);
            lv_obj_set_width(w_tdist, W_CW - 4);
            int gh = A + (d[0] ? 4 + (int)ui_font_txt16.line_height : 0), y0 = (sh - 2 - gh) / 2;
            lv_obj_set_pos(w_arr, (W_CW - 2 - A) / 2, y0);
            lv_obj_set_y(w_tdist, y0 + A + 4);
        } else {
            int y0 = (sh - 2 - 44 - 6 - (int)ui_font_cap12.line_height) / 2;
            lv_obj_set_pos(w_vtile, (W_CW - 2 - 44) / 2, y0);
            lv_obj_set_y(w_vtxt, y0 + 50);
            lv_obj_set_style_text_color(w_vtxt, lv_color_hex(p->dim), 0);
        }
    } else if (nv) {
        lv_obj_set_width(w_tdist, W_DIVX - 4);
        R = 52;
        const lv_image_dsc_t *arr = vml_nav_img ? vml_nav_img : arrow_for_turn(turn_now);
        lv_image_set_src(w_arr, arr ? arr : &arrow_straight); lv_obj_invalidate(w_arr);
        lv_image_set_scale(w_arr, (uint32_t)(40 * 256 / (arr ? (int)arr->header.w : 64)));
        lv_obj_set_style_image_recolor(w_arr, fg, 0);
        lv_obj_set_style_image_opa(w_arr, arr ? LV_OPA_COVER : LV_OPA_60, 0);
        char num[12], d[20];
        const char *u;
        fmt_dist2(num, sizeof num, &u, dist_now);
        if (!arr) snprintf(d, sizeof d, "Chờ GPS");
        else if (num[0]) snprintf(d, sizeof d, "%s %s", num, u);
        else d[0] = 0;
        lv_label_set_text(w_tdist, d);
        lv_obj_set_style_text_color(w_tdist, fg, 0);
        int gh = 40 + (d[0] ? 2 + (int)ui_font_txt16.line_height : 0), y0 = (sh - 2 - gh) / 2;
        lv_obj_set_pos(w_arr, (W_DIVX - 40) / 2, y0);
        lv_obj_set_y(w_tdist, y0 + 42);
        lv_obj_set_pos(w_div, W_DIVX, 16);
        lv_obj_set_height(w_div, sh - 34);
        lv_obj_set_style_bg_color(w_div, lv_color_hex(night ? 0x3A4160 : 0xC9CDD6), 0);
    } else {
        R = lo ? 80 : 120;
    }
    if (bs) {
        show(w_ring, false);
        show(w_limg, false);
        goto lower;
    }
    lv_obj_set_size(w_ring, R, R);
    lv_obj_set_style_border_width(w_ring, R * 12 / 100, 0);
    int rcx = nv ? W_DIVX + 1 + (W_CW - 2 - W_DIVX - 1) / 2 : (W_CW - 2) / 2;
    lv_obj_set_pos(w_ring, rcx - R / 2, (sh - 2 - R) / 2);
    if (limit > 0) snprintf(b, sizeof b, "%d", limit % 1000);
    else snprintf(b, sizeof b, "-");
    const lv_font_t *rf;
    bool three = limit >= 100;
    if (R >= 120) rf = three ? &ui_font_sign40 : &ui_font_num54;
    else if (R >= 80) rf = three ? &ui_font_sign32 : &ui_font_sign40;
    else rf = three ? &ui_font_txt16 : &ui_font_sign24;
    if (limit <= 0 && rf == &ui_font_num54) rf = &ui_font_sign40;  /* num54 khong co dau "-" */
    lv_obj_set_style_text_font(w_ring_l, rf, 0);
    lv_label_set_text(w_ring_l, b);
    lv_obj_center(w_ring_l);
    bool limg = vml_limit_img && limit <= 0;                       /* ban VML: anh bien hien tai thay vong "-" */
    show(w_ring, !limg);
    show(w_limg, limg);
    if (limg) {
        lv_image_set_src(w_limg, vml_limit_img); lv_obj_invalidate(w_limg);
        lv_image_set_scale(w_limg, (uint32_t)(R * 256 / (int)vml_limit_img->header.w));
        lv_obj_set_pos(w_limg, rcx - R / 2, (sh - 2 - R) / 2);
    }

    /* --- o duoi --- */
lower:
    lv_obj_set_height(w_lo_lab, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(w_lo_lab, lo == 3 ? &ui_font_txt14 : &ui_font_cap12, 0);
    show(w_lo, lo != 0);
    show(w_lo_img, lo == 2);
    show(w_tl_house, lo == 4);
    if (lo == 1) {
        static const uint32_t bgN[4] = {0, 0x3A300A, 0x4A0F16, 0}, bgD[4] = {0, 0xFFF4C2, 0xFFE0E4, 0};
        static const uint32_t bdN[4] = {0, 0xFFD400, 0xFF4D4F, 0x4A5068}, bdD[4] = {0, 0xB45309, 0xBE123C, 0xC4C8D2};
        static const uint32_t nmN[4] = {0x34D399, 0xFFD400, 0xFF6B6B, 0x6B7280}, nmD[4] = {0x047857, 0xB45309, 0xBE123C, 0x9CA3AF};
        int k = xs_kind;
        if (k == 1 || k == 2) lv_obj_set_style_bg_color(w_lo, lv_color_hex((night ? bgN : bgD)[k]), 0);
        if (k) lv_obj_set_style_border_color(w_lo, lv_color_hex((night ? bdN : bdD)[k]), 0);
        lv_obj_set_style_border_width(w_lo, 2, 0);
        lv_obj_set_style_opa(w_lo, xs_dim ? 140 : LV_OPA_COVER, 0);
        lv_color_t nc = lv_color_hex((night ? nmN : nmD)[k]);
        show(w_lo_lab, xa_alert && xs_lab[0]);
        lv_label_set_text(w_lo_lab, xs_lab);
        lv_obj_set_style_text_color(w_lo_lab, night ? lv_color_white() : lv_color_hex(0x14112B), 0);
        const lv_font_t *f = k == 3 ? &ui_font_speed62 : &ui_font_num54;   /* num54 khong co "-" */
        int ncap = baseline(f) * 95 / 100;
        int lh = shown(w_lo_lab) ? (int)ui_font_cap12.line_height + 5 : 0;
        int top = (W_HALF - 4 - lh - ncap) / 2;
        lv_obj_set_y(w_lo_lab, top);
        w_num_unit(w_lo_num, w_lo_unit, xs_num, xs_unit, f, W_CW - 4, top + lh + ncap);
        lv_obj_set_style_text_color(w_lo_num, nc, 0);
        lv_obj_set_style_text_color(w_lo_unit, nc, 0);
    } else {
        lv_obj_set_style_opa(w_lo, LV_OPA_COVER, 0);
    }
    if (lo == 2) {                                                 /* canh bao phia truoc: bien + khoang cach, nen vang */
        lv_obj_set_style_bg_color(w_lo, night ? lv_color_hex(0x3A300A) : lv_color_hex(0xFFF4C2), 0);
        lv_obj_set_style_border_color(w_lo, night ? lv_color_hex(0xFFD400) : lv_color_hex(0xB45309), 0);
        lv_obj_set_style_border_width(w_lo, 2, 0);
        if (vml_alert_img) {                                        /* ban VML: anh bien / camera cua app */
            lv_image_set_src(w_lo_img, vml_alert_img); lv_obj_invalidate(w_lo_img);
            lv_image_set_scale(w_lo_img, 36 * 256 / vml_alert_img->header.w);
        } else {
            lv_image_set_src(w_lo_img, sign_for_alert(ev->code));
            lv_image_set_scale(w_lo_img, 36 * 256 / 48);
        }
        char num[12];
        const char *unit;
        if (ev->code == 8 && ev->value > 0) {
            snprintf(num, sizeof num, "%d", ev->value % 1000);
            unit = "km/h";
        } else {
            fmt_dist2(num, sizeof num, &unit, ev->dist);
        }
        if (full) {                                                /* v10: anh to + khoang cach to + chu (camera / tram thu phi + gia ve) */
            const int IM = 72;
            lv_image_set_scale(w_lo_img, (uint32_t)(IM * 256 / (vml_alert_img ? (int)vml_alert_img->header.w : 48)));
            lv_obj_set_pos(w_lo_img, (W_CW - 2 - IM) / 2, 10);
            const lv_font_t *f = text_w(num, &ui_font_num54) + 30 > W_CW - 8 ? &ui_font_num30 : &ui_font_num54;
            int nb = 10 + IM + 8 + baseline(f) * 95 / 100;
            w_num_unit(w_lo_num, w_lo_unit, num, unit, f, W_CW - 4, nb);
            show(w_lo_lab, vml_alert_txt[0] != 0);
            lv_obj_set_style_text_font(w_lo_lab, &ui_font_txt14, 0);
            lv_obj_set_width(w_lo_lab, W_CW - 12);
            lv_obj_set_x(w_lo_lab, 4);
            lv_obj_set_height(w_lo_lab, 3 * (int)ui_font_txt14.line_height);   /* LONG_DOT: xuong dong, qua 3 dong thi ... */
            lv_label_set_text(w_lo_lab, vml_alert_txt);
            lv_obj_set_style_text_color(w_lo_lab, night ? lv_color_white() : lv_color_hex(0x14112B), 0);
            lv_obj_set_y(w_lo_lab, nb + 10);
        } else {
        show(w_lo_lab, false);
        lv_obj_set_pos(w_lo_img, (W_CW - 2 - 36) / 2, 10);
        w_num_unit(w_lo_num, w_lo_unit, num, unit, &ui_font_num30, W_CW - 4, 10 + 36 + 8 + baseline(&ui_font_num30) * 95 / 100 + 4);
        }
        lv_color_t c = night ? lv_color_hex(0xFFD400) : lv_color_hex(0xB45309);
        lv_obj_set_style_text_color(w_lo_num, c, 0);
        lv_obj_set_style_text_color(w_lo_unit, c, 0);
    }
    if (lo == 4) {                                                 /* den giao thong (app LANE) */
        static const uint32_t lit[3] = {0xFF3B30, 0xFFC400, 0x22D36B}, off[3] = {0x3A1A1E, 0x3A3214, 0x14321F};
        static const uint32_t fgN[3] = {0xFF5A52, 0xFFD400, 0x34D399}, fgD[3] = {0xD7262B, 0xB45309, 0x047857};
        static const uint32_t bgN[3] = {0x32161C, 0x2E2A12, 0x123024}, bgD[3] = {0xFFE4E4, 0xFFF4C2, 0xDDF7E9};
        int ci = tl_col == 'R' ? 0 : tl_col == 'Y' ? 1 : 2;
        lv_color_t c = lv_color_hex((night ? fgN : fgD)[ci]);
        lv_obj_set_style_bg_color(w_lo, lv_color_hex((night ? bgN : bgD)[ci]), 0);
        lv_obj_set_style_border_color(w_lo, lv_color_hex(lit[ci]), 0);
        lv_obj_set_style_border_width(w_lo, 2, 0);
        for (int i = 0; i < 3; i++) {
            lv_obj_set_style_bg_color(w_tl_lamp[i], lv_color_hex(i == ci ? lit[i] : off[i]), 0);
            lv_obj_set_style_shadow_color(w_tl_lamp[i], lv_color_hex(lit[i]), 0);
            lv_obj_set_style_shadow_width(w_tl_lamp[i], i == ci ? 14 : 0, 0);
            lv_obj_set_style_shadow_opa(w_tl_lamp[i], i == ci ? 200 : 0, 0);
        }
        lv_obj_set_pos(w_tl_house, 8, (W_HALF - 4 - 82) / 2);
        static const char *const nm[3] = {"ĐÈN ĐỎ", "ĐÈN VÀNG", "ĐÈN XANH"};
        bool num = tl_sec >= 0;
        bool blink = ci == 2 && num && tl_sec <= 3 && (lv_tick_get() / 250) % 2;
        int rx = 8 + 30 + 4, rw = W_CW - 4 - rx - 4;
        show(w_lo_lab, true);
        lv_obj_set_width(w_lo_lab, rw);
        lv_obj_set_x(w_lo_lab, rx);
        lv_label_set_text(w_lo_lab, ci == 0 && num && tl_sec <= 3 ? "CHUẨN BỊ" : nm[ci]);
        if (num) {
            char nb[12];
            snprintf(nb, sizeof nb, "%d", tl_sec > 999 ? 999 : tl_sec);
            const lv_font_t *f = text_w(nb, &ui_font_num54) > rw - 14 ? &ui_font_num30 : &ui_font_num54;
            lv_obj_set_style_text_font(w_lo_lab, &ui_font_cap12, 0);
            lv_obj_set_style_text_color(w_lo_lab, night ? lv_color_white() : lv_color_hex(0x14112B), 0);
            int lh = (int)ui_font_cap12.line_height + 4, ncap = baseline(f) * 95 / 100;
            int top = (W_HALF - 4 - lh - ncap) / 2;
            lv_obj_set_y(w_lo_lab, top);
            w_num_unit(w_lo_num, w_lo_unit, nb, "s", f, rw, top + lh + ncap);
            lv_obj_set_x(w_lo_num, lv_obj_get_style_x(w_lo_num, 0) + rx);
            lv_obj_set_x(w_lo_unit, lv_obj_get_style_x(w_lo_unit, 0) + rx);
            lv_obj_set_style_text_color(w_lo_num, c, 0);
            lv_obj_set_style_text_color(w_lo_unit, c, 0);
            show(w_lo_num, !blink);
            show(w_lo_unit, !blink);
        } else {
            lv_obj_set_style_text_font(w_lo_lab, &ui_font_txt16, 0);
            lv_obj_set_style_text_color(w_lo_lab, c, 0);
            lv_obj_set_y(w_lo_lab, (W_HALF - 4 - (int)ui_font_txt16.line_height) / 2);
            show(w_lo_num, false);
            show(w_lo_unit, false);
        }
    } else {
        show(w_lo_num, true);
        lv_obj_set_x(w_lo_lab, 4);
        lv_obj_set_width(w_lo_lab, W_CW - 12);
    }
    if (lo == 3) {                                                 /* ten duong + quang con lai */
        lv_obj_set_style_border_width(w_lo, 1, 0);
        const char *st = street_nav[0] ? street_nav : street_now[0] ? street_now : "Đang dẫn đường";
        show(w_lo_lab, true);
        lv_obj_set_style_text_font(w_lo_lab, &ui_font_txt14, 0);
        lv_label_set_text(w_lo_lab, st);
        lv_obj_set_style_text_color(w_lo_lab, fg, 0);
        char num[12];
        const char *u;
        fmt_dist2(num, sizeof num, &u, eta_rm > 0 ? eta_rm : -1);
        if (!num[0]) {                                             /* chua co quang con lai: gio den, khong thi gio hien tai */
            if (eta_txt[0]) snprintf(num, sizeof num, "%s", eta_txt);
            else if (clk_h >= 0 && (!vm || clk_synced)) snprintf(num, sizeof num, "%02d:%02d", clk_h % 24, clk_m % 60);
            else snprintf(num, sizeof num, vm ? "" : "--");        /* VML: gio chua chinh thi chi hien ten duong */
            u = "";
        }
        int lh = (int)ui_font_txt14.line_height + 4, ncap = num[0] ? baseline(&ui_font_num30) * 95 / 100 : 0;
        int top = (W_HALF - 4 - lh - ncap) / 2;
        lv_obj_set_y(w_lo_lab, top);
        const lv_font_t *f = strchr(num, ':') || num[0] == '-' ? &ui_font_dist24 : &ui_font_num30;
        w_num_unit(w_lo_num, w_lo_unit, num, u, f, W_CW - 4, top + lh + ncap);
        lv_color_t c = night ? lv_color_hex(0x38BDF8) : lv_color_hex(0x0369A1);
        lv_obj_set_style_text_color(w_lo_num, c, 0);
        lv_obj_set_style_text_color(w_lo_unit, c, 0);
    }
}

void ui_tlight(char col, int sec)
{
    if (col != 'R' && col != 'Y' && col != 'G') col = 0;
    bool chg = col != tl_col || (col && sec != tl_sec);
    tl_col = col;
    tl_sec = col ? sec : -1;
    tl_tick = lv_tick_get();
    if (col) update_screen();                                      /* dang o man cho (do den lau): ve man chay */
    if (chg && (screen == SCR_CRUISE || screen == SCR_NAV)) render_two();
}

void ui_xadas_state(int kind, const char *num, const char *unit, const char *minlab, const char *chip, int color, bool dim)
{
    xs_kind = kind;
    xs_col = color;
    xs_dim = dim;
    snprintf(xs_num, sizeof xs_num, "%s", num ? num : "");
    snprintf(xs_unit, sizeof xs_unit, "%s", unit ? unit : "");
    snprintf(xs_lab, sizeof xs_lab, "%s", minlab ? minlab : "");
    snprintf(xs_chip, sizeof xs_chip, "%s", chip ? chip : "");
    if (screen == SCR_CRUISE || screen == SCR_NAV) render_two();
}
