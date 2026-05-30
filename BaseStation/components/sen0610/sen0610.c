#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "sen0610.h"


#define REG_STATUS              0x00   // R
#define REG_CTRL0               0x01   // W
#define REG_CTRL1               0x02   // W
#define REG_SOFT_VERSION        0x03   // R

// Presence Detection Mode
#define REG_RESULT_STATUS       0x10   // R
#define REG_RESULT_RANGE_L      0x11   // R
#define REG_RESULT_RANGE_H      0x12   // R
#define REG_RESULT_SNR          0x13   // R
#define REG_TRIG_SENSITIVITY    0x20   // RW
#define REG_KEEP_SENSITIVITY    0x21   // RW
#define REG_TRIG_DELAY          0x22   // RW
#define REG_KEEP_TIMEOUT_L      0x23   // RW
#define REG_KEEP_TIMEOUT_H      0x24   // RW
#define REG_E_MIN_RANGE_L       0x25   // RW
#define REG_E_MIN_RANGE_H       0x26   // RW
#define REG_E_MAX_RANGE_L       0x27   // RW
#define REG_E_MAX_RANGE_H       0x28   // RW
#define REG_E_TRIG_RANGE_L      0x29   // RW
#define REG_E_TRIG_RANGE_H      0x2A   // RW

// Velocimetry and Ranging Mode
#define REG_RESULT_OBJ_MUN      0x10   // R
#define REG_CFAR_THR_L          0x20   // RW
#define REG_CFAR_THR_H          0x21   // RW
#define REG_T_MIN_RANGE_L       0x22   // RW
#define REG_T_MIN_RANGE_H       0x23   // RW
#define REG_T_MAX_RANGE_L       0x24   // RW
#define REG_T_MAX_RANGE_H       0x25   // RW
#define REG_MICRO_MOTION        0x26   // RW

static const char *TAG = "C4001";


static esp_err_t write_reg_i2c(sen0610_t *sensor, uint8_t reg, uint8_t *data, uint8_t len) {
    uint8_t buf[len + 1];
    buf[0] = reg;
    for (uint8_t i = 0; i < len; i++) {
        buf[i + 1] = data[i];
    }
    return i2c_master_transmit(sensor->dev_handle, buf, len + 1, 1000);
}

static esp_err_t read_reg_i2c(sen0610_t *sensor, uint8_t reg, uint8_t *data, uint8_t len) {
    if (len == 0) {
        return ESP_OK;
    }
    return i2c_master_transmit_receive(sensor->dev_handle, &reg, 1, data, len, 1000);
}

esp_err_t sen0610_init(sen0610_t *sensor, i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = i2c_addr,
        .scl_speed_hz    = 50000,
    };
    return i2c_master_bus_add_device(bus_handle, &dev_cfg, &sensor->dev_handle);
}

sensor_status_t sen0610_get_status(sen0610_t *sensor) {
    sensor_status_t data;

    uint8_t temp = 0;
    read_reg_i2c(sensor, REG_STATUS, &temp, 1);
    data.work_status = (temp & 0x01);
    data.work_mode   = (temp & 0x02) >> 1;
    data.init_status = (temp & 0x80) >> 7;

    return data;
}

uint32_t sen0610_get_soft_version(sen0610_t *sensor) {
    uint8_t temp[3] = {0};

    read_reg_i2c(sensor, REG_SOFT_VERSION, temp, 3);

    return (temp[0] * 10000) + (temp[1] * 100) + temp[2];
}

