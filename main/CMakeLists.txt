idf_component_register(
    SRCS "main.c" "board.c" "buttons.c" "settings.c" "ble_link.c" "link.c" "demo.c"
         "dashboard.c" "dashboard_hlp.c" "dashboard_signs.c" "hlp.c"
         "ui_font_cap12.c" "ui_font_sm12.c" "ui_font_txt14.c" "ui_font_txt16.c" "ui_font_mono14.c"
         "ui_font_dist24.c" "ui_font_sign24.c" "ui_font_speed62.c" "ui_font_speed88.c"
         "ui_font_sign32.c" "ui_font_clock60.c" "ui_logo.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_timer esp_adc nvs_flash bt esp_app_format)
