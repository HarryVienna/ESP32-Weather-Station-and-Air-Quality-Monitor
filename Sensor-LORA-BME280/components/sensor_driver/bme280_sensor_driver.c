#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/i2c_master.h"

#include "bme280_sensor_driver.h"

static const char *TAG = "bme280";

static i2c_master_bus_handle_t bme280_i2c_bus = NULL;

void bme280_set_i2c_bus(i2c_master_bus_handle_t bus)
{
    bme280_i2c_bus = bus;
}

int8_t bme280_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t cnt, void *intf_ptr)
{
    sensor_driver_bme280_t *bme280 = (sensor_driver_bme280_t *)intf_ptr;

    ESP_LOGD(TAG, "I2C write: reg=0x%02x, cnt=%d", reg_addr, cnt);

    uint8_t tx_buf[cnt + 1];

    tx_buf[0] = reg_addr;
    if (cnt > 0 && reg_data != NULL) {
        memcpy(&tx_buf[1], reg_data, cnt);
    }

    esp_err_t err = i2c_master_transmit(bme280->i2c_device_handle, tx_buf, cnt + 1, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C transmit failed: 0x%x", err);
        return (int8_t)err;
    }

    return ESP_OK;
}

int8_t bme280_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t cnt, void *intf_ptr)
{
    sensor_driver_bme280_t *bme280 = (sensor_driver_bme280_t *)intf_ptr;

    ESP_LOGD(TAG, "I2C read: reg=0x%02x, cnt=%d", reg_addr, cnt);

    esp_err_t err = i2c_master_transmit_receive(bme280->i2c_device_handle, 
                                                &reg_addr, 1, 
                                                reg_data, cnt, 
                                                1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C transmit-receive failed: 0x%x", err);
        return (int8_t)err;
    }

    return ESP_OK;
}


void bme280_delay_us(uint32_t delay, void *intf_ptr)
{
    esp_rom_delay_us(delay);
}

esp_err_t bme280_check_sensor(uint8_t i2c_addr)
{
    ESP_LOGI(TAG, "Checking BME280 sensor at address 0x%02x...", i2c_addr);
    
    i2c_master_dev_handle_t device_handle;
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 100000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bme280_i2c_bus, &dev_config, &device_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: 0x%x", ret);
        return ret;
    }

    ESP_LOGI(TAG, "BME280 sensor check passed");
    i2c_master_bus_rm_device(device_handle);
    return ESP_OK;
}

