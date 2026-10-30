/* ===== Phan cung ESP32-2432S028 (theo github.com/witnessmenow/ESP32-Cheap-Yellow-Display) =====
 *  LCD (SPI2): SCLK 14, MOSI 13, MISO 12, CS 15, DC 2, RST noi EN, den nen 21 (muc cao = sang)
 *  Cam ung XPT2046 (SPI3, bus rieng): SCLK 25, MOSI 32, MISO 39, CS 33
 *  LED RGB: 4 / 16 / 17 (muc thap = sang), loa 26 (qua mach khuech dai), quang tro 34 (ADC1 kenh 6)
 * Panel goc 240x320 doc; xoay ngang bang phan cung (swap_xy) de LVGL thay man hinh 320x240. */
#include "board.h"
#include "sdkconfig.h"
#include "dashboard.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if CONFIG_EVCARHUD_LCD_ILI9341
#include "esp_lcd_ili9341.h"
#endif
#if CONFIG_EVCARHUD_TOUCH
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#endif
#if CONFIG_EVCARHUD_LDR
#include "esp_adc/adc_oneshot.h"
#endif

/* Muc bool tat trong menuconfig thi CONFIG_... khong duoc dinh nghia: doi ra true/false o day */
#ifdef CONFIG_EVCARHUD_LCD_INVERT
#define LCD_INVERT true
#else
#define LCD_INVERT false
#endif
#ifdef CONFIG_EVCARHUD_LCD_SWAP_XY
#define LCD_SWAP_XY true
#else
#define LCD_SWAP_XY false
#endif
#ifdef CONFIG_EVCARHUD_LCD_MIRROR_X
#define LCD_MIRROR_X true
#else
#define LCD_MIRROR_X false
#endif
#ifdef CONFIG_EVCARHUD_LCD_MIRROR_Y
#define LCD_MIRROR_Y true
#else
#define LCD_MIRROR_Y false
#endif

static const char *TAG = "board";

#define LCD_SPI_HOST SPI2_HOST
#define TP_SPI_HOST  SPI3_HOST
#define DRAW_LINES   20                   /* bo dem ve tung phan: 320 x 20 diem x 2 byte, 2 bo, RAM DMA */

#define BL_TIMER   LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0
#define SPK_TIMER  LEDC_TIMER_1
#define SPK_CHANNEL LEDC_CHANNEL_1

static lv_display_t *s_disp;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_mutex;

/* ---------------------- den nen (PWM) ---------------------- */
static void backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BL_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = CONFIG_EVCARHUD_PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void board_set_backlight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, (uint32_t)percent * 1023u / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

/* ---------------------- LED RGB (muc thap = sang) ---------------------- */
static void led_pin(int pin, bool on)
{
    if (pin >= 0) gpio_set_level(pin, on ? 0 : 1);
}

static void led_init(void)
{
    const int pins[] = {CONFIG_EVCARHUD_PIN_LED_R, CONFIG_EVCARHUD_PIN_LED_G, CONFIG_EVCARHUD_PIN_LED_B};
    for (int i = 0; i < 3; i++) {
        if (pins[i] < 0) continue;
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
        led_pin(pins[i], false);
    }
}

void board_set_led(bool r, bool g, bool b)
{
    led_pin(CONFIG_EVCARHUD_PIN_LED_R, r);
    led_pin(CONFIG_EVCARHUD_PIN_LED_G, g);
    led_pin(CONFIG_EVCARHUD_PIN_LED_B, b);
}

/* ---------------------- loa ---------------------- */
static QueueHandle_t s_beep_q;

static void tone(bool on)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, SPK_CHANNEL, on ? 512 : 0);  /* 50% */
    ledc_update_duty(LEDC_LOW_SPEED_MODE, SPK_CHANNEL);
}

static void beep_task(void *arg)
{
    (void)arg;
    int n;
    for (;;) {
        if (xQueueReceive(s_beep_q, &n, portMAX_DELAY) != pdTRUE) continue;
        for (int i = 0; i < n; i++) {
            tone(true);
            vTaskDelay(pdMS_TO_TICKS(90));
            tone(false);
            vTaskDelay(pdMS_TO_TICKS(70));
        }
    }
}

static void speaker_init(void)
{
    if (CONFIG_EVCARHUD_PIN_SPEAKER < 0) return;
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = SPK_TIMER,
        .freq_hz = 2400,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t c = {
        .gpio_num = CONFIG_EVCARHUD_PIN_SPEAKER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = SPK_CHANNEL,
        .timer_sel = SPK_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    if (ledc_timer_config(&t) != ESP_OK || ledc_channel_config(&c) != ESP_OK) {
        ESP_LOGW(TAG, "khong bat duoc loa: bo qua tieng tit");
        return;
    }
    s_beep_q = xQueueCreate(4, sizeof(int));
    if (s_beep_q) xTaskCreate(beep_task, "beep", 2048, NULL, 3, NULL);
}

void board_beep(int times)
{
    if (s_beep_q && times > 0) xQueueSend(s_beep_q, &times, 0);
}

/* ---------------------- quang tro ---------------------- */
#if CONFIG_EVCARHUD_LDR
static adc_oneshot_unit_handle_t s_adc;
#endif

int board_light_raw(void)
{
#if CONFIG_EVCARHUD_LDR
    if (!s_adc) {
        adc_oneshot_unit_init_cfg_t u = {.unit_id = ADC_UNIT_1};
        adc_oneshot_chan_cfg_t c = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
        if (adc_oneshot_new_unit(&u, &s_adc) != ESP_OK) return -1;
        adc_oneshot_config_channel(s_adc, ADC_CHANNEL_6, &c);          /* GPIO34 */
    }
    int v = -1;
    if (adc_oneshot_read(s_adc, ADC_CHANNEL_6, &v) != ESP_OK) return -1;
    return v;
#else
    return -1;
#endif
}

/* ---------------------- LVGL <-> esp_lcd ---------------------- */
static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    (void)io; (void)ev;
    lv_display_flush_ready((lv_display_t *)ctx);                   /* chay trong ngat SPI */
    return false;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)disp;
    int w = lv_area_get_width(area), h = lv_area_get_height(area);
    lv_draw_sw_rgb565_swap(px, (uint32_t)(w * h));                 /* LVGL: little-endian; man hinh SPI: big-endian */
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px);
}

