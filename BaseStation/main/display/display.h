#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Landscape display dimensions (after 270° rotation) */
#define DISPLAY_WIDTH   1280
#define DISPLAY_HEIGHT  800

/* Helligkeit, auf die display_init() die Hintergrundbeleuchtung beim Boot
 * setzt (siehe display.c) - brightness_task.c startet seine lokale
 * current_brightness-Schattenvariable hiermit, statt bei 0, damit sie beim
 * (verzoegerten) Task-Start nicht erst sichtbar runter- und wieder
 * hochrampt. */
#define DISPLAY_INIT_BRIGHTNESS 64

/**
 * @brief Display-Subsystem initialisieren.
 *
 * Reihenfolge:
 *   1. I2C-Bus von i2c_manager holen
 *   2. Backlight (I2C 0x45)
 *   3. MIPI DSI Display (JD9365)
 *   4. GT911 Touch-Controller
 *   5. LVGL Port, Display- und Touch-Registrierung
 *
 * Muss nach i2c_manager_init() aufgerufen werden.
 *
 * @return ESP_OK on success
 */
esp_err_t display_init(void);

/**
 * @brief LVGL Display-Handle zurückgeben.
 *        Nur gültig nach display_init().
 */
lv_disp_t *display_get(void);

/**
 * @brief Helligkeit der Hintergrundbeleuchtung setzen.
 * @param val 0–255 (0=aus, 255=volle Helligkeit)
 */
esp_err_t display_set_brightness(uint8_t val);

#ifdef __cplusplus
}
#endif
