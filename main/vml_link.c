/* Ban 2.8 VML: HUD gia lam HUD VietMap H1V qua Bluetooth thuong (SPP) de app VietMap Live gui chi dan.
 *
 * Giao thuc (giai tu goi that, xem vietmap-hud-test trong repo EVCarHUD_P4):
 *   khung: A5 5A 37 C3 | do dai BE32 (tong = do dai + 8) | ... | lenh o byte 12-13 | ... | 8 byte duoi
 *   0E 00 (khung 22 byte): app hoi thong tin may -> tra nguyen van chuoi H1V 110 byte
 *   40 FE: chi dan. BE32 do dai anh o byte 14; = 1 thi byte 18 = 1 (khong anh), nguoc lai JPEG 60x60 (FFD8..FFD9);
 *          sau anh: met BE32 + 1 byte do dai nhan + nhan UTF-8.
 *          Nhan "Tốc độ tiếp theo" = bien toc do ke tiep (anh la bien); anh xanh duong = mui ten re (nhan = ten duong);
 *          anh khac = canh bao (camera...). Goi chi co chu cung ten duong = cap nhat met.
 * Anh mui ten -> mat na A8 (giao dien to theo mau chu); bien / canh bao -> RGB565 60x60 hien o o duoi. */
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "rom/tjpgd.h"

#include "board.h"
#include "dashboard.h"
#include "vml_link.h"

static const char *TAG = "vml";
static void log_rec(uint8_t type, uint8_t a, uint8_t b, uint8_t c);

#define IMG 60
#define PKT_MAX 8192
/* VML chi gui goi khi co thay doi (dung den do co the im ca phut) -> giu lau, chi an khi qua lau khong cap nhat */
/* 27/11: dung yen (trong nha) VML khong gui gi them -> 2 phut la an mat chi dan; giu 10 phut */
#define TURN_MS 600000
#define SIGN_MS 300000
#define WARN_MS 20000
#define VML_SHOW_LIMIT 1     /* goi 0101 byte 20 = bien hien tai (27/11: mau 1 khop 50, anh cho chay thu) */

static const char H1V_INFO[] =
    "\xA5\x5A\x37\xC3\x00\x00\x00\x66\x68\x05\x27\xB8\x0E"
    "MODEL:H1V,HW:1.6.4,FW:1.3.6,PROTOCOL:2.2.2, OBDV:H1_2.1.44C,"
    "COMPILE:Feb 27 2021-12:13:56"
    "\x00\xE6\xD4\x26\x3B\x71\x50\x41\x6C";
_Static_assert(sizeof(H1V_INFO) - 1 == 110, "chuoi H1V phai 110 byte");
static const char NEXT_LIMIT[] = "Tốc độ tiếp theo";

static StreamBufferHandle_t s_rx;
static volatile uint32_t s_handle;
static volatile int s_state;                 /* 0 chua len, 1 cho ket noi, 2 da noi, -1 loi */
static volatile bool s_open_evt, s_close_evt, s_auth_fail;
static int64_t s_lost_at;                    /* VML vua ngat: giu man 30 s cho no noi lai (VML hay ngat/noi lai) */
static int s_last_lim = -1, s_last_lim_shown = -1;
#define GRACE_MS 600000                     /* 27/11: ngat lau 10 phut moi xoa (VML ngat/noi lai lien tuc, noi lai khong gui lai) */
static int s_conn_n, s_n0101, s_n40fe;
static volatile int s_drop;
static int64_t s_last_rx;

/* ---------- anh ---------- */
static uint16_t *s_work;                     /* RGB565 60x60 vua giai ma */
static uint8_t *s_nav_px;                    /* A8 */
static uint16_t *s_sign_px, *s_warn_px, *s_lim_px;
static lv_image_dsc_t s_nav_dsc, s_sign_dsc, s_warn_dsc, s_lim_dsc;
#define JD_POOL 8192                 /* ROM tjpgd: 3100 la toi thieu; bang Huffman lon can hon */
static uint8_t *s_jd_pool;

typedef struct { const uint8_t *p; size_t n, pos; } jsrc_t;

static unsigned int jd_in(JDEC *jd, uint8_t *buf, unsigned int n)
{
    jsrc_t *s = (jsrc_t *)jd->device;
    if (s->pos + n > s->n) n = s->n - s->pos;
    if (buf) memcpy(buf, s->p + s->pos, n);
    s->pos += n;
    return n;
}

