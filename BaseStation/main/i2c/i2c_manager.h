#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pin-Konstanten für externe GPIO-Recovery */
#define I2C_MANAGER_SDA_PIN  GPIO_NUM_20
#define I2C_MANAGER_SCL_PIN  GPIO_NUM_21

esp_err_t i2c_manager_init(void);
esp_err_t i2c_manager_del_bus(void);   /* Bus löschen (für Recovery) */
i2c_master_bus_handle_t i2c_manager_get_bus(void);

#ifdef __cplusplus
}
#endif
