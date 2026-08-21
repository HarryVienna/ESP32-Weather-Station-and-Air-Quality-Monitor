#pragma once

#include "sdkconfig.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Landscape display dimensions (after 270° rotation) - same physical panel
 * size on both supported boards. */
#define DISPLAY_WIDTH   1280
#define DISPLAY_HEIGHT  800

/* Board choice lives in Kconfig now (see main/Kconfig.projbuild -> "idf.py
 * menuconfig" -> "Weather Station Display Board", or edit sdkconfig.defaults*
 * directly) - a `choice` there guarantees exactly one of
 * CONFIG_DISPLAY_BOARD_WAVESHARE/CONFIG_DISPLAY_BOARD_GUITION is ever set,
 * this is just cheap insurance against a stale/hand-edited sdkconfig. */
#if !defined(CONFIG_DISPLAY_BOARD_WAVESHARE) && !defined(CONFIG_DISPLAY_BOARD_GUITION)
#error "Run 'idf.py menuconfig' -> 'Weather Station Display Board' and pick one"
#endif

/* Brightness that display_init() sets the backlight to at boot (see
 * display_waveshare.c/display_guition.c) - brightness_task.c seeds its
 * local current_brightness shadow variable with this instead of 0, so it
 * doesn't visibly ramp down and back up once its (delayed) task start
 * kicks in. Same value on both boards: display_set_brightness()'s 0-255
 * contract below is honored by both backlight mechanisms (Waveshare's I2C
 * register takes the byte as-is, Guition's LEDC PWM duty resolution is
 * also 8-bit/0-255), so there's nothing board-specific to convert here. */
#define DISPLAY_INIT_BRIGHTNESS 64

/**
 * @brief Initialize the display subsystem.
 *
 * Implemented in exactly one of display_waveshare.c / display_guition.c,
 * selected at compile time by CONFIG_DISPLAY_BOARD_WAVESHARE /
 * CONFIG_DISPLAY_BOARD_GUITION (Kconfig, see main/Kconfig.projbuild) - both
 * files can sit in the tree at once, only the selected one compiles to
 * anything (see the #if guard at the top of each).
 *
 * Order (both boards):
 *   1. Get the shared I2C bus from i2c_manager (touch and every other I2C
 *      peripheral in the project physically share this one bus - see
 *      main/i2c/i2c_manager.h; Waveshare's backlight is also on it, Guition's
 *      is LEDC PWM instead)
 *   2. Backlight
 *   3. MIPI DSI display (JD9365)
 *   4. Touch controller (GT911 on Waveshare, GSL3680 on Guition)
 *   5. LVGL port, display and touch registration
 *
 * Must be called after i2c_manager_init().
 *
 * @return ESP_OK on success
 */
esp_err_t display_init(void);

/**
 * @brief Return the LVGL display handle.
 *        Only valid after display_init().
 */
lv_disp_t *display_get(void);

/**
 * @brief Set the backlight brightness.
 * @param val 0-255 (0=off, 255=full brightness)
 */
esp_err_t display_set_brightness(uint8_t val);

#ifdef __cplusplus
}
#endif