esp_err_t bme280_init_sensor(sensor_driver_t *handle)
{
    esp_err_t ret = ESP_OK;
    sensor_driver_bme280_t *bme280 = __containerof(handle, sensor_driver_bme280_t, parent);

    sensor_driver_bme280_conf_t bme280_config = bme280->driver_config;

    ESP_LOGI(TAG, "Initializing BME280 at address 0x%02x...", bme280_config.dev_id);

    // Create I2C device handle
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = bme280_config.dev_id,
        .scl_speed_hz = 50000,  // Reduced to 50kHz for better reliability
    };

    ret = i2c_master_bus_add_device(bme280_i2c_bus, &dev_config, &bme280->i2c_device_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: 0x%x", ret);
        return ret;
    }

    ESP_LOGI(TAG, "I2C device handle created successfully");

    // Set up BME280 device structure
    bme280->bme280_device.intf = BME280_I2C_INTF;
    bme280->bme280_device.write = bme280_i2c_write;
    bme280->bme280_device.read = bme280_i2c_read;
    bme280->bme280_device.delay_us = bme280_delay_us;
    bme280->bme280_device.intf_ptr = bme280;  // Pass the bme280 struct itself

    // Initialize BME280
    ret = bme280_init(&bme280->bme280_device);
    if (ret != BME280_OK) {
        ESP_LOGE(TAG, "BME280 init failed: 0x%x", ret);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BME280 chip ID: 0x%02x", bme280->bme280_device.chip_id);

    // Set sensor settings
    bme280->bme280_device.settings.osr_p = bme280->driver_config.osr_p;
    bme280->bme280_device.settings.osr_t = bme280->driver_config.osr_t;
    bme280->bme280_device.settings.osr_h = bme280->driver_config.osr_h;
    bme280->bme280_device.settings.filter = bme280->driver_config.filter;

    uint8_t desired_settings = BME280_OSR_PRESS_SEL | BME280_OSR_TEMP_SEL | BME280_OSR_HUM_SEL | BME280_FILTER_SEL;
    ret = bme280_set_sensor_settings(desired_settings, &bme280->bme280_device);
    if (ret != BME280_OK) {
        ESP_LOGE(TAG, "BME280 set settings failed: 0x%x", ret);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BME280 sensor initialized successfully");
    return ESP_OK;
}

esp_err_t bme280_read_values(sensor_driver_t *handle, sensor_data_t *values)
{
    esp_err_t ret = ESP_OK;

    sensor_driver_bme280_t *bme280 = __containerof(handle, sensor_driver_bme280_t, parent);

    ESP_LOGI(TAG, "Reading BME280 sensor values...");

    ret = bme280_set_sensor_mode(BME280_FORCED_MODE, &bme280->bme280_device);
    if (ret != BME280_OK) {
        ESP_LOGE(TAG, "BME280 set mode failed: 0x%x", ret);
        return ESP_FAIL;
    }

    uint32_t delay = bme280_cal_meas_delay(&bme280->bme280_device.settings);
    bme280->bme280_device.delay_us(delay * 1000, bme280->bme280_device.intf_ptr);

    struct bme280_data comp_data;

    ret = bme280_get_sensor_data(BME280_ALL, &comp_data, &bme280->bme280_device);
    if (ret != BME280_OK) {
        ESP_LOGE(TAG, "BME280 get data failed: 0x%x", ret);
        return ESP_FAIL;
    }

    values->temperature = comp_data.temperature;
    values->pressure = comp_data.pressure / 100.0;
    values->humidity = comp_data.humidity;

    ESP_LOGI(TAG, "BME280 read successful: T=%.2f C, P=%.2f hPa, H=%.2f %%", 
             values->temperature, values->pressure, values->humidity);

    return ESP_OK;
}

static void bme280_deinit_sensor(sensor_driver_t *handle)
{
    sensor_driver_bme280_t *bme280 = __containerof(handle, sensor_driver_bme280_t, parent);

    // Explizit SLEEP MODE schreiben, bevor der I2C-Bus abgebaut wird.
    // Hintergrund: Der neue i2c_master-Treiber liefert I2C-Fehlercodes im
    // Bereich 0x6200+. Das Cast (int8_t)0x62XX ergibt 0 = BME280_OK,
    // wodurch bme280_set_sensor_settings() bei einem stillen I2C-Fehler
    // Garbage-Daten zurückliest und ctrl_meas[1:0]=11 (NORMAL MODE) schreibt.
    // Dieser explizite Sleep-Befehl stellt sicher, dass der Chip schläft,
    // egal in welchem Zustand er sich befindet.
    bme280_set_sensor_mode(BME280_SLEEP_MODE, &bme280->bme280_device);

    if (bme280->i2c_device_handle) {
        i2c_master_bus_rm_device(bme280->i2c_device_handle);
        bme280->i2c_device_handle = NULL;
    }
    free(bme280);
}

sensor_driver_t *sensor_driver_new_bme280(const sensor_driver_bme280_conf_t *config)
{
    sensor_driver_bme280_t *bme280 = calloc(1, sizeof(sensor_driver_bme280_t));

    bme280->driver_config.osr_t = config->osr_t;
    bme280->driver_config.osr_h = config->osr_h;
    bme280->driver_config.osr_p = config->osr_p;
    bme280->driver_config.filter = config->filter;
    bme280->driver_config.dev_id = config->dev_id;

    bme280->parent.init_sensor  = bme280_init_sensor;
    bme280->parent.read_values  = bme280_read_values;
    bme280->parent.deinit_sensor = bme280_deinit_sensor;

    return &bme280->parent;
}
