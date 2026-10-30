/* Che do trinh dien: tu sinh cac dong HLP/1 gia (giong Waze Mod gui) va dua vao chinh bo giai ma that,
 * nen xem duoc toan bo giao dien khi chua noi dien thoai. Bat/tat: giu BOOT 3 giay (xem buttons.c).
 * Chay trong task LVGL (lv_timer), khong can khoa them. */
#include "demo.h"
#include "dashboard_hlp.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    int secs;               /* do dai chang (giay) */
    int lim, spd0, spd1;    /* gioi han, toc do dau -> cuoi chang */
    int trn, trn2, dst0;    /* re, re tiep, khoang cach re dau chang (m) */
    int exit_n;
    const char *st, *st2;
    int alr, alr_d0;        /* canh bao gan nhat va khoang cach dau chang */
    int alr2, alr2_d0;
    int avg;                /* vung cam vuot */
} leg_t;

static const leg_t LEGS[] = {
    {10, 50, 28, 46, 1, 2, 600, 0, "Nguyễn Trãi", "Nguyễn Trãi", 1, 900, 23, 300, 0},
    {8, 60, 46, 64, 3, 9, 350, 0, "Nguyễn Hữu Thọ", "Lê Văn Lương", 2, 250, 9, 500, 0},
    {10, 80, 62, 78, 10, 0, 800, 2, "Đại lộ Võ Văn Kiệt", "Vòng xoay Phú Lâm", 6, 700, 0, 0, 1},
    {8, 50, 70, 46, 2, 0, 300, 0, "Hậu Giang", "Hậu Giang", 8, 200, 28, 150, 0},
    {6, 0, 30, 0, 0, 0, -1, 0, "Đường số 7", "", 0, 0, 0, 0, 0},
};
#define N_LEGS ((int)(sizeof LEGS / sizeof LEGS[0]))

static lv_timer_t *tmr;
static uint32_t t0;
static uint32_t seq;

static void feed(const char *line)
{
    hlp_dash_feed((const uint8_t *)line, strlen(line));
    hlp_dash_feed((const uint8_t *)"\n", 1);
}

static int lerp(int a, int b, int num, int den) { return den <= 0 ? b : a + (b - a) * num / den; }

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t ms = lv_tick_elaps(t0);
    int total = 0;
    for (int i = 0; i < N_LEGS; i++) total += LEGS[i].secs * 1000;
    int pos = (int)(ms % (uint32_t)total), k = 0;
    while (pos >= LEGS[k].secs * 1000) pos -= LEGS[k++].secs * 1000;
    const leg_t *L = &LEGS[k];
    int len = L->secs * 1000;

    int spd = lerp(L->spd0, L->spd1, pos, len);
    int dst = L->dst0 < 0 ? -1 : lerp(L->dst0, 20, pos, len);
    int ad = L->alr ? lerp(L->alr_d0, 30, pos, len) : -1;
    int ad2 = L->alr2 ? lerp(L->alr2_d0, 300, pos, len) : -1;
    int rm = 6700 - (int)(ms / 100) % 6000;

    char alrs[160] = "";
    if (L->alr && L->alr2) snprintf(alrs, sizeof alrs, ",\"alrs\":[{\"k\":%d,\"d\":%d},{\"k\":%d,\"d\":%d}]", L->alr, ad, L->alr2, ad2);
    else if (L->alr) snprintf(alrs, sizeof alrs, ",\"alrs\":[{\"k\":%d,\"d\":%d,\"v\":%d}]", L->alr, ad, L->alr == 8 ? 40 : -1);
    else snprintf(alrs, sizeof alrs, ",\"alrs\":[]");

    char line[512];
    snprintf(line, sizeof line,
             "{\"v\":1,\"t\":\"s\",\"ts\":%u,\"nav\":%d,\"spd\":%d,\"lim\":%d,\"over\":%d,\"trn\":%d,\"trn2\":%d,"
             "\"dst\":%d,\"exit\":%d,\"st\":\"%s\",\"st2\":\"%s\",\"eta\":\"20:05\",\"rm\":%d,\"rmin\":%d,"
             "\"avg\":%d,\"avgL\":%d,\"alr\":%d,\"alrD\":%d%s}",
             (unsigned)(++seq * 250), L->trn ? 1 : 0, spd, L->lim, L->lim > 0 && spd > L->lim, L->trn, L->trn2,
             dst, L->exit_n, L->st, L->st2, rm, rm / 370 + 1, L->avg, L->avg ? lerp(1200, 200, pos, len) : 0, L->alr, ad, alrs);
    feed(line);
}

void demo_start(void)
{
    if (tmr) return;
    t0 = lv_tick_get();
    seq = 0;
    hlp_dash_on_connect();
    /* gio gia: 13:45 thu Nam (mui gio +7) */
    feed("{\"v\":1,\"t\":\"hi\",\"sess\":1,\"unix\":1761806700,\"tz\":420,\"rate\":4}");
    tmr = lv_timer_create(tick_cb, 250, NULL);
    tick_cb(tmr);
}

void demo_stop(void)
{
    if (!tmr) return;
    lv_timer_delete(tmr);
    tmr = NULL;
    hlp_dash_on_disconnect();
}

bool demo_running(void) { return tmr != NULL; }

void demo_step(int ms)
{
    if (!tmr) return;
    t0 -= (uint32_t)ms;                                  /* chi dung cho anh xem truoc tren PC: nhay toi thoi diem khac */
    tick_cb(tmr);
}
