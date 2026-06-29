#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>

#include "sensirion_i2c_hal.h"

static const char* TAG = "sensirion_i2c_hal";

#define pdUS_TO_TICKS(time_us)                                               \
    ((TickType_t)(((TickType_t)(time_us) * (TickType_t)configTICK_RATE_HZ) / \
                  (TickType_t)1000000U))

#define SHT45_I2C_ADDR        0x44
#define SHT45_I2C_FREQ_HZ     400000
#define I2C_MASTER_TIMEOUT_MS 1000

static i2c_master_dev_handle_t dev_handle;

int16_t sensirion_i2c_hal_read(uint8_t address, uint8_t* data, uint8_t count) {
    return (int16_t)i2c_master_receive(dev_handle, data, count, I2C_MASTER_TIMEOUT_MS);
}

int16_t sensirion_i2c_hal_write(uint8_t address, const uint8_t* data,
                                uint8_t count) {
    return (int16_t)i2c_master_transmit(dev_handle, data, count, I2C_MASTER_TIMEOUT_MS);
}

void sensirion_i2c_hal_init(i2c_master_bus_handle_t bus_handle) {
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = SHT45_I2C_ADDR,
        .scl_speed_hz    = SHT45_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));
    ESP_LOGI(TAG, "SHT45 device added to I2C bus");
}

void sensirion_i2c_hal_free(void) {
    i2c_master_bus_rm_device(dev_handle);
    ESP_LOGI(TAG, "SHT45 device removed from I2C bus");
}

void sensirion_i2c_hal_sleep_usec(uint32_t useconds) {
    vTaskDelay(pdUS_TO_TICKS(useconds));
}
