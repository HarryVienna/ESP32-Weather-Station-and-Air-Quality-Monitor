#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "bme280.h"

typedef struct {
    double pressure;
    double temperature;
    double humidity;
} bme280_data_t;

typedef struct {
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    uint8_t osr_p;
    uint8_t osr_t;
    uint8_t osr_h;
    uint8_t filter;
    uint8_t dev_id;
} bme280_config_t;

typedef struct {
    bme280_config_t         config;
    struct bme280_dev       device;
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t i2c_handle;
} bme280_sensor_t;

esp_err_t bme280_sensor_init(bme280_sensor_t *sensor, const bme280_config_t *config);
esp_err_t bme280_sensor_read(bme280_sensor_t *sensor, bme280_data_t *data);
void      bme280_sensor_deinit(bme280_sensor_t *sensor);
