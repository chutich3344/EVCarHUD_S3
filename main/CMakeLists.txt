# Ban 2.8 VML (CONFIG_EVCARHUD_VML, sdkconfig.vml): Bluetooth thuong SPP cho VietMap Live thay Bluetooth LE.
if(CONFIG_EVCARHUD_VML)
    set(link_src "vml_link.c")
else()
    set(link_src "ble_link.c")
endif()

idf_component_register(
    SRCS "main.c" "board.c" "buttons.c" "settings.c" ${link_src} "xadas.c" "ota.c" "link.c" "demo.c"
         "dashboard.c" "dashboard_hlp.c" "dashboard_signs.c" "hlp.c" "gofa.c"
         "ui_font_cap12.c" "ui_font_sm12.c" "ui_font_txt14.c" "ui_font_txt16.c" "ui_font_mono14.c"
         "ui_font_dist24.c" "ui_font_sign24.c" "ui_font_speed62.c" "ui_font_speed88.c"
         "ui_font_sign32.c" "ui_font_clock60.c" "ui_font_clock80.c" "ui_logo.c"
         "ui_font_num54.c" "ui_font_num30.c" "ui_font_speed72.c" "ui_font_speed110.c" "ui_font_sign40.c" "ui_font_unit22.c" "ui_font_onoff44.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_timer esp_adc nvs_flash bt esp_app_format app_update)
