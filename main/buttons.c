#include "buttons.h"
#include "board.h"
#include "dashboard.h"
#include "settings.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POLL_MS 20
#define LONG_MS 3000

static const char *TAG = "nut";

static void btn_task(void *arg)
{
    (void)arg;
    int held = 0;
    bool fired = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        bool down = gpio_get_level(CONFIG_EVCARHUD_PIN_BOOT) == 0;
        if (down) {
            held += POLL_MS;
            if (held >= LONG_MS && !fired) {                       /* giu 3 s: doi che do trinh dien */
                fired = true;
                bool demo = !settings_get_demo();
                settings_set_demo(demo);
                ESP_LOGI(TAG, "che do trinh dien: %s", demo ? "bat" : "tat");
                board_ui_lock();
                ui_toast(demo ? "Trình diễn: BẬT" : "Trình diễn: TẮT");
                board_ui_unlock();
                vTaskDelay(pdMS_TO_TICKS(1200));
                esp_restart();
            }
            continue;
        }
        if (held >= 40 && !fired) {                                /* bam nha (loc rung 40 ms) */
            board_ui_lock();
            ui_cycle_mode();
            board_ui_unlock();
        }
        held = 0;
        fired = false;
    }
}

void buttons_start(void)
{
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << CONFIG_EVCARHUD_PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&c);
    xTaskCreate(btn_task, "nut", 3072, NULL, 3, NULL);
}
