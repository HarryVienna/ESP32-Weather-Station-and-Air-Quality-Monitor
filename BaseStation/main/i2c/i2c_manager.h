#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the shared I2C bus (SDA=GPIO7, SCL=GPIO8).
 *
 * One physical bus for everything on it: display backlight, GT911 touch,
 * SEN66, C4001 radar, BH1750 lux, and the Sensor-Receiver S3 link. Must be
 * called before display_init() and any other consumer's init function.
 */
esp_err_t i2c_manager_init(void);

esp_err_t i2c_manager_del_bus(void);   /* delete the bus (for recovery) */

i2c_master_bus_handle_t i2c_manager_get_bus(void);

#ifdef __cplusplus
}
#endif
