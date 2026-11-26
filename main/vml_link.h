/* Ban 2.8 VML: Bluetooth thuong (SPP) gia HUD VietMap H1V cho app VietMap Live. Xem vml_link.c. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void vml_link_start(void);       /* bat Bluetooth + tao task, tra ve ngay */
int vml_link_state(void);        /* 0 chua len, 1 cho ket noi, 2 app dang noi, -1 loi */

#ifdef __cplusplus
}
#endif
