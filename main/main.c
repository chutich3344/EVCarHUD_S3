/* EVCarHUD cho board ESP32-2432S028 ("Cheap Yellow Display" 2.8"):
 * dien thoai chay Waze Mod gui du lieu dan duong (HLP/1) qua Bluetooth LE -> giai ma -> giao dien the 320x240.
 * Giu BOOT 3 giay: bat/tat che do trinh dien (du lieu gia, khong can dien thoai). */
#include <stdio.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "ble_link.h"
#include "board.h"
#include "buttons.h"
#include "dashboard.h"
#include "demo.h"
#include "link.h"
#include "settings.h"
#include "ui_fonts.h"

static const char *TAG = "evcarhud";

/* --- giao dien -> phan cung --- */
static void on_night(bool night) { board_set_backlight(night ? CONFIG_EVCARHUD_BL_NIGHT : CONFIG_EVCARHUD_BL_DAY); }

static void on_mode(int mode) { settings_set_mode(mode); }   /* nho che do Ngay/Dem qua lan tat may */

static void on_level(int level)                              /* vuot toc do: LED do + 2 tieng tit */
{
    board_set_led(level == UI_LEVEL_OVER, false, false);
    if (level == UI_LEVEL_OVER) board_beep(2);
}

/* Man kiem tra mau: 4 o DO / XANH LA / TRANG / XANH DUONG (tren-trai, tren-phai, duoi-trai, duoi-phai).
 * Sau do den LED nhay xanh duong 3 giay: bam BOOT luc nay de doi sang chip man hinh con lai (ILI9341 <-> ST7789)
 * va khoi dong lai, khong can build lai. Sai mau / sai huong khac: idf.py menuconfig > EVCarHUD > Man hinh (README). */
static void color_test(void)
{
#if CONFIG_EVCARHUD_COLOR_TEST
    static const struct { uint32_t c; const char *name; int x, y; } q[] = {
        {0xFF0000, "ĐỎ", 0, 0}, {0x00FF00, "XANH LÁ", UI_W / 2, 0},
        {0xFFFFFF, "TRẮNG", 0, UI_H / 2}, {0x0000FF, "XANH DƯƠNG", UI_W / 2, UI_H / 2},
    };
    board_ui_lock();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_obj_create(scr);
        lv_obj_remove_style_all(b);
        lv_obj_set_pos(b, q[i].x, q[i].y);
        lv_obj_set_size(b, UI_W / 2, UI_H / 2);
        lv_obj_set_style_bg_color(b, lv_color_hex(q[i].c), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &ui_font_txt16, 0);
        lv_obj_set_style_text_color(l, lv_color_black(), 0);
        lv_label_set_text(l, q[i].name);
        lv_obj_center(l);
    }
    lv_obj_t *t = lv_label_create(scr);
    lv_obj_set_style_text_font(t, &ui_font_txt14, 0);
    lv_obj_set_style_bg_color(t, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_set_style_pad_all(t, 6, 0);
    const esp_app_desc_t *ad = esp_app_get_description();
    lv_label_set_text_fmt(t, "EVCarHUD %s · %s\nBấm BOOT nếu hình nhiễu", ad ? ad->version : "", board_lcd_name());
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(t);
    lv_screen_load(scr);
    board_ui_unlock();
    vTaskDelay(pdMS_TO_TICKS(50));
    board_set_backlight(CONFIG_EVCARHUD_BL_DAY);
    board_set_led(true, false, false);                       /* LED: do -> xanh la -> xanh duong de kiem tra */
    vTaskDelay(pdMS_TO_TICKS(600));
    board_set_led(false, true, false);
    vTaskDelay(pdMS_TO_TICKS(600));

    gpio_config_t bc = {.pin_bit_mask = 1ULL << CONFIG_EVCARHUD_PIN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&bc);
    for (int i = 0; i < 30; i++) {                           /* 3 giay nhay xanh duong: cho bam BOOT */
        board_set_led(false, false, (i / 3) % 2 == 0);
        if (gpio_get_level(CONFIG_EVCARHUD_PIN_BOOT) == 0) {
            settings_set_lcd_alt(!settings_get_lcd_alt());
            ESP_LOGW(TAG, "doi chip man hinh (dang dung %s), khoi dong lai", board_lcd_name());
            board_set_led(false, false, false);
            board_beep(1);
            vTaskDelay(pdMS_TO_TICKS(400));
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    board_set_led(false, false, false);
#endif
}

#if CONFIG_EVCARHUD_LDR
static void light_task(void *arg)                            /* quang tro: Tu dong Ngay/Dem, co tre de khong nhap nhay */
{
    (void)arg;
    int dark_cnt = 0, n = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int v = board_light_raw();
        if (v < 0) continue;
        if (++n % 10 == 0) ESP_LOGI(TAG, "anh sang: %d (toi khi > %d)", v, CONFIG_EVCARHUD_LDR_DARK_ABOVE);
        bool dark = v > CONFIG_EVCARHUD_LDR_DARK_ABOVE;
        dark_cnt = dark ? (dark_cnt < 5 ? dark_cnt + 1 : 5) : (dark_cnt > 0 ? dark_cnt - 1 : 0);
        if (dark_cnt == 5 || dark_cnt == 0) {
            board_ui_lock();
            ui_set_ambient(dark_cnt == 5);
            board_ui_unlock();
        }
    }
}
#endif

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (!board_display_init()) {
        ESP_LOGE(TAG, "khong khoi tao duoc man hinh");
        return;
    }
    color_test();

    bool demo = settings_get_demo();
    board_ui_lock();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_screen_load(scr);
    dashboard_create(scr);
    ui_set_mode(settings_get_mode());
    ui_set_night_hook(on_night);                             /* dat den nen ngay theo Ngay/Dem */
    ui_set_mode_hook(on_mode);
    ui_set_level_hook(on_level);
    link_init();                                             /* bo giai ma HLP + chon duong truyen */
    if (demo) {
        demo_start();
        ui_toast("Trình diễn (giữ BOOT 3 giây để tắt)");
    } else {
        char hint[96];
        snprintf(hint, sizeof hint, "Mở Waze Mod > HUD > Bluetooth LE,\nchọn \"%s\"", CONFIG_EVCARHUD_BLE_NAME);
        ui_set_hint(hint);
    }
    board_ui_unlock();

    buttons_start();
    if (!demo) ble_link_start();                             /* cho Waze Mod ket noi qua Bluetooth LE */
#if CONFIG_EVCARHUD_LDR
    xTaskCreate(light_task, "anh_sang", 2560, NULL, 2, NULL);
#endif
    ESP_LOGI(TAG, "EVCarHUD chay (%s). RAM trong con %u byte", demo ? "trinh dien" : "Bluetooth",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
