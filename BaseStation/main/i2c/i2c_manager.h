#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pin constants for external GPIO recovery */
#define I2C_MANAGER_SDA_PIN  GPIO_NUM_20
#define I2C_MANAGER_SCL_PIN  GPIO_NUM_21

esp_err_t i2c_manager_init(void);
esp_err_t i2c_manager_del_bus(void);   /* delete the bus (for recovery) */
i2c_master_bus_handle_t i2c_manager_get_bus(void);

#ifdef __cplusplus
}
#endif