static uint32_t tick_cb(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void lvgl_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
        uint32_t wait = lv_timer_handler();
        xSemaphoreGiveRecursive(s_mutex);
        if (wait < 5) wait = 5;
        if (wait > 40) wait = 40;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

bool board_ui_lock(void) { return xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY) == pdTRUE; }
void board_ui_unlock(void) { xSemaphoreGiveRecursive(s_mutex); }

/* ---------------------- cam ung ---------------------- */
#if CONFIG_EVCARHUD_TOUCH
static esp_lcd_touch_handle_t s_tp;

/* Giao dien chi can biet "co cham" (cham dau cung doi Ngay/Dem), nen khong can hieu chinh toa do. */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x = 0, y = 0;
    uint8_t cnt = 0;
    esp_lcd_touch_read_data(s_tp);
    bool pressed = esp_lcd_touch_get_coordinates(s_tp, &x, &y, NULL, &cnt, 1) && cnt > 0;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (pressed) {
        data->point.x = x < UI_W ? x : UI_W - 1;
        data->point.y = y < UI_H ? y : UI_H - 1;
    }
}

static void touch_init(lv_display_t *disp)
{
    spi_bus_config_t bus = {
        .mosi_io_num = CONFIG_EVCARHUD_PIN_TP_MOSI,
        .miso_io_num = CONFIG_EVCARHUD_PIN_TP_MISO,
        .sclk_io_num = CONFIG_EVCARHUD_PIN_TP_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    esp_err_t err = spi_bus_initialize(TP_SPI_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "khong tao duoc bus SPI cam ung (%s): bo qua cam ung", esp_err_to_name(err));
        return;
    }
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(CONFIG_EVCARHUD_PIN_TP_CS);
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TP_SPI_HOST, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "khong tao duoc IO cam ung (%s): bo qua cam ung", esp_err_to_name(err));
        return;
    }
    esp_lcd_touch_config_t tc = {
        .x_max = UI_W,
        .y_max = UI_H,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .flags = {.swap_xy = 1},
    };
    err = esp_lcd_touch_new_spi_xpt2046(io, &tc, &s_tp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "XPT2046 khong phan hoi (%s): bo qua cam ung", esp_err_to_name(err));
        return;
    }
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);
    ESP_LOGI(TAG, "cam ung san sang");
}
#endif

/* ---------------------- khoi tao ---------------------- */
lv_display_t *board_display_init(void)
{
    s_mutex = xSemaphoreCreateRecursiveMutex();
    backlight_init();                                              /* den nen tat cho den khi co hinh dau tien */
    led_init();
    speaker_init();

    lv_init();
    lv_tick_set_cb(tick_cb);
    s_disp = lv_display_create(UI_W, UI_H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    const size_t buf_bytes = (size_t)UI_W * DRAW_LINES * sizeof(uint16_t);
    void *b1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!b1 || !b2) {
        ESP_LOGE(TAG, "khong cap duoc bo dem ve (%u byte x2)", (unsigned)buf_bytes);
        return NULL;
    }
    lv_display_set_buffers(s_disp, b1, b2, (uint32_t)buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);

    spi_bus_config_t bus = {
        .mosi_io_num = CONFIG_EVCARHUD_PIN_LCD_MOSI,
        .miso_io_num = CONFIG_EVCARHUD_PIN_LCD_MISO,
        .sclk_io_num = CONFIG_EVCARHUD_PIN_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)buf_bytes,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CONFIG_EVCARHUD_PIN_LCD_DC,
        .cs_gpio_num = CONFIG_EVCARHUD_PIN_LCD_CS,
        .pclk_hz = CONFIG_EVCARHUD_LCD_SPI_MHZ * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = s_disp,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io));

    esp_lcd_panel_dev_config_t pc = {
        .reset_gpio_num = CONFIG_EVCARHUD_PIN_LCD_RST,
#if CONFIG_EVCARHUD_LCD_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };
#if CONFIG_EVCARHUD_LCD_ILI9341
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &pc, &s_panel));
#else
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &pc, &s_panel));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, LCD_INVERT));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

#if CONFIG_EVCARHUD_TOUCH
    touch_init(s_disp);
#endif

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 6144, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "man hinh %dx%d san sang (SPI %d MHz)", UI_W, UI_H, CONFIG_EVCARHUD_LCD_SPI_MHZ);
    return s_disp;
}
