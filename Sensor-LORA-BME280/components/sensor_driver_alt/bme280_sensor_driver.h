#pragma once


#include "driver/i2c.h"
#include "esp_err.h"

#include "bme280.h"
#include "sensor_driver.h"



/**
* @brief Sensor Configuration Type
*
*/
typedef struct sensor_driver_bme280_conf_s {
    uint8_t osr_p;  /*< pressure oversampling */
    uint8_t osr_t;  /*< temperature oversampling */
    uint8_t osr_h;  /*< humidity oversampling */
    uint8_t filter; /*< filter coefficient */
    uint8_t dev_id; /*< device address */
} sensor_driver_bme280_conf_t;




typedef struct {
    sensor_driver_t parent;

    sensor_driver_bme280_conf_t driver_config;
    struct bme280_dev bme280_device;

} sensor_driver_bme280_t;


/**
* @brief Install a new BME280 driver 
*
* @param config: step motor configuration
* @return
*      step motor instance or NULL
*/
sensor_driver_t *sensor_driver_new_bme280(const sensor_driver_bme280_conf_t *config);














