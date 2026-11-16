/* Cap nhat firmware OTA qua app dien thoai: xem ota.h. Ham GATT (task NimBLE) chi kiem vi tri + chep vao bo dem;
 * task "ota" ghi flash, gui notify tien do va ve man "Dang cap nhat firmware" (lop tren cung, che ca bang dieu khien). */
#include "ota.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "lvgl.h"

#include "board.h"
#include "dashboard.h"
#include "ui_fonts.h"

static const char *TAG = "ota";

#define CONFIRM_US (30LL * 1000000)       /* ban moi chay on 30 giay: xac nhan */
#define ACK_EVERY  4096                   /* notify "P" moi 4 KB */
#define BUF_BYTES  10240                  /* app gui truoc toi da 6 KB: khong tran */
#define PKT_MAX    520

/* ======================= ghi firmware: bo qua dau file gop, dung dung do dai anh firmware ======================= */
typedef struct {
    esp_ota_handle_t h;
    const esp_partition_t *part;
    size_t skip, in, app, app_len;
    uint8_t hb[24];
    size_t hb_n, hdr_at, hdr_need;
    int stage, seg_left;
    bool hash;
    char err[64];
} sink_t;

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static bool sink_parse(sink_t *s, const uint8_t *d, size_t n)
{
    size_t base = s->app;
    while (!s->app_len) {
        size_t at = s->hdr_at + s->hb_n;
        if (at >= base + n) break;
        size_t i = at - base, take = s->hdr_need - s->hb_n;
        if (take > n - i) take = n - i;
        memcpy(s->hb + s->hb_n, d + i, take);
        s->hb_n += take;
        if (s->hb_n < s->hdr_need) break;
        if (s->stage == 0) {
            if (s->hb[0] != 0xE9 || s->hb[1] == 0 || s->hb[1] > 16) { snprintf(s->err, sizeof s->err, "File khong phai firmware"); return false; }
            s->seg_left = s->hb[1];
            s->hash = s->hb[23] == 1;
            s->hdr_at = 24;
            s->hdr_need = 8;
            s->stage = 1;
        } else {
            s->hdr_at += 8 + le32(s->hb + 4);
            if (--s->seg_left == 0) {
                s->app_len = (s->hdr_at & ~(size_t)15) + 16 + (s->hash ? 32 : 0);
                if (s->app_len > s->part->size) { snprintf(s->err, sizeof s->err, "Firmware lon hon vung OTA"); return false; }
            }
        }
        s->hb_n = 0;
    }
    return true;
}

/* 0: can them, 1: du firmware, -1: loi */
static int sink_feed(sink_t *s, const uint8_t *d, size_t n)
{
    if (s->in < s->skip) {
        size_t k = s->skip - s->in < n ? s->skip - s->in : n;
        s->in += k;
        d += k;
        n -= k;
    }
    s->in += n;
    if (!n) return 0;
    if (s->app_len && s->app >= s->app_len) return 1;
    if (!sink_parse(s, d, n)) return -1;
    if (s->app_len && s->app + n > s->app_len) n = s->app_len - s->app;
    if (n) {
        esp_err_t e = esp_ota_write(s->h, d, n);
        if (e != ESP_OK) { snprintf(s->err, sizeof s->err, "Ghi flash loi %s", esp_err_to_name(e)); return -1; }
        s->app += n;
    }
    return s->app_len && s->app >= s->app_len ? 1 : 0;
}

/* ======================= man "Dang cap nhat firmware" ======================= */
static lv_obj_t *u_scr, *u_title, *u_pct, *u_bar, *u_fill, *u_sub;

