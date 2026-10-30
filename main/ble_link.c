#include "sdkconfig.h"
#include "ble_link.h"

#if CONFIG_BT_NIMBLE_ENABLED

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "link.h"

/* HLP/1 qua BLE GATT (tai lieu Waze Mod, muc 8):
 *   dich vu  8a7e0001-4d6e-4c48-9a9d-484c504c0001
 *   TX       8a7e0002-...  dien thoai -> HUD, ghi co xac nhan, chia khuc tuy y (ghep lai theo LF)
 *   RX       8a7e0003-...  HUD -> dien thoai, notify, moi khuc toi da MTU - 3 byte
 *   CAPS     8a7e0004-...  doc: kha nang cua HUD
 * NimBLE luu UUID 128 bit theo thu tu byte dao nguoc. */
#define HLP_UUID(n) BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a, 0x48, 0x4c, 0x6e, 0x4d, (n), 0x00, 0x7e, 0x8a)
static const ble_uuid128_t UUID_SVC = HLP_UUID(0x01);
static const ble_uuid128_t UUID_TX = HLP_UUID(0x02);
static const ble_uuid128_t UUID_RX = HLP_UUID(0x03);
static const ble_uuid128_t UUID_CAPS = HLP_UUID(0x04);
static const char CAPS[] = "{\"v\":1,\"caps\":{\"transport\":\"ble\",\"maxFrame\":512}}";

#define START_DELAY_MS 1000              /* de man hinh len truoc */
#define LINE_MAX 640                     /* 1 dong HLP toi da 512 byte */
#define EV_RX   (1u << 0)
#define EV_TX   (1u << 1)
#define EV_UP   (1u << 2)
#define EV_DOWN (1u << 3)

static const char *TAG = "ble";
static TaskHandle_t s_task;
static StreamBufferHandle_t s_rx_sb;     /* byte tu dien thoai: ham GATT chi chep vao day roi tra ve ngay */
static MessageBufferHandle_t s_tx_mb;    /* cac dong cho gui len dien thoai */
static uint16_t s_rx_handle;             /* handle gia tri cua dac tinh RX (notify) */
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile int s_state;
static uint8_t s_addr_type;
static bool s_linked;                    /* da goi link_up (chi task ble dung) */
static volatile bool s_synced;           /* ngan xep da noi chuyen duoc voi bo dieu khien Bluetooth */
static int s_resets;

static int gap_event(struct ble_gap_event *ev, void *arg);

static void wake(uint32_t bits)
{
    if (s_task) xTaskNotify(s_task, bits, eSetBits);
}

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    const ble_uuid_t *u = ctxt->chr->uuid;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && ble_uuid_cmp(u, &UUID_TX.u) == 0) {
        uint8_t buf[256];
        uint16_t n = 0;
        if (OS_MBUF_PKTLEN(ctxt->om) > sizeof buf) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof buf, &n) != 0) return BLE_ATT_ERR_UNLIKELY;
        if (n && xStreamBufferSend(s_rx_sb, buf, n, 0) != n) ESP_LOGW(TAG, "bo dem nhan day, mat %u byte", (unsigned)n);
        wake(EV_RX);
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && ble_uuid_cmp(u, &UUID_CAPS.u) == 0) {
        return os_mbuf_append(ctxt->om, CAPS, sizeof CAPS - 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = &UUID_TX.u, .access_cb = chr_access, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
            {.uuid = &UUID_RX.u, .access_cb = chr_access, .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_rx_handle},
            {.uuid = &UUID_CAPS.u, .access_cb = chr_access, .flags = BLE_GATT_CHR_F_READ},
            {0},
        },
    },
    {0},
};

static void advertise(void)
{
    const char *name = ble_svc_gap_device_name();
    struct ble_hs_adv_fields f;
    memset(&f, 0, sizeof f);
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = (ble_uuid128_t *)&UUID_SVC;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    f.name = (const uint8_t *)name;
    f.name_len = (uint8_t)strlen(name);
    f.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);                          /* co + ten + UUID dich vu: vua 31 byte khi ten <= 8 ky tu */
    if (rc != 0) {                                                /* ten dai: UUID o goi quang ba, ten sang goi tra loi quet */
        struct ble_hs_adv_fields r;
        memset(&r, 0, sizeof r);
        r.name = f.name;
        r.name_len = f.name_len;
        r.name_is_complete = 1;
        f.name = NULL;
        f.name_len = 0;
        f.name_is_complete = 0;
        rc = ble_gap_adv_set_fields(&f);
        if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&r);
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "khong dat duoc du lieu quang ba (rc=%d)", rc);
        return;
    }
    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof p);
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "khong bat dau quang ba duoc (rc=%d)", rc);
        return;
    }
    if (s_state != 2) s_state = 1;
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            s_conn = ev->connect.conn_handle;
            ESP_LOGI(TAG, "dien thoai ket noi Bluetooth: cho dien thoai doc dich vu HLP va bat thong bao RX");
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "dien thoai ngat Bluetooth (ly do %d)", ev->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_state = 1;
        wake(EV_DOWN);
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_rx_handle) {           /* dien thoai bat/tat nhan thong bao tren RX */
            if (ev->subscribe.cur_notify) {
                s_state = 2;
                wake(EV_UP);
            } else {
                wake(EV_DOWN);
            }
        }
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %d", ev->mtu.value);
        break;
    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    s_synced = true;
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &s_addr_type) != 0) {
        ESP_LOGW(TAG, "khong co dia chi Bluetooth");
        return;
    }
    uint8_t a[6] = {0};
    ble_hs_id_copy_addr(s_addr_type, a, NULL);
    ESP_LOGI(TAG, "dang quang ba \"%s\" (%02X:%02X:%02X:%02X:%02X:%02X, dich vu HLP RX handle %u): cho Waze Mod ket noi",
             ble_svc_gap_device_name(), a[5], a[4], a[3], a[2], a[1], a[0], (unsigned)s_rx_handle);
    advertise();
}