static unsigned int jd_out(JDEC *jd, void *bmp, JRECT *r)
{
    const uint8_t *px = (const uint8_t *)bmp;           /* ROM tjpgd: RGB888 */
    for (int y = r->top; y <= r->bottom; y++)
        for (int x = r->left; x <= r->right; x++, px += 3)
            if (x < IMG && y < IMG)
                s_work[y * IMG + x] = (uint16_t)(((px[0] & 0xF8) << 8) | ((px[1] & 0xFC) << 3) | (px[2] >> 3));
    return 1;
}

static bool decode(const uint8_t *jpg, size_t n)
{
    if (!s_work || !s_jd_pool) return false;
    JDEC jd;
    jsrc_t src = {jpg, n, 0};
    JRESULT r = jd_prepare(&jd, jd_in, s_jd_pool, JD_POOL, &src);
    if (r != JDR_OK) {
        ESP_LOGW(TAG, "JPEG prepare loi %d", r);
        return false;
    }
    if (jd.width != IMG || jd.height != IMG) ESP_LOGW(TAG, "JPEG %ux%u (khong phai 60x60)", jd.width, jd.height);
    memset(s_work, 0, IMG * IMG * 2);
    r = jd_decomp(&jd, jd_out, 0);
    if (r != JDR_OK) ESP_LOGW(TAG, "JPEG decomp loi %d", r);
    return r == JDR_OK;
}

/* Mui ten re la mau xanh duong (anh that cua VML); dem diem xanh / do nhu ban test V5 */
static bool is_arrow(void)
{
    unsigned blue = 0, red = 0;
    for (int i = 0; i < IMG * IMG; i++) {
        uint16_t c = s_work[i];
        int r = ((c >> 11) & 31) * 2, g = (c >> 5) & 63, b = (c & 31) * 2;
        if (b > 30 && b > r + 15 && b > g + 4) ++blue;
        if (r > 30 && r > b + 15 && r > g + 15) ++red;
    }
    return blue >= 60 && red < 30;
}

static void to_mask(void)
{
    for (int i = 0; i < IMG * IMG; i++) {
        uint16_t c = s_work[i];
        int r = ((c >> 11) & 31) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
        int a = b - (r > g ? r : g);                     /* do "xanh duong": nen den / trang deu ra 0 */
        if (a < 0) a = 0;
        a = a * 2;
        s_nav_px[i] = (uint8_t)(a > 255 ? 255 : a);
    }
}

/* Bien tron: RGB565 + mat na A8 hinh tron (bo goc nen cua anh JPEG) */
static void round_mask(uint16_t *px)
{
    uint8_t *a = (uint8_t *)(px + IMG * IMG);
    for (int y = 0; y < IMG; y++)
        for (int x = 0; x < IMG; x++) {
            float dx = x + 0.5f - IMG / 2.0f, dy = y + 0.5f - IMG / 2.0f;
            float d = IMG / 2.0f - __builtin_sqrtf(dx * dx + dy * dy);   /* > 0: trong vong tron */
            a[y * IMG + x] = d >= 1 ? 255 : d <= 0 ? 0 : (uint8_t)(d * 255);
        }
}

static void dsc_init(lv_image_dsc_t *d, void *px, lv_color_format_t cf, int bpp)
{
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = cf;
    d->header.w = IMG;
    d->header.h = IMG;
    d->header.stride = IMG * (cf == LV_COLOR_FORMAT_RGB565A8 ? 2 : bpp);
    d->data_size = IMG * IMG * bpp;
    d->data = px;
}

/* ---------- trang thai chi dan ---------- */
static bool s_turn, s_sign, s_warn, s_lim, s_data;
static int s_turn_code = 3;
static uint32_t s_turn_m, s_sign_m, s_warn_m, s_sign_min;
static int64_t s_turn_at, s_sign_at, s_warn_at, s_cur_at;
static bool s_cur;                           /* dang co goi bien hien tai (0101): giu man chay ca khi khong dan duong */
static char s_street[96], s_last_text[96];

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void push_alerts(void)                /* goi trong khoa UI */
{
    ui_alert_t a;
    if (s_warn) {
        a = (ui_alert_t){3, (int)s_warn_m, 0};
        ui_set_alert_icon(&s_warn_dsc);
        ui_set_alerts(&a, 1);
    } else if (s_sign) {
        a = (ui_alert_t){8, (int)s_sign_m, -1};
        ui_set_alert_icon(&s_sign_dsc);
        ui_set_alerts(&a, 1);
    } else {
        ui_set_alerts(NULL, 0);
        ui_set_alert_icon(NULL);
    }
}

