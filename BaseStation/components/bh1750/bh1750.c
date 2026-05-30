#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bh1750.h"


static const char *TAG = "bh1750";


static esp_err_t write_reg_i2c(bh_1750_t *sensor, uint8_t *data, uint8_t len) {
    return i2c_master_transmit(sensor->dev_handle, data, len, 1000);
}

static esp_err_t read_reg_i2c(bh_1750_t *sensor, uint8_t *data, uint8_t len) {
    if (len == 0) {
        return ESP_OK;
    }
    return i2c_master_receive(sensor->dev_handle, data, len, 1000);
}

esp_err_t bh1750_init(bh_1750_t *sensor, i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = i2c_addr,
        .scl_speed_hz    = 50000,
    };
    return i2c_master_bus_add_device(bus_handle, &dev_cfg, &sensor->dev_handle);
}


esp_err_t bh1750_power_down(bh_1750_t *sensor)
{
    uint8_t cmd = POWER_DOWN;
    return write_reg_i2c(sensor, &cmd, 1);
}

esp_err_t bh1750_power_on(bh_1750_t *sensor)
{
    uint8_t cmd = POWER_UP;
    return write_reg_i2c(sensor, &cmd, 1);
}

esp_err_t bh1750_send_opcode(bh_1750_t *sensor, bh1750_opcode_t opcode)
{
    uint8_t cmd = opcode;
    esp_err_t ret = write_reg_i2c(sensor, &cmd, 1);
    vTaskDelay(pdMS_TO_TICKS(180));
    return ret;
}

esp_err_t bh1750_set_measure_time(bh_1750_t *sensor, uint8_t time)
{
    if (time < 31 || time > 254) {
        return ESP_FAIL;
    }

    uint8_t cmd_high = CHANGE_MEASURE_TIME_HIGH | (time >> 5);
    uint8_t cmd_low  = CHANGE_MEASURE_TIME_LOW  | (time & 0x1f);

    esp_err_t ret = write_reg_i2c(sensor, &cmd_high, 1);
    if (ret == ESP_OK) {
        ret = write_reg_i2c(sensor, &cmd_low, 1);
    }
    vTaskDelay(pdMS_TO_TICKS(180));
    return ret;
}

esp_err_t bh1750_read(bh_1750_t *sensor, uint16_t *lux)
{
    uint8_t buf[2];

    esp_err_t ret = read_reg_i2c(sensor, buf, 2);
    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t raw = ((uint16_t)buf[0] << 8) | buf[1];
    *lux = (raw * 10) / 12; // division by 1.2
    return ESP_OK;
}