static void on_reset(int reason)
{
    if (++s_resets <= 3) ESP_LOGW(TAG, "ngan xep Bluetooth dat lai (ly do %d)", reason);   /* lap lai mai thi chi bao vai lan */
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();                                            /* chi tra ve khi nimble_port_stop() */
    nimble_port_freertos_deinit();
}

static void ble_tx(const char *line)                              /* link.c goi (dang giu khoa LVGL): chi xep hang */
{
    size_t n = strlen(line);
    if (!s_tx_mb || n == 0 || n >= LINE_MAX) return;
    if (xMessageBufferSend(s_tx_mb, line, n, 0) == n) wake(EV_TX);
}

static void send_line(char *line, size_t n)                       /* line co cho cho them LF */
{
    uint16_t conn = s_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE) return;
    line[n++] = '\n';
    uint16_t mtu = ble_att_mtu(conn);
    size_t chunk = mtu > 23 ? (size_t)mtu - 3 : 20;
    for (size_t off = 0; off < n; off += chunk) {
        size_t len = n - off < chunk ? n - off : chunk;
        int rc = -1;
        for (int k = 0; k < 4 && rc != 0; k++) {                  /* het bo dem tam thoi: thu lai vai lan */
            struct os_mbuf *om = ble_hs_mbuf_from_flat(line + off, (uint16_t)len);
            rc = om ? ble_gatts_notify_custom(conn, s_rx_handle, om) : BLE_HS_ENOMEM;
            if (rc != 0) vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (rc != 0) {
            ESP_LOGW(TAG, "gui thong bao that bai (rc=%d)", rc);
            return;
        }
    }
}

static bool bt_up(void)
{
    /* ESP-IDF 5.x: nimble_port_init() tu bat bo dieu khien Bluetooth cua ESP32 */
    esp_err_t e;
    e = nimble_port_init();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "nimble_port_init: %s", esp_err_to_name(e));
        return false;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(s_svcs);
    if (rc == 0) rc = ble_svc_gap_device_name_set(CONFIG_EVCARHUD_BLE_NAME);
    if (rc != 0) {
        ESP_LOGW(TAG, "dang ky dich vu GATT that bai (rc=%d)", rc);
        return false;
    }
    nimble_port_freertos_init(host_task);
    for (int i = 0; i < 50 && !s_synced; i++) vTaskDelay(pdMS_TO_TICKS(100));
    if (!s_synced) {
        ESP_LOGW(TAG, "bo dieu khien Bluetooth khong tra loi: bo qua Bluetooth");
        if (nimble_port_stop() == 0) nimble_port_deinit();
        return false;
    }
    return true;
}

static void ble_task(void *arg)
{
    (void)arg;
    static char line[LINE_MAX + 2];
    static uint8_t rx[256];
    vTaskDelay(pdMS_TO_TICKS(START_DELAY_MS));
    if (!bt_up()) {
        s_state = -1;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        uint32_t ev = 0;
        xTaskNotifyWait(0, UINT32_MAX, &ev, pdMS_TO_TICKS(1000));
        if ((ev & EV_DOWN) && s_linked) {
            s_linked = false;
            link_down(LINK_BLE);
            xStreamBufferReset(s_rx_sb);
            xMessageBufferReset(s_tx_mb);
        }
        if ((ev & EV_UP) && !s_linked && s_conn != BLE_HS_CONN_HANDLE_NONE) {
            s_linked = true;
            link_up(LINK_BLE, ble_tx);                            /* HUD gui `dev` truoc (qua ble_tx -> hang doi) */
        }
        size_t n;
        while ((n = xStreamBufferReceive(s_rx_sb, rx, sizeof rx, 0)) > 0) {
            if (s_linked) link_rx(LINK_BLE, rx, n, false);        /* ghep dong theo LF nam trong bo giai ma */
        }
        while ((n = xMessageBufferReceive(s_tx_mb, line, LINE_MAX, 0)) > 0) send_line(line, n);
    }
}

void ble_link_start(void)
{
    if (s_task) return;
    s_rx_sb = xStreamBufferCreate(2048, 1);
    s_tx_mb = xMessageBufferCreate(2048);
    if (!s_rx_sb || !s_tx_mb) return;
    if (xTaskCreate(ble_task, "ble_link", 5120, NULL, 4, &s_task) != pdPASS) s_task = NULL;
}

int ble_link_state(void) { return s_state; }

#else  /* Bluetooth tat trong menuconfig */

void ble_link_start(void) {}
int ble_link_state(void) { return 0; }

#endif
