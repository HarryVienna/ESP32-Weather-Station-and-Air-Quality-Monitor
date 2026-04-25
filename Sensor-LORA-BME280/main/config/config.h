#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "esp_err.h"
#include "u8g2.h"

#define CONFIG_DEFAULT_SENSOR_NR  0
#define CONFIG_DEFAULT_TX_POWER   12

typedef struct {
    uint8_t sensor_nr;  // 0–15
    int8_t  tx_power;   // –9 to 22 dBm
} sensor_config_t;

/**
 * Load config from NVS. Fills defaults if no entry found.
 * Requires nvs_flash_init() to have been called beforehand.
 */
esp_err_t config_load(sensor_config_t *cfg);

/**
 * Persist config to NVS.
 */
esp_err_t config_save(const sensor_config_t *cfg);

/**
 * Blocking interactive config menu on the OLED.
 * Returns after the user selects "Save".
 * Saves the config to NVS before returning.
 *
 * Navigation:
 *   Short press (not editing) → next item
 *   Long  press (not editing, on value item) → enter edit mode
 *   Short press (editing) → increment value
 *   Long  press (editing) → exit edit mode (keep value)
 *   Long  press (not editing, on SAVE) → save & exit
 */
void config_run_menu(u8g2_t *u8g2, sensor_config_t *cfg);

#endif /* CONFIG_H */
