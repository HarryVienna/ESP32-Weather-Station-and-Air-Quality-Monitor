#include <string.h>
#include "esp_log.h"
#include "bme280_sensor.h"

static const char *TAG = "bme280";

static int8_t i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t cnt, void *intf_ptr)
{
    bme280_sensor_t *sensor = (bme280_sensor_t *)intf_ptr;
    uint8_t tx_buf[cnt + 1];
    tx_buf[0] = reg_addr;
    if (cnt > 0 && reg_data != NULL)
        memcpy(&tx_buf[1], reg_data, cnt);
    esp_err_t err = i2c_master_transmit(sensor->i2c_handle, tx_buf, cnt + 1, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: 0x%x", err);
        return 1;
    }
    return 0;
}

static int8_t i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t cnt, void *intf_ptr)
{
    bme280_sensor_t *sensor = (bme280_sensor_t *)intf_ptr;
    esp_err_t err = i2c_master_transmit_receive(sensor->i2c_handle, &reg_addr, 1, reg_data, cnt, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: 0x%x", err);
        return 1;
    }
    return 0;
}

static void delay_us(uint32_t delay, void *intf_ptr)
{
    esp_rom_delay_us(delay);
}

esp_err_t bme280_sensor_init(bme280_sensor_t *sensor, const bme280_config_t *config)
{
    sensor->config = *config;

    gpio_reset_pin(config->sda_pin);
    gpio_reset_pin(config->scl_pin);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = config->sda_pin,
        .scl_io_num        = config->scl_pin,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &sensor->bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: 0x%x", ret);
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = config->dev_id,
        .scl_speed_hz    = 50000,
    };
    ret = i2c_master_bus_add_device(sensor->bus_handle, &dev_cfg, &sensor->i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C add device failed: 0x%x", ret);
        i2c_del_master_bus(sensor->bus_handle);
        return ret;
    }

    sensor->device.intf     = BME280_I2C_INTF;
    sensor->device.write    = i2c_write;
    sensor->device.read     = i2c_read;
    sensor->device.delay_us = delay_us;
    sensor->device.intf_ptr = sensor;

    int8_t rslt = bme280_init(&sensor->device);
    if (rslt != BME280_OK) {
        ESP_LOGE(TAG, "BME280 init failed: %d", rslt);
        i2c_master_bus_rm_device(sensor->i2c_handle);
        i2c_del_master_bus(sensor->bus_handle);
        return ESP_FAIL;
    }

    sensor->device.settings.osr_p  = config->osr_p;
    sensor->device.settings.osr_t  = config->osr_t;
    sensor->device.settings.osr_h  = config->osr_h;
    sensor->device.settings.filter = config->filter;

    uint8_t mask = BME280_OSR_PRESS_SEL | BME280_OSR_TEMP_SEL | BME280_OSR_HUM_SEL | BME280_FILTER_SEL;
    rslt = bme280_set_sensor_settings(mask, &sensor->device);
    if (rslt != BME280_OK) {
        ESP_LOGE(TAG, "BME280 set settings failed: %d", rslt);
        i2c_master_bus_rm_device(sensor->i2c_handle);
        i2c_del_master_bus(sensor->bus_handle);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BME280 initialized (chip_id=0x%02x)", sensor->device.chip_id);
    return ESP_OK;
}

esp_err_t bme280_sensor_read(bme280_sensor_t *sensor, bme280_data_t *data)
{
    int8_t rslt = bme280_set_sensor_mode(BME280_FORCED_MODE, &sensor->device);
    if (rslt != BME280_OK) {
        ESP_LOGE(TAG, "BME280 set mode failed: %d", rslt);
        return ESP_FAIL;
    }

    uint32_t delay = bme280_cal_meas_delay(&sensor->device.settings);
    sensor->device.delay_us(delay * 1000, sensor->device.intf_ptr);

    struct bme280_data raw;
    rslt = bme280_get_sensor_data(BME280_ALL, &raw, &sensor->device);
    if (rslt != BME280_OK) {
        ESP_LOGE(TAG, "BME280 get data failed: %d", rslt);
        return ESP_FAIL;
    }

    data->temperature = raw.temperature;
    data->pressure    = raw.pressure / 100.0;
    data->humidity    = raw.humidity;

    ESP_LOGI(TAG, "T=%.2f°C P=%.2f hPa H=%.2f%%", data->temperature, data->pressure, data->humidity);
    return ESP_OK;
}

void bme280_sensor_deinit(bme280_sensor_t *sensor)
{
    if (sensor->i2c_handle) {
        i2c_master_bus_rm_device(sensor->i2c_handle);
        sensor->i2c_handle = NULL;
    }
    if (sensor->bus_handle) {
        i2c_del_master_bus(sensor->bus_handle);
        sensor->bus_handle = NULL;
    }
}