static void push_live(void)
{
    bool live = s_turn || s_sign || s_warn || s_cur;
    if (live && !s_data) {
        s_data = true;
        ui_set_signal(true);
        ui_set_waze(true);
        ui_set_speed(-1);
    }
    (void)live;
    ui_set_keep_on(s_handle != 0 || s_lost_at != 0);                 /* app dang noi: luon o man chay (bien hien tai / cho chi dan) */
}

/* Bien vua qua (con <= 60 m roi het) thanh bien hien tai, thay vong so ben trai */
static void promote_sign(void)
{
    if (!s_sign || s_sign_min > 60 || !s_lim_px) return;
    memcpy(s_lim_px, s_sign_px, IMG * IMG * 2);
    s_lim = true;
    ui_set_limit_icon(&s_lim_dsc);
}

static void hexlog(const char *tag, const uint8_t *d, size_t n)
{
    char line[3 * 32 + 1];
    ESP_LOGI(TAG, "%s %u byte", tag, (unsigned)n);
    for (size_t i = 0; i < n; i += 32) {
        size_t k = n - i < 32 ? n - i : 32, o = 0;
        for (size_t j = 0; j < k; j++) o += snprintf(line + o, sizeof line - o, "%02X ", d[i + j]);
        ESP_LOGI(TAG, "  %s", line);
    }
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void on_guidance(const uint8_t *p, size_t size)
{
    if (size < 32) return;
    size_t end = size - 8, meta = 19;
    uint32_t len = be32(p + 14);
    const uint8_t *jpg = NULL;
    if (len != 1) {
        if (len < 4 || len > end - 18) return;
        meta = 18 + len;
        if (p[18] != 0xFF || p[19] != 0xD8 || p[meta - 2] != 0xFF || p[meta - 1] != 0xD9) return;
        jpg = p + 18;
    } else if (p[18] != 1) {
        return;
    }
    if (end - meta < 5) return;
    uint32_t m = be32(p + meta);
    size_t ln = p[meta + 4];
    if (m == UINT32_MAX || !ln || ln > end - meta - 5) return;
    char label[96];
    size_t cl = ln < sizeof label - 1 ? ln : sizeof label - 1;
    memcpy(label, p + meta + 5, cl);
    label[cl] = 0;
    bool next_limit = ln == sizeof NEXT_LIMIT - 1 && memcmp(label, NEXT_LIMIT, ln) == 0;
    bool ok_img = jpg && decode(jpg, len);
    if (jpg && !ok_img) ESP_LOGW(TAG, "anh loi (%u byte)", (unsigned)len);
    int64_t t = now_ms();

    board_ui_lock();
    if (next_limit) {
        ESP_LOGI(TAG, "BIEN tiep %u m", (unsigned)m);
        log_rec(5, 2, (uint8_t)((m / 10) >> 8), (uint8_t)(m / 10));
        if (ok_img) memcpy(s_sign_px, s_work, IMG * IMG * 2);
        if (!s_sign || m > s_sign_m + 200) s_sign_min = m;     /* bien moi */
        if (m < s_sign_min) s_sign_min = m;
        if (ok_img || s_sign) {
            s_sign = true;
            s_sign_m = m;
            s_sign_at = t;
        }
    } else if (ok_img && is_arrow()) {
        ESP_LOGI(TAG, "RE %u m | %s", (unsigned)m, label);
        log_rec(5, 1, (uint8_t)((m / 10) >> 8), (uint8_t)(m / 10));
        to_mask();
        snprintf(s_street, sizeof s_street, "%s", label);
        s_turn = true;
        s_turn_m = m;
        s_turn_at = t;
        s_turn_code = 3;
        ui_set_nav_icon(&s_nav_dsc);
        ui_set_nav(true, 3, (int)m, 0, s_street, 0);
    } else if (ok_img) {
        ESP_LOGI(TAG, "CANH BAO %u m | %s", (unsigned)m, label);
        log_rec(5, 3, (uint8_t)((m / 10) >> 8), (uint8_t)(m / 10));
        hexlog("anh canh bao", p, size > 512 ? 512 : size);
        memcpy(s_warn_px, s_work, IMG * IMG * 2);
        s_warn = true;
        s_warn_m = m;
        s_warn_at = t;
        if (strcmp(label, s_last_text)) {
            snprintf(s_last_text, sizeof s_last_text, "%s", label);
            ui_toast(label);
        }
    } else if (jpg && !ok_img) {                              /* anh hong (khong giai duoc): van hien re + ten duong */
        ESP_LOGI(TAG, "RE (anh loi) %u m | %s", (unsigned)m, label);
        hexlog("anh loi", p, size > 256 ? 256 : size);
        snprintf(s_street, sizeof s_street, "%s", label);
        s_turn = true;
        s_turn_m = m;
        s_turn_at = t;
        ui_set_nav_icon(NULL);
        s_turn_code = 1;
        ui_set_nav(true, 1, (int)m, 0, s_street, 0);
    } else if (s_turn && !strcmp(label, s_street)) {          /* chi co chu, cung duong: cap nhat met */
        s_turn_m = m;
        s_turn_at = t;
        ui_set_nav(true, s_turn_code, (int)m, 0, s_street, 0);
    } else {
        ESP_LOGI(TAG, "CHU %u m | %s", (unsigned)m, label);
        if (strcmp(label, s_last_text)) {
            snprintf(s_last_text, sizeof s_last_text, "%s", label);
            ui_toast(label);
        }
    }
    push_alerts();
    push_live();
    board_ui_unlock();
}

static void expire(void)
{
    int64_t t = now_ms();
    bool ch = false;
    if (!(s_turn && t - s_turn_at > TURN_MS) && !(s_sign && t - s_sign_at > SIGN_MS) && !(s_warn && t - s_warn_at > WARN_MS)
        && !(s_turn && s_turn_m <= 30 && t - s_turn_at > 15000) && !(s_sign && s_sign_m <= 30 && t - s_sign_at > 8000))
        return;
    board_ui_lock();
    if (s_turn && t - s_turn_at > TURN_MS) {
        s_turn = false;
        ui_set_nav(false, 0, -1, 0, s_street, 0);
        ui_set_nav_icon(NULL);
        ui_toast("Ẩn chỉ dẫn: 10 phút không cập nhật");
        ch = true;
    }
    if (s_turn && s_turn_m <= 30 && t - s_turn_at > 15000) {     /* v9: da qua cho re (con <=30 m, 15 s khong cap nhat) */
        s_turn = false;
        ui_set_nav(false, 0, -1, 0, s_street, 0);
        ui_set_nav_icon(NULL);
        ch = true;
    }
    if (s_sign && s_sign_m <= 30 && t - s_sign_at > 8000) {      /* v9: da qua bien (VML khong bao "da qua"): thanh bien hien tai */
        promote_sign();
        s_sign = false;
        ch = true;
    }
    if (s_sign && t - s_sign_at > SIGN_MS) {
        promote_sign();
        s_sign = false;
        ch = true;
    }
    if (s_warn && t - s_warn_at > WARN_MS) {
        s_warn = false;
        ch = true;
    }
    if (ch) {
        push_alerts();
        push_live();
    }
    board_ui_unlock();
}

static void reset_all(void)
{
    board_ui_lock();
    s_turn = s_sign = s_warn = s_lim = s_data = s_cur = false;
    s_last_text[0] = 0;
    ui_toast("Mất VML quá 10 phút: xoá dữ liệu");
    ui_set_nav_icon(NULL);
    ui_set_alert_icon(NULL);
    ui_set_limit_icon(NULL);
    ui_set_keep_on(false);
    ui_set_signal(false);
    board_ui_unlock();
}

/* ---------- log tu dong vao flash (ban thu) ----------
 * Ban VML khong dung OTA qua BLE -> khe ota_1 trong. Ghi ban ghi 8 byte vao 64 KB dau khe ota_1:
 *   [loai][giay 24 bit][d0 d1 d2][0]   loai: 1 goi 0101 ngan (3 byte), 2 goi 0101 29 byte (byte 14,15,20),
 *   3 noi VML, 4 ngat, 5 chi dan (d0: 1 re / 2 bien tiep / 3 canh bao, d1d2 = met/10), 0x10 bat HUD (d0d1 = so lan).
 * Lan bat sau: hien tom tat len man (ui_info) de chup; day 3/4 thi xoa sau khi hien. */
#define LOG_SIZE 0x10000
static const esp_partition_t *s_log_part;
static uint32_t s_log_off;

static void log_rec(uint8_t type, uint8_t a, uint8_t b, uint8_t c)
{
    if (!s_log_part || s_log_off + 8 > LOG_SIZE) return;
    uint32_t t = (uint32_t)(esp_timer_get_time() / 1000000);
    uint8_t r[8] = {type, (uint8_t)(t >> 16), (uint8_t)(t >> 8), (uint8_t)t, a, b, c, 0};
    if (esp_partition_write(s_log_part, s_log_off, r, 8) == ESP_OK) s_log_off += 8;
}

static void log_start(void)
{
    /* v9: khe KHONG dang chay (truoc chi dung ota_1: neu firmware nap vao ota_1 thi log tat ma khong bao) */
    const esp_partition_t *run = esp_ota_get_running_partition();
    s_log_part = esp_ota_get_next_update_partition(NULL);
    if (!s_log_part || s_log_part == run) {
        s_log_part = NULL;
        board_ui_lock();
        ui_info("LOG VML: không tìm được vùng ghi log.\nChạm để tắt", 10000);
        board_ui_unlock();
        return;
    }
    uint8_t r[8];
    uint32_t off = 0, boots = 0, n25 = 0, n29 = 0, nc = 0, nd = 0, ng = 0, nbad = 0;
    int mn[3] = {256, 256, 256}, mx[3] = {-1, -1, -1};
    uint8_t last25[12][3], lim[10];
    int k25 = 0, kl = 0, lastlim = -1;
    bool seen[256] = {0};
    int distinct = 0;
    for (; off + 8 <= LOG_SIZE; off += 8) {
        if (esp_partition_read(s_log_part, off, r, 8) != ESP_OK) break;
        if (r[0] == 0xFF) break;
        switch (r[0]) {
        case 1:
            ++n25;
            for (int i = 0; i < 3; i++) { if (r[4 + i] < mn[i]) mn[i] = r[4 + i]; if (r[4 + i] > mx[i]) mx[i] = r[4 + i]; }
            if (!seen[r[6]]) { seen[r[6]] = true; ++distinct; }
            memcpy(last25[k25 % 12], r + 4, 3); ++k25;
            break;
        case 2:
            ++n29;
            if (r[6] != lastlim && kl < 10) { lim[kl++] = r[6]; lastlim = r[6]; }
            break;
        case 3: ++nc; break;
        case 4: ++nd; break;
        case 5: ++ng; break;
        case 0x10: ++boots; break;
        default: ++nbad; break;
        }
        if (nbad) break;
    }
    s_log_off = off;
    if (nbad) {                                        /* khe ota_1 con firmware cu / rac: xoa, bat dau log moi */
        ESP_LOGW(TAG, "log: vung ota_1 khong phai log (%u ban ghi la) -> xoa", (unsigned)nbad);
        esp_partition_erase_range(s_log_part, 0, LOG_SIZE);
        s_log_off = 0;
        log_rec(0x10, 0, 0, 0);
        board_ui_lock();
        ui_info("LOG VML: lần đầu, đã chuẩn bị vùng ghi log.\nAnh chạy xe có VML, lần bật sau sẽ hiện bảng.\nChạm để tắt", 10000);
        board_ui_unlock();
        return;
    }
    if (off && (n25 || n29 || ng)) {
        char *t = malloc(1100);
        if (t) {
            int o = snprintf(t, 1100, "LOG VML (%u bản ghi, %u lần bật). Chạm để tắt\n", (unsigned)(off / 8), (unsigned)boots);
            if (n25) {
                o += snprintf(t + o, 1100 - o, "Gói ngắn: %u lần đổi, %d giá trị cuối khác nhau\n b1 %02X..%02X  b2 %02X..%02X  b3 %d..%d\n Cuối:",
                              (unsigned)n25, distinct, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);
                int s0 = k25 > 12 ? k25 - 12 : 0;
                for (int i = s0; i < k25 && o < 1000; i++) o += snprintf(t + o, 1100 - o, " %d", last25[i % 12][2]);
                o += snprintf(t + o, 1100 - o, "\n");
            }
            o += snprintf(t + o, 1100 - o, "Gói biển 29B: %u lần, biển:", (unsigned)n29);
            for (int i = 0; i < kl && o < 1040; i++) o += snprintf(t + o, 1100 - o, " %d", lim[i]);
            snprintf(t + o, 1100 - o, "\nChỉ dẫn: %u  Nối: %u  Ngắt: %u%s", (unsigned)ng, (unsigned)nc, (unsigned)nd,
                     off > LOG_SIZE * 3 / 4 ? "\nLog gần đầy: đã xoá để ghi mới" : "");
            ESP_LOGI(TAG, "%s", t);
            board_ui_lock();
            ui_info(t, 30000);
            board_ui_unlock();
            free(t);
        }
    } else {
        char t[200];
        snprintf(t, sizeof t, "LOG VML: chưa có dữ liệu (%u lần bật, nối %u, ngắt %u).\nVML chưa gửi biển/chỉ dẫn nào.\nChạm để tắt",
                 (unsigned)boots, (unsigned)nc, (unsigned)nd);
        board_ui_lock();
        ui_info(t, 10000);
        board_ui_unlock();
    }
    if (off > LOG_SIZE * 3 / 4) {
        esp_partition_erase_range(s_log_part, 0, LOG_SIZE);
        s_log_off = 0;
    }
    log_rec(0x10, (uint8_t)(boots >> 8), (uint8_t)boots, 0);
}

/* ---------- ghep khung ---------- */
static const uint8_t MAGIC[4] = {0xA5, 0x5A, 0x37, 0xC3};
static uint8_t *s_pkt;
static size_t s_used, s_expect;

static void on_frame(const uint8_t *p, size_t n)
{
    if (n == 22 && p[12] == 0x0E && p[13] == 0x00) {
        if (s_handle) esp_spp_write(s_handle, sizeof H1V_INFO - 1, (uint8_t *)H1V_INFO);
        ESP_LOGI(TAG, "app hoi thong tin -> tra loi H1V");
    } else if (n >= 23 && n <= 64 && p[12] == 0x01 && p[13] == 0x01) {   /* bien hien tai: byte cuoi truoc 8 byte duoi (29 byte -> byte 20) */
        ++s_n0101;
        {
            static uint8_t l25[3], l29[3];
            static bool h25, h29;
            if (n == 25 && (!h25 || memcmp(l25, p + 14, 3))) { memcpy(l25, p + 14, 3); h25 = true; log_rec(1, p[14], p[15], p[16]); }
            if (n == 29) {
                uint8_t v[3] = {p[14], p[15], p[20]};
                if (!h29 || memcmp(l29, v, 3)) { memcpy(l29, v, 3); h29 = true; log_rec(2, v[0], v[1], v[2]); }
            }
        }
        {                                              /* 27/11: con co loai 25 byte (trong nha) -> ban thu: hien hex de xac minh */
            static uint8_t last[64];
            static size_t last_n;
            if (n != 29 && n != 25 && (n != last_n || memcmp(last, p, n))) {
                memcpy(last, p, n);
                last_n = n;
                char t[96];
                int o = snprintf(t, sizeof t, "0101 (%u):", (unsigned)n);
                for (size_t i = 14; i < n - 8 && o < (int)sizeof t - 4; i++) o += snprintf(t + o, sizeof t - o, " %02X", p[i]);
                board_ui_lock();
                ui_toast(t);
                board_ui_unlock();
            }
        }
        s_cur_at = now_ms();
        if (!s_cur) {
            s_cur = true;
            board_ui_lock();
            push_live();
            board_ui_unlock();
        }
        if (n == 29 && p[20] != s_last_lim) {           /* 27/11: loai 25 byte (86 A0 xx) chua ro nghia (toc do?) -> chi hien hex, khong dung lam bien */
            s_last_lim = p[20];
            int last = s_last_lim;
            ESP_LOGI(TAG, "VML LIMIT CANDIDATE: %d", last);
#if VML_SHOW_LIMIT
            board_ui_lock();
            if (last >= 5 && last <= 150 && last % 5 == 0) {
                s_last_lim_shown = last;
                ui_set_speed_limit(last);              /* bien hien tai */
            } else {                                   /* 27/11: VML gui 0 (mat GPS trong nha?): giu bien cu, bao de biet */
                char t[48];
                snprintf(t, sizeof t, "VML gửi biển %d, giữ biển cũ", last);
                ui_toast(t);
            }
            board_ui_unlock();
#endif
        }
    } else if (n >= 14 && p[12] == 0x40 && p[13] == 0xFE) {
        ++s_n40fe;
        on_guidance(p, n);
    } else {
        hexlog("goi la", p, n > 256 ? 256 : n);
        static int last_cmd = -1;                      /* ban thu: bao goi la (co the la lenh xoa chi dan) */
        int cmd = n > 13 ? p[12] << 8 | p[13] : -2;
        if (cmd != last_cmd) {
            last_cmd = cmd;
            char t[48];
            snprintf(t, sizeof t, "VML gói lạ %04X, %u byte", cmd & 0xFFFF, (unsigned)n);
            board_ui_lock();
            ui_toast(t);
            board_ui_unlock();
        }
    }
}

static void feed(uint8_t v)
{
    if (s_used < 4 && v != MAGIC[s_used]) {
        s_used = v == MAGIC[0] ? 1 : 0;
        if (s_used) s_pkt[0] = v;
        return;
    }
    s_pkt[s_used++] = v;
    if (s_used == 8) {
        uint32_t len = be32(s_pkt + 4);
        if (len < 6 || len > PKT_MAX - 8) {
            s_used = s_expect = 0;
            return;
        }
        s_expect = len + 8;
    }
    if (!s_expect || s_used != s_expect) return;
    on_frame(s_pkt, s_used);
    s_used = s_expect = 0;
}

static void vml_task(void *arg)
{
    uint8_t buf[256];
    for (;;) {
        size_t n = xStreamBufferReceive(s_rx, buf, sizeof buf, pdMS_TO_TICKS(250));
        if (n) {
            if (s_used && now_ms() - s_last_rx > 1000) s_used = s_expect = 0;   /* khung cu bi dut: bo, bat dau lai (nhu ban test V5) */
            s_last_rx = now_ms();
        }
        if (s_open_evt) {
            s_open_evt = false;
            log_rec(3, 0, 0, 0);
            s_used = s_expect = 0;
            s_lost_at = 0;
            s_last_lim = -1;                       /* noi lai: neu VML gui lai bien hien tai thi ve lai (khong xoa bien cu) */
            board_ui_lock();
            ui_set_phone(true);
            if (++s_conn_n > 1) {                  /* ban thu: bao so goi da nhan de biet VML noi lai co gui lai khong */
                char t[96];
                snprintf(t, sizeof t, "Nối lại %d, biển %d, gói %d/%d%s", s_conn_n - 1,
                         s_last_lim_shown, s_n0101, s_n40fe, s_drop ? ", mất" : "");
                ui_toast(t);
            }
            {
                static bool told;                  /* lan noi dau sau khi HUD vua sap: bao lai ly do (luc khoi dong de bi lo) */
                esp_reset_reason_t rr = esp_reset_reason();
                if (!told && (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT)) {
                    told = true;
                    ui_toast(rr == ESP_RST_BROWNOUT ? "HUD vừa khởi động lại do sụt nguồn" : "HUD vừa khởi động lại do lỗi");
                }
            }
            s_data = true;                         /* VML noi la len man chay ngay, khong doi goi dau tien */
            ui_set_signal(true);
            ui_set_waze(true);
            ui_set_speed(-1);
            ui_set_keep_on(true);
            board_ui_unlock();
        }
        if (s_close_evt) {                         /* chua xoa: VML thuong noi lai ngay, chi dan cu van dung */
            s_close_evt = false;
            log_rec(4, 0, 0, 0);
            s_lost_at = now_ms();
            board_ui_lock();
            ui_set_phone(false);
            board_ui_unlock();
        }
        if (s_lost_at && now_ms() - s_lost_at > GRACE_MS) {
            s_lost_at = 0;
            reset_all();
        }
        if (s_auth_fail) {
            s_auth_fail = false;
            board_ui_lock();
            ui_toast("Ghép đôi lỗi: vào Bluetooth điện thoại, Bỏ ghép đôi HUD rồi ghép lại");
            board_ui_unlock();
        }
        for (size_t i = 0; i < n; i++) feed(buf[i]);
        expire();
    }
}

/* ---------- Bluetooth ---------- */
static void gap_cb(esp_bt_gap_cb_event_t ev, esp_bt_gap_cb_param_t *p)
{
    switch (ev) {
    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = {'1', '2', '3', '4'};
        esp_bt_gap_pin_reply(p->pin_req.bda, true, 4, pin);
        break;
    }
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (p->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "ghep doi xong: %s", (const char *)p->auth_cmpl.device_name);
        } else {
            /* Dien thoai con khoa ghep doi cu (ban test Arduino / nap lai) ma HUD khong co -> noi roi ngat lien tuc.
             * Xoa khoa ben HUD va bao nguoi dung xoa ghep doi tren dien thoai. */
            ESP_LOGW(TAG, "ghep doi loi (ma %d): xoa khoa cu", p->auth_cmpl.stat);
            esp_bt_gap_remove_bond_device(p->auth_cmpl.bda);
            s_auth_fail = true;
        }
        break;
    default:
        break;
    }
}