static void ui_show(const char *title, int pct, const char *sub)
{
    board_ui_lock();
    if (!u_scr) {
        u_scr = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(u_scr);
        lv_obj_set_size(u_scr, UI_W, UI_H);
        lv_obj_set_style_bg_color(u_scr, lv_color_hex(0x0B1220), 0);
        lv_obj_set_style_bg_opa(u_scr, LV_OPA_COVER, 0);
        lv_obj_add_flag(u_scr, LV_OBJ_FLAG_CLICKABLE);              /* chan cham xuong man duoi */
        u_title = lv_label_create(u_scr);
        lv_obj_set_style_text_font(u_title, &ui_font_txt16, 0);
        lv_obj_set_style_text_color(u_title, lv_color_white(), 0);
        lv_obj_set_width(u_title, UI_W - 32);
        lv_obj_set_style_text_align(u_title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(u_title, LV_ALIGN_TOP_MID, 0, 56);
        u_pct = lv_label_create(u_scr);
        lv_obj_set_style_text_font(u_pct, &ui_font_dist24, 0);
        lv_obj_set_style_text_color(u_pct, lv_color_hex(0x22C55E), 0);
        lv_obj_align(u_pct, LV_ALIGN_TOP_MID, 0, 96);
        u_bar = lv_obj_create(u_scr);
        lv_obj_remove_style_all(u_bar);
        lv_obj_set_size(u_bar, UI_W - 64, 10);
        lv_obj_set_style_radius(u_bar, 5, 0);
        lv_obj_set_style_bg_color(u_bar, lv_color_hex(0x1F2A3D), 0);
        lv_obj_set_style_bg_opa(u_bar, LV_OPA_COVER, 0);
        lv_obj_align(u_bar, LV_ALIGN_TOP_MID, 0, 138);
        u_fill = lv_obj_create(u_bar);
        lv_obj_remove_style_all(u_fill);
        lv_obj_set_size(u_fill, 0, 10);
        lv_obj_set_style_radius(u_fill, 5, 0);
        lv_obj_set_style_bg_color(u_fill, lv_color_hex(0x22C55E), 0);
        lv_obj_set_style_bg_opa(u_fill, LV_OPA_COVER, 0);
        u_sub = lv_label_create(u_scr);
        lv_obj_set_style_text_font(u_sub, &ui_font_sm12, 0);
        lv_obj_set_style_text_color(u_sub, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_width(u_sub, UI_W - 32);
        lv_obj_set_style_text_align(u_sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(u_sub, LV_ALIGN_TOP_MID, 0, 164);
    }
    lv_label_set_text(u_title, title);
    if (pct >= 0) {
        lv_label_set_text_fmt(u_pct, "%d%%", pct > 100 ? 100 : pct);
        lv_obj_set_width(u_fill, (UI_W - 64) * (pct > 100 ? 100 : pct) / 100);
    }
    lv_obj_align(u_pct, LV_ALIGN_TOP_MID, 0, 96);
    lv_label_set_text(u_sub, sub ? sub : "");
    lv_obj_remove_flag(u_scr, LV_OBJ_FLAG_HIDDEN);
    board_ui_unlock();
}

static void ui_hide(const char *toast)
{
    board_ui_lock();
    if (u_scr) lv_obj_add_flag(u_scr, LV_OBJ_FLAG_HIDDEN);
    if (toast) ui_toast(toast);
    board_ui_unlock();
}

/* ======================= nhan tu app ======================= */
enum { M_BEGIN = 1, M_END, M_ABORT, M_LOST };
typedef struct { uint8_t kind; uint32_t a, b; } ctrl_t;

static MessageBufferHandle_t s_mb;       /* goi DATA (khong kem vi tri) va lenh, theo dung thu tu */
static void (*s_notify)(const char *);
static volatile bool s_active;           /* dang nhan (sau "B", truoc "E"/"A") */
static volatile uint32_t s_expect;       /* vi tri byte tiep theo HUD nhan */
static volatile uint32_t s_total;
static volatile uint32_t s_gap_at = UINT32_MAX;   /* da bao "R" cho vi tri nay */
static esp_timer_handle_t s_ok_tmr;
static sink_t s_sink;

static void notify(const char *m)
{
    ESP_LOGI(TAG, "-> app: %s", m);
    if (s_notify) s_notify(m);
}

static void push_ctrl(uint8_t kind, uint32_t a, uint32_t b)
{
    uint8_t m[1 + sizeof(ctrl_t)];
    ctrl_t c = {.kind = kind, .a = a, .b = b};
    m[0] = 0;                                                      /* 0 = lenh, 1 = du lieu */
    memcpy(m + 1, &c, sizeof c);
    xMessageBufferSend(s_mb, m, sizeof m, pdMS_TO_TICKS(50));
}

int ota_ble_ctrl(const uint8_t *d, size_t n)
{
    char s[48];
    if (n >= sizeof s) return 0x0D;                                 /* BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN */
    memcpy(s, d, n);
    s[n] = 0;
    ESP_LOGI(TAG, "<- app: %s", s);
    if (s[0] == 'B' && s[1] == '|') {
        char *p = s + 2;
        uint32_t size = strtoul(p, &p, 10), skip = *p == '|' ? strtoul(p + 1, NULL, 10) : 0;
        s_active = false;
        xMessageBufferReset(s_mb);
        s_expect = 0;
        s_gap_at = UINT32_MAX;
        s_total = size;
        push_ctrl(M_BEGIN, size, skip);
    } else if (s[0] == 'E') {
        push_ctrl(M_END, 0, 0);
    } else if (s[0] == 'A') {
        s_active = false;
        push_ctrl(M_ABORT, 0, 0);
    }
    return 0;
}

int ota_ble_data(const uint8_t *d, size_t n)
{
    if (!s_active || n <= 4 || n - 4 > PKT_MAX) return 0;
    uint32_t off = le32(d);
    if (off != s_expect) {                                         /* trung lap (bo qua) hoac mat goi (bao vi tri can gui) */
        if (off > s_expect && s_gap_at != s_expect) {
            s_gap_at = s_expect;
            push_ctrl(M_LOST, s_expect, 0);
        }
        return 0;
    }
    uint8_t m[1 + PKT_MAX];
    m[0] = 1;
    memcpy(m + 1, d + 4, n - 4);
    if (xMessageBufferSend(s_mb, m, n - 3, 0) != n - 3) return 0;  /* day: bo, app se gui lai */
    s_expect = off + (n - 4);
    return 0;
}

int ota_ble_info(char *out, size_t cap)
{
    uint8_t m[6];
    esp_efuse_mac_get_default(m);
    const esp_app_desc_t *a = esp_app_get_description();
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    return snprintf(out, cap, "OTA1|%s|%s|%02X:%02X:%02X:%02X:%02X:%02X|%s|%u", a->project_name, a->version,
                    m[0], m[1], m[2], m[3], m[4], m[5], CONFIG_EVCARHUD_OTA_MODEL, p ? (unsigned)p->size : 0);
}

void ota_ble_disconnected(void)
{
    if (s_active) {
        s_active = false;
        push_ctrl(M_ABORT, 1, 0);
    }
}

void ota_set_notify(void (*fn)(const char *msg)) { s_notify = fn; }

static void fail(const char *why)
{
    char m[80];
    ESP_LOGW(TAG, "%s", why);
    s_active = false;
    if (s_sink.h) { esp_ota_abort(s_sink.h); s_sink.h = 0; }
    snprintf(m, sizeof m, "ER|%s", why);
    notify(m);
    ui_hide("Cập nhật firmware lỗi, vẫn chạy bản cũ");
}

static void ota_task(void *arg)
{
    (void)arg;
    static uint8_t m[1 + PKT_MAX];
    size_t acked = 0;
    int last_pct = -1;
    for (;;) {
        size_t n = xMessageBufferReceive(s_mb, m, sizeof m, portMAX_DELAY);
        if (!n) continue;
        if (m[0] == 1) {                                           /* du lieu */
            if (!s_sink.h) continue;
            int r = sink_feed(&s_sink, m + 1, n - 1);
            if (r < 0) { fail(s_sink.err); continue; }
            if (s_sink.in - acked >= ACK_EVERY || s_sink.in >= s_total) {
                acked = s_sink.in;
                char a[24];
                snprintf(a, sizeof a, "P|%u", (unsigned)acked);
                notify(a);
            }
            int pct = s_total ? (int)((uint64_t)s_sink.in * 100 / s_total) : 0;
            if (pct != last_pct) {
                last_pct = pct;
                char sub[64];
                snprintf(sub, sizeof sub, "%u / %u KB · giữ điện thoại gần HUD", (unsigned)(s_sink.in / 1024), (unsigned)(s_total / 1024));
                ui_show("Đang cập nhật firmware", pct, sub);
            }
            continue;
        }
        ctrl_t c;
        memcpy(&c, m + 1, sizeof c);
        switch (c.kind) {
        case M_BEGIN: {
            if (s_sink.h) { esp_ota_abort(s_sink.h); s_sink.h = 0; }
            memset(&s_sink, 0, sizeof s_sink);
            s_sink.hdr_need = 24;
            s_sink.skip = c.b;
            s_sink.part = esp_ota_get_next_update_partition(NULL);
            if (!s_sink.part) { fail("Bang phan vung chua co OTA, nap lai file gop qua USB"); break; }
            esp_err_t e = esp_ota_begin(s_sink.part, OTA_WITH_SEQUENTIAL_WRITES, &s_sink.h);
            if (e != ESP_OK) { s_sink.h = 0; fail(esp_err_to_name(e)); break; }
            acked = 0;
            last_pct = -1;
            s_active = true;
            ESP_LOGI(TAG, "bat dau: %u byte, bo qua %u, ghi vao %s", (unsigned)c.a, (unsigned)c.b, s_sink.part->label);
            ui_show("Đang cập nhật firmware", 0, "Giữ điện thoại gần HUD, không tắt nguồn");
            notify("OK|B");
            break;
        }
        case M_LOST: {
            char a[24];
            snprintf(a, sizeof a, "R|%u", (unsigned)c.a);
            notify(a);
            break;
        }
        case M_END: {
            s_active = false;
            if (!s_sink.h) { notify("ER|Chua bat dau"); break; }
            if (!s_sink.app_len || s_sink.app < s_sink.app_len) { fail("File thieu du lieu"); break; }
            ui_show("Đang kiểm tra firmware...", 100, "Không tắt nguồn HUD");
            esp_err_t e = esp_ota_end(s_sink.h);
            s_sink.h = 0;
            if (e != ESP_OK) { fail(e == ESP_ERR_OTA_VALIDATE_FAILED ? "Firmware hong hoac sai chip" : esp_err_to_name(e)); break; }
            esp_app_desc_t nd;
            if (esp_ota_get_partition_description(s_sink.part, &nd) != ESP_OK ||
                strcmp(nd.project_name, esp_app_get_description()->project_name)) {
                fail("Khong phai firmware cua HUD nay");
                break;
            }
            if (esp_ota_set_boot_partition(s_sink.part) != ESP_OK) { fail("Khong dat duoc vung khoi dong"); break; }
            char a[48], t[64];
            snprintf(a, sizeof a, "OK|E|%s", nd.version);
            notify(a);
            snprintf(t, sizeof t, "Đã cài bản %s", nd.version);
            ui_show(t, 100, "Khởi động lại sau 2 giây...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
            break;
        }
        case M_ABORT:
            if (s_sink.h) { esp_ota_abort(s_sink.h); s_sink.h = 0; }
            s_active = false;
            ESP_LOGW(TAG, "huy cap nhat%s", c.a ? " (mat Bluetooth)" : "");
            ui_hide(c.a ? "Mất Bluetooth, đã huỷ cập nhật" : "Đã huỷ cập nhật");
            break;
        }
    }
}

/* ======================= xac nhan ban moi / bao quay ve ======================= */
static void confirm_cb(void *a)
{
    (void)a;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        char t[64];
        snprintf(t, sizeof t, "Đã cập nhật lên bản %s", esp_app_get_description()->version);
        ESP_LOGI(TAG, "%s", t);
        board_ui_lock();
        ui_toast(t);
        board_ui_unlock();
    }
}

static void report_rollback(void)
{
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    esp_app_desc_t d;
    nvs_handle_t nh;
    if (!bad || esp_ota_get_partition_description(bad, &d) != ESP_OK) return;
    if (nvs_open("ota", NVS_READWRITE, &nh) != ESP_OK) return;
    char seen[33] = "";
    size_t n = sizeof seen;
    nvs_get_str(nh, "bad", seen, &n);
    if (strcmp(seen, d.version)) {
        nvs_set_str(nh, "bad", d.version);
        nvs_commit(nh);
        char t[96];
        snprintf(t, sizeof t, "Bản %.31s lỗi, đã quay về bản cũ", d.version);
        board_ui_lock();
        ui_toast(t);
        board_ui_unlock();
    }
    nvs_close(nh);
}

void ota_start(void)
{
    s_mb = xMessageBufferCreate(BUF_BYTES);
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    ESP_LOGI(TAG, "dang chay %s tu %s", esp_app_get_description()->version, run ? run->label : "?");
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_timer_create_args_t a = {.callback = confirm_cb, .name = "ota_ok"};
        esp_timer_create(&a, &s_ok_tmr);
        esp_timer_start_once(s_ok_tmr, CONFIRM_US);
    } else {
        report_rollback();
    }
    if (s_mb) xTaskCreate(ota_task, "ota", 4096, NULL, 3, NULL);
}
