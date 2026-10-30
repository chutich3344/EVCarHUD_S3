#include "settings.h"
#include "nvs.h"

#define NS "evcarhud"

static int get_u8(const char *key, int def)
{
    nvs_handle_t h;
    uint8_t v = (uint8_t)def;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return def;
    if (nvs_get_u8(h, key, &v) != ESP_OK) v = (uint8_t)def;
    nvs_close(h);
    return v;
}

static void set_u8(const char *key, int v)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, (uint8_t)v);
    nvs_commit(h);
    nvs_close(h);
}

int settings_get_mode(void) { return get_u8("mode", 0); }
void settings_set_mode(int mode) { set_u8("mode", mode); }
bool settings_get_demo(void) { return get_u8("demo", 0) != 0; }
void settings_set_demo(bool on) { set_u8("demo", on ? 1 : 0); }
bool settings_get_lcd_alt(void) { return get_u8("lcd", 0) != 0; }
void settings_set_lcd_alt(bool on) { set_u8("lcd", on ? 1 : 0); }