static void spp_cb(esp_spp_cb_event_t ev, esp_spp_cb_param_t *p)
{
    switch (ev) {
    case ESP_SPP_INIT_EVT:
        esp_bt_gap_set_device_name(CONFIG_EVCARHUD_VML_NAME);
        esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "SPP_SERVER");
        break;
    case ESP_SPP_START_EVT:
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        s_state = 1;
        ESP_LOGI(TAG, "dang cho VietMap Live (ten \"%s\")", CONFIG_EVCARHUD_VML_NAME);
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        s_handle = p->srv_open.handle;
        s_state = 2;
        s_open_evt = true;
        xStreamBufferReset(s_rx);
        ESP_LOGI(TAG, "VietMap Live da noi");
        break;
    case ESP_SPP_CLOSE_EVT:
        s_handle = 0;
        s_state = 1;
        s_close_evt = true;
        ESP_LOGI(TAG, "VietMap Live ngat (status %d)", p->close.status);
        break;
    case ESP_SPP_DATA_IND_EVT:
        if (xStreamBufferSend(s_rx, p->data_ind.data, p->data_ind.len, pdMS_TO_TICKS(30)) != p->data_ind.len) {
            ++s_drop;
            ESP_LOGW(TAG, "day bo dem, mat %u byte", p->data_ind.len);
        }
        break;
    default:
        break;
    }
}