void sen0610_set_sensor(sen0610_t *sensor, set_cmd_t cmd) {
    uint8_t temp = cmd;

    if (cmd == START_SEN) {
        write_reg_i2c(sensor, REG_CTRL0, &temp, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    } else if (cmd == STOP_SEN) {
        write_reg_i2c(sensor, REG_CTRL0, &temp, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    } else if (cmd == RESET_SEN) {
        write_reg_i2c(sensor, REG_CTRL0, &temp, 1);
        vTaskDelay(pdMS_TO_TICKS(1500));
    } else if (cmd == SAVE_PARAMS) {
        write_reg_i2c(sensor, REG_CTRL1, &temp, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    } else if (cmd == RECOVER_SEN) {
        write_reg_i2c(sensor, REG_CTRL1, &temp, 1);
        vTaskDelay(pdMS_TO_TICKS(800));
    } else if (cmd == CHANGE_MODE) {
        write_reg_i2c(sensor, REG_CTRL1, &temp, 1);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

esp_err_t sen0610_set_mode(sen0610_t *sensor, sensor_mode_t mode) {
    sensor_status_t data = sen0610_get_status(sensor);

    if (data.work_mode == mode) {
        return ESP_OK;
    }

    sen0610_set_sensor(sensor, CHANGE_MODE);
    data = sen0610_get_status(sensor);

    return (data.work_mode == mode) ? ESP_OK : ESP_FAIL;
}

// Presence Detection Mode functions

esp_err_t sen0610_set_trig_sensitivity(sen0610_t *sensor, uint8_t sensitivity)
{
    if (sensitivity > 9) {
        return ESP_FAIL;
    }
    write_reg_i2c(sensor, REG_TRIG_SENSITIVITY, &sensitivity, 1);
    sen0610_set_sensor(sensor, SAVE_PARAMS);
    return ESP_OK;
}

uint8_t sen0610_get_trig_sensitivity(sen0610_t *sensor)
{
    uint8_t temp = 0;
    read_reg_i2c(sensor, REG_TRIG_SENSITIVITY, &temp, 1);
    return temp;
}

esp_err_t sen0610_set_keep_sensitivity(sen0610_t *sensor, uint8_t sensitivity)
{
    if (sensitivity > 9) {
        return ESP_FAIL;
    }
    write_reg_i2c(sensor, REG_KEEP_SENSITIVITY, &sensitivity, 1);
    sen0610_set_sensor(sensor, SAVE_PARAMS);
    return ESP_OK;
}

uint8_t sen0610_get_keep_sensitivity(sen0610_t *sensor)
{
    uint8_t temp = 0;
    read_reg_i2c(sensor, REG_KEEP_SENSITIVITY, &temp, 1);
    return temp;
}

esp_err_t sen0610_set_delay(sen0610_t *sensor, uint8_t trig, uint16_t keep)
{
    if (trig > 200) {
        return ESP_FAIL;
    }
    if (keep < 4 || keep > 3000) {
        return ESP_FAIL;
    }

    uint8_t temp[3];
    temp[0] = trig;
    temp[1] = (uint8_t)(keep);
    temp[2] = (uint8_t)(keep >> 8);
    write_reg_i2c(sensor, REG_TRIG_DELAY, temp, 3);
    sen0610_set_sensor(sensor, SAVE_PARAMS);
    return ESP_OK;
}

uint8_t sen0610_get_trig_delay(sen0610_t *sensor)
{
    uint8_t temp = 0;
    read_reg_i2c(sensor, REG_TRIG_DELAY, &temp, 1);
    return temp;
}

uint16_t sen0610_get_keep_timerout(sen0610_t *sensor)
{
    uint8_t temp[2] = {0};
    read_reg_i2c(sensor, REG_KEEP_TIMEOUT_L, temp, 2);
    return (((uint16_t)temp[1]) << 8) | temp[0];
}

esp_err_t sen0610_set_detect_range(sen0610_t *sensor, uint16_t min, uint16_t max, uint16_t trig)
{
    if (max < 240 || max > 2000) {
        return ESP_FAIL;
    }
    if (min < 30 || min > max) {
        return ESP_FAIL;
    }
    if (trig < 240 || trig > 2000) {
        return ESP_FAIL;
    }

    uint8_t temp[6];
    temp[0] = (uint8_t)(min);
    temp[1] = (uint8_t)(min >> 8);
    temp[2] = (uint8_t)(max);
    temp[3] = (uint8_t)(max >> 8);
    temp[4] = (uint8_t)(trig);
    temp[5] = (uint8_t)(trig >> 8);
    write_reg_i2c(sensor, REG_E_MIN_RANGE_L, temp, 6);
    sen0610_set_sensor(sensor, SAVE_PARAMS);
    return ESP_OK;
}

uint16_t sen0610_get_min_range(sen0610_t *sensor)
{
    uint8_t temp[2] = {0};
    read_reg_i2c(sensor, REG_E_MIN_RANGE_L, temp, 2);
    return (uint16_t)(temp[0] | ((uint16_t)temp[1] << 8));
}

uint16_t sen0610_get_max_range(sen0610_t *sensor)
{
    uint8_t temp[2] = {0};
    read_reg_i2c(sensor, REG_E_MAX_RANGE_L, temp, 2);
    return (uint16_t)(temp[0] | ((uint16_t)temp[1] << 8));
}

uint16_t sen0610_get_trig_range(sen0610_t *sensor)
{
    uint8_t temp[2] = {0};
    read_reg_i2c(sensor, REG_E_TRIG_RANGE_L, temp, 2);
    return (uint16_t)(temp[0] | ((uint16_t)temp[1] << 8));
}

void sen0610_get_presence_status(sen0610_t *sensor, presence_data_t *presence_data)
{
    uint8_t temp[3] = {0};
    read_reg_i2c(sensor, REG_RESULT_STATUS, temp, 3);

    presence_data->presence = temp[0] & 0x01;
    presence_data->range    = (uint16_t)(temp[1] | ((uint16_t)temp[2] << 8));
}


// Velocimetry and Ranging Mode functions

esp_err_t sen0610_set_detect_thres(sen0610_t *sensor, uint16_t min, uint16_t max, uint16_t thres)
{
    if (max > 1300) {
        return ESP_FAIL;
    }
    if (min > max) {
        return ESP_FAIL;
    }

    uint8_t temp[6];
    temp[0] = (uint8_t)(thres);
    temp[1] = (uint8_t)(thres >> 8);
    temp[2] = (uint8_t)(min);
    temp[3] = (uint8_t)(min >> 8);
    temp[4] = (uint8_t)(max);
    temp[5] = (uint8_t)(max >> 8);
    write_reg_i2c(sensor, REG_CFAR_THR_L, temp, 6);
    sen0610_set_sensor(sensor, SAVE_PARAMS);
    return ESP_OK;
}

uint16_t sen0610_get_tmin_range(sen0610_t *sensor)
{
    uint8_t temp[2] = {0};
    read_reg_i2c(sensor, REG_T_MIN_RANGE_L, temp, 2);
    return (uint16_t)(temp[0] | ((uint16_t)temp[1] << 8));
}

uint16_t sen0610_get_tmax_range(sen0610_t *sensor)
{
    uint8_t temp[2] = {0};
    read_reg_i2c(sensor, REG_T_MAX_RANGE_L, temp, 2);
    return (uint16_t)(temp[0] | ((uint16_t)temp[1] << 8));
}

uint16_t sen0610_get_thres_range(sen0610_t *sensor)
{
    uint8_t temp[2] = {0};
    read_reg_i2c(sensor, REG_CFAR_THR_L, temp, 2);
    return (uint16_t)(temp[0] | ((uint16_t)temp[1] << 8));
}

void sen0610_set_micro_detection(sen0610_t *sensor, switch_t sta)
{
    uint8_t temp = sta;
    write_reg_i2c(sensor, REG_MICRO_MOTION, &temp, 1);
    sen0610_set_sensor(sensor, SAVE_PARAMS);
}

switch_t sen0610_get_micro_detection(sen0610_t *sensor)
{
    uint8_t temp = 0;
    read_reg_i2c(sensor, REG_MICRO_MOTION, &temp, 1);
    return (switch_t)temp;
}

void sen0610_get_speed_status(sen0610_t *sensor, speed_data_t *speed_data)
{
    uint8_t temp[7] = {0};
    read_reg_i2c(sensor, REG_RESULT_OBJ_MUN, temp, 7);

    speed_data->number = temp[0];
    speed_data->range  = (uint16_t)(temp[1] | ((uint16_t)temp[2] << 8));
    speed_data->speed  = (int16_t) (temp[3] | ((uint16_t)temp[4] << 8));
    speed_data->energy = (uint16_t)(temp[5] | ((uint16_t)temp[6] << 8));
}
