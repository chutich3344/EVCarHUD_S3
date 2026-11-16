/* Cap nhat firmware OTA qua app dien thoai (Bluetooth LE). App EVCarHUD hoi evcarhud.com/api/ota ban moi nhat,
 * tai file .bin ve dien thoai roi gui xuong HUD qua dich vu GATT "EVCarHUD OTA" (ble_link.c):
 *   dich vu 4556d0f0-8c2a-4b7e-9d3f-5a1c0e7b2f10
 *   CTRL    4556d0f1-...  doc: "OTA1|<du an>|<phien ban>|<MAC chip>|<model>|<dung luong khe>"
 *                         ghi: "B|<so byte file>|<bo qua dau file>" bat dau ("bo qua" = 65536 voi file gop nap 0x0, 0 voi file app),
 *                              "E" xong, "A" huy
 *                         notify: "OK|B", "P|<so byte da ghi>" (moi 4 KB), "R|<vi tri can gui tiep>" (mat goi),
 *                                 "OK|E|<phien ban moi>" (HUD khoi dong lai sau 2 giay), "ER|<loi>"
 *   DATA    4556d0f2-...  ghi khong phan hoi: [vi tri 4 byte little endian][du lieu]. HUD chi nhan dung vi tri tiep theo;
 *                         app gui truoc toi da 6 KB so voi "P" cuoi, qua 2 giay khong tien thi gui lai tu "P" cuoi.
 * Ban moi khoi dong lan dau: 30 giay sau khi len man chinh moi xac nhan; sap truoc do thi bootloader quay ve ban cu. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void ota_start(void);                                  /* goi trong app_main sau khi dung bang dieu khien */

/* ble_link.c goi (task NimBLE: chi chep du lieu, khong ghi flash o day) */
int ota_ble_ctrl(const uint8_t *d, size_t n);          /* 0 hoac ma loi ATT */
int ota_ble_data(const uint8_t *d, size_t n);
int ota_ble_info(char *out, size_t cap);               /* do dai chuoi CTRL doc */
void ota_ble_disconnected(void);
void ota_set_notify(void (*fn)(const char *msg));      /* ble_link.c: gui notify tren CTRL */
