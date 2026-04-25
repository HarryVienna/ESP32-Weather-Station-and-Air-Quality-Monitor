#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/cdefs.h>
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "bme280_sensor_driver.h"

#define BME280_MQTT_MESSAGE " \"bme280\": { \"temperature\": %f, \"humidity\": %f, \"pressure\": %f },"

static const char *TAG = "bme280";


int8_t bme280_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t cnt, void *intf_ptr)
{
	esp_err_t err;

    uint8_t id = *(uint8_t *)intf_ptr;

	i2c_cmd_handle_t cmd = i2c_cmd_link_create();

	err = i2c_master_start(cmd);
    if (err != ESP_OK) {
        goto end;
    }	
	
	err = i2c_master_write_byte(cmd, (id << 1) | I2C_MASTER_WRITE, true);
    if (err != ESP_OK) {
        goto end;
    }

	err = i2c_master_write_byte(cmd, reg_addr, true);
    if (err != ESP_OK) {
        goto end;
    }

	err = i2c_master_write(cmd, reg_data, cnt, true);
    if (err != ESP_OK) {
        goto end;
    }

	err = i2c_master_stop(cmd);
    if (err != ESP_OK) {
        goto end;
    }

	err = i2c_master_cmd_begin(I2C_NUM_0, cmd, 10/portTICK_PERIOD_MS);
    if (err != ESP_OK) {
        goto end;
    }

end:	
	i2c_cmd_link_delete(cmd);
	return err;
}

int8_t bme280_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t cnt, void *intf_ptr)
{
	esp_err_t err;
 
    uint8_t id = *(uint8_t *)intf_ptr;

	i2c_cmd_handle_t cmd = i2c_cmd_link_create();

	err = i2c_master_start(cmd);
    if (err != ESP_OK) {
        goto end;
    }

	err = i2c_master_write_byte(cmd, (id << 1) | I2C_MASTER_WRITE, true);
    if (err != ESP_OK) {
        goto end;
    }

	err = i2c_master_write_byte(cmd, reg_addr, true);
    if (err != ESP_OK) {
        goto end;
    }

	err = i2c_master_start(cmd);
    if (err != ESP_OK) {
        goto end;
    }

	err = i2c_master_write_byte(cmd, (id << 1) | I2C_MASTER_READ, true);
    if (err != ESP_OK) {
        goto end;
    }

	err = i2c_master_read(cmd, reg_data, cnt, I2C_MASTER_LAST_NACK);
    if (err != ESP_OK) {
        goto end;
    }

	err = i2c_master_stop(cmd);
    if (err != ESP_OK) {
        goto end;
    }

	err = i2c_master_cmd_begin(I2C_NUM_0, cmd, 50/portTICK_PERIOD_MS);

end:	
	i2c_cmd_link_delete(cmd);

	return err;
}

void bme280_delay_us(uint32_t delay, void *intf_ptr)
{
	//vTaskDelay(v/1000/portTICK_PERIOD_MS);
	ets_delay_us(delay);  // Be carefull, should not be used in FreeRTOS
}


esp_err_t bme280_check_sensor(uint8_t i2c_addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 50 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    return ret;
}

/**
 * @brief Init Stepper
 *
 * @param motor handle to stepper_driver_t type object
 */
esp_err_t bme280_init_sensor(sensor_driver_t *handle)
{
    esp_err_t ret = ESP_OK;
    sensor_driver_bme280_t *bme280 = __containerof(handle, sensor_driver_bme280_t, parent);

	sensor_driver_bme280_conf_t bme280_config = bme280->driver_config;

    ret = bme280_check_sensor(bme280_config.dev_id);
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }

	bme280->bme280_device.intf = BME280_I2C_INTF;
	bme280->bme280_device.write = bme280_i2c_write;
	bme280->bme280_device.read = bme280_i2c_read;
	bme280->bme280_device.delay_us = bme280_delay_us;

    bme280->bme280_device.intf_ptr = &bme280->driver_config.dev_id;


	ret = bme280_init(&bme280->bme280_device);

	bme280->bme280_device.settings.osr_p = bme280->driver_config.osr_p;
	bme280->bme280_device.settings.osr_t = bme280->driver_config.osr_t;
	bme280->bme280_device.settings.osr_h = bme280->driver_config.osr_h;
	bme280->bme280_device.settings.filter = bme280->driver_config.filter;

	uint8_t desired_settings = BME280_OSR_PRESS_SEL | BME280_OSR_TEMP_SEL | BME280_OSR_HUM_SEL | BME280_FILTER_SEL;
	ret = bme280_set_sensor_settings(desired_settings, &bme280->bme280_device);

    return ret;
}

/**
 * @brief Read values from registers
 *
 * @param motor handle to stepper_driver_t type object
 */
esp_err_t bme280_read_values(sensor_driver_t *handle, sensor_data_t *values)
{
    esp_err_t ret = ESP_OK;

	sensor_driver_bme280_t *bme280 = __containerof(handle, sensor_driver_bme280_t, parent);
	sensor_driver_bme280_conf_t bme280_config = bme280->driver_config;

	ret = bme280_set_sensor_mode(BME280_FORCED_MODE, &bme280->bme280_device);

	uint32_t delay = bme280_cal_meas_delay(&bme280->bme280_device.settings);
	bme280->bme280_device.delay_us(delay * 1000, bme280->bme280_device.intf_ptr);

	struct bme280_data comp_data;

	ret = bme280_get_sensor_data(BME280_ALL, &comp_data, &bme280->bme280_device);

	if (ret == BME280_OK) {
		values->temperature = comp_data.temperature;
		values->pressure = comp_data.pressure / 100.0;
		values->humidity = comp_data.humidity;
	} 	

    return ret;
}

/**
 * @brief Get JSON value
 *
 * @param handle handle to sensor_driver_t type object
 */
void bme280_get_json(sensor_driver_t *handle, sensor_data_t values, char* message)
{
    sprintf(message, BME280_MQTT_MESSAGE, values.temperature, values.humidity, values.pressure);

    ESP_LOGI(TAG, "JSON %s", message); 
}

sensor_driver_t *sensor_driver_new_bme280(const sensor_driver_bme280_conf_t *config)
{
    sensor_driver_bme280_t *bme280 = calloc(1, sizeof(sensor_driver_bme280_t));

    bme280->driver_config.osr_t = config->osr_t;
    bme280->driver_config.osr_h = config->osr_h;
	bme280->driver_config.osr_p = config->osr_p;
	bme280->driver_config.filter = config->filter;
	bme280->driver_config.dev_id = config->dev_id;

    bme280->parent.init_sensor = bme280_init_sensor;
    bme280->parent.read_values= bme280_read_values;
    bme280->parent.get_json= bme280_get_json;

    return &bme280->parent;
}


