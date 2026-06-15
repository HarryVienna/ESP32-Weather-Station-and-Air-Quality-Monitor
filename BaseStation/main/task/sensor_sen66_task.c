#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "i2c/i2c_manager.h"

#include "sen66_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"

//#include "gui/gui.h"


static const char* TAG = "sensor_sen66_task";

//extern SemaphoreHandle_t lvgl_mux;

/**
 * @brief     Task for reading sensor data and displaying information
 *
 * @param     pvParameter   Pointer to task parameters (not used in this function)
 *
 * @details   Initializes and reads data from two sensors (SEN66).
 *            Monitors and retrieves sensor measurements, displaying them periodically.
 */
void sensor_sen66_task(void *pvParameter) {
    ESP_LOGI(TAG, "Start Sensor SEN66 task");
    
    int16_t error = 0;

    sensirion_i2c_hal_init(i2c_manager_get_bus());
    sen66_init(SEN66_I2C_ADDR_6B);


    error = sen66_device_reset();
    if (error) {
        ESP_LOGE(TAG, "Error executing sen5x_device_reset(): %i", error);
    }

    vTaskDelay(pdMS_TO_TICKS(1000 * 12));

    int8_t product_name[32] = {0};
    error = sen66_get_product_name(product_name, 32);
    if (error) {
        ESP_LOGE(TAG, "Error executing sen66_get_product_name(): %i", error);
    } else {
        ESP_LOGI(TAG, "Product name: %s", product_name);
    }

    int8_t serial_number[32] = {0};
    error = sen66_get_serial_number(serial_number, 32);
    if (error) {
        ESP_LOGE(TAG, "Error executing sen66_get_serial_number(): %i", error);
    } else {
        ESP_LOGI(TAG, "Serial number: %s", serial_number);
    }

// 21.97
// 4838

//23.79°
//5086

// 24.12°C 
// 5115

//24.21°C
//5310 

//24.25°C
//5326

//24.16°C
//5292

// 24.03°C
// 5041

// 22.58°
// 4645

// 23.67°
// 5239

    // Temperature offset: T_compensated = T_ambient + (slope * T_ambient) + offset
    // offset scaled by 200, slope scaled by 10000, time_constant in seconds (0 = immediate)
    float temp_offset = 0.0f;
    error = sen66_set_temperature_offset_parameters(
        (int16_t)(200.0f * temp_offset), /*slope=*/0, /*time_constant=*/0, /*slot=*/0);
    if (error) {
        ESP_LOGE(TAG, "Error executing sen66_set_temperature_offset_parameters(): %i", error);
    } else {
        ESP_LOGI(TAG, "Temperature offset set to %.2f °C", temp_offset);
    }

    // Start Measurement
    error = sen66_start_continuous_measurement();
    if (error) {
        ESP_LOGE(TAG, "Error executing sen5x_start_measurement(): %i", error);  
    }

    uint16_t mass_concentration_pm1p0;
    uint16_t mass_concentration_pm2p5;
    uint16_t mass_concentration_pm4p0;
    uint16_t mass_concentration_pm10p0;
    int16_t ambient_humidity;
    int16_t ambient_temperature;
    int16_t voc_index;
    int16_t nox_index;
    uint16_t co2 = 0;

    for (;;) {
        // Read Measurement
        vTaskDelay(pdMS_TO_TICKS(1000 * 10));



        error = sen66_read_measured_values_as_integers(
            &mass_concentration_pm1p0, &mass_concentration_pm2p5,
            &mass_concentration_pm4p0, &mass_concentration_pm10p0,
            &ambient_humidity, &ambient_temperature, &voc_index, &nox_index,
            &co2);

        if (error) {
            ESP_LOGE(TAG, "Error executing sen66_read_measured_values_as_integers(): %i", error);
        } else {
            ESP_LOGI(TAG, "Mass concentration Pm1p0: %u   Pm2p5: %u   Pm4p0: %u   Pm10p0: %u", 
                mass_concentration_pm1p0, mass_concentration_pm2p5, mass_concentration_pm4p0, mass_concentration_pm10p0);

            if (ambient_humidity == 0x7fff) {
                ESP_LOGI(TAG, "Ambient humidity: n/a");
            } else {
                ESP_LOGI(TAG, "Ambient humidity: %i %%RH",
                       ambient_humidity);
            }

            if (ambient_temperature == 0x7fff) {
                ESP_LOGI(TAG, "Ambient temperature: n/a");
            } else {
                ESP_LOGI(TAG, "Ambient temperature: %i °C",
                       ambient_temperature);
            }

            if (voc_index == 0x7fff) {
                ESP_LOGI(TAG, "Voc index: n/a");
            } else {
                ESP_LOGI(TAG, "Voc index: %i", voc_index);
            }

            if (nox_index == 0x7fff) {
                ESP_LOGI(TAG, "Nox index: n/a");
            } else {
                ESP_LOGI(TAG, "Nox index: %i", nox_index);
            }

            if (co2 == 0x7fff) {
                ESP_LOGI(TAG, "CO2: n/a");
            } else {
                ESP_LOGI(TAG, "CO2: %i", co2);
            }

            //xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY);
            //disp_sen5x(ambient_temperature / 200.0f, ambient_humidity / 100.0f, mass_concentration_pm1p0 / 10.0f, mass_concentration_pm2p5 / 10.0f, mass_concentration_pm4p0 / 10.0f, mass_concentration_pm10p0 / 10.0f, voc_index / 10.0f, nox_index / 10.0f);
            //xSemaphoreGiveRecursive(lvgl_mux);
        }
    }

    vTaskDelete(NULL); // Delete the task when done
}