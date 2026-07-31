#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Landscape display dimensions (after 270° rotation) */
#define DISPLAY_WIDTH   1280
#define DISPLAY_HEIGHT  800

/* Brightness that display_init() sets the backlight to at boot (see
 * display.c) - brightness_task.c seeds its local current_brightness
 * shadow variable with this instead of 0, so it doesn't visibly ramp
 * down and back up once its (delayed) task start kicks in. */
#define DISPLAY_INIT_BRIGHTNESS 64

/**
 * @brief Initialize the display subsystem.
 *
 * Order:
 *   1. Get I2C bus from i2c_manager
 *   2. Backlight (I2C 0x45)
 *   3. MIPI DSI display (JD9365)
 *   4. GT911 touch controller
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