int vml_link_state(void) { return s_state; }

void vml_link_start(void)
{
    s_pkt = heap_caps_malloc(PKT_MAX, MALLOC_CAP_8BIT);
    s_jd_pool = heap_caps_malloc(JD_POOL, MALLOC_CAP_8BIT);
    s_work = heap_caps_malloc(IMG * IMG * 2, MALLOC_CAP_8BIT);
    s_nav_px = heap_caps_malloc(IMG * IMG, MALLOC_CAP_8BIT);
    s_sign_px = heap_caps_calloc(1, IMG * IMG * 3, MALLOC_CAP_8BIT);
    s_warn_px = heap_caps_calloc(1, IMG * IMG * 2, MALLOC_CAP_8BIT);
    s_lim_px = heap_caps_calloc(1, IMG * IMG * 3, MALLOC_CAP_8BIT);
    s_rx = xStreamBufferCreate(16 * 1024, 1);
    if (!s_pkt || !s_work || !s_nav_px || !s_sign_px || !s_warn_px || !s_rx) {
        ESP_LOGE(TAG, "thieu RAM");
        s_state = -1;
        return;
    }
    dsc_init(&s_nav_dsc, s_nav_px, LV_COLOR_FORMAT_A8, 1);
    round_mask(s_sign_px);
    dsc_init(&s_sign_dsc, s_sign_px, LV_COLOR_FORMAT_RGB565A8, 3);
    dsc_init(&s_warn_dsc, s_warn_px, LV_COLOR_FORMAT_RGB565, 2);
    if (s_lim_px) {
        round_mask(s_lim_px);
        dsc_init(&s_lim_dsc, s_lim_px, LV_COLOR_FORMAT_RGB565A8, 3);
    }

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_bt_controller_config_t cc = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_bt_controller_init(&cc);
    if (e == ESP_OK) e = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    esp_bluedroid_config_t bc = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bc.ssp_en = true;
    if (e == ESP_OK) e = esp_bluedroid_init_with_cfg(&bc);
    if (e == ESP_OK) e = esp_bluedroid_enable();
    if (e == ESP_OK) e = esp_bt_gap_register_callback(gap_cb);
    if (e == ESP_OK) e = esp_spp_register_callback(spp_cb);
    esp_spp_cfg_t sc = {.mode = ESP_SPP_MODE_CB, .enable_l2cap_ertm = true, .tx_buffer_size = 0};
    if (e == ESP_OK) e = esp_spp_enhanced_init(&sc);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "khoi dong Bluetooth loi: %s", esp_err_to_name(e));
        s_state = -1;
        return;
    }
    esp_bt_io_cap_t io = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io, sizeof io);
    log_start();
    xTaskCreate(vml_task, "vml", 8192, NULL, 4, NULL);
    ESP_LOGI(TAG, "SPP len, RAM trong con %u byte", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
