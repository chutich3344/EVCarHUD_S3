/* Noi HLP/1 (Waze HUD Link) vao giao dien: nhan byte tu transport (WebSocket/BLE/UART...),
 * tach dong, giai ma, goi ui_set_*. Khong phu thuoc loai transport. */
#ifndef DASHBOARD_HLP_H
#define DASHBOARD_HLP_H

#include "dashboard.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Gui 1 dong len dien thoai. `line` KHONG co LF: transport tu them '\n' (hoac 1 WebSocket text message). */
typedef void (*hlp_tx_cb_t)(const char *line, void *user);

void hlp_dash_init(hlp_tx_cb_t tx, void *user);   /* goi sau dashboard_create(); tx co the la NULL */
void hlp_dash_on_connect(void);                   /* transport vua noi: gui `dev` xin truong du lieu */
void hlp_dash_on_disconnect(void);                /* transport mat: hien NO SIGNAL */
void hlp_dash_feed(const uint8_t *data, size_t len);   /* byte nhan duoc, chunk bat ky */

#ifdef __cplusplus
}
#endif
#endif
