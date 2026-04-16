#pragma once

#include "esp_err.h"

typedef struct sensor_data_s
{
    double pressure;
    double temperature;
    double humidity;
} sensor_data_t;


/**
* @brief Sensor Type
*
*/
typedef struct sensor_driver_s sensor_driver_t;



/**
* @brief Declare of Sensor Type
*
*/
struct sensor_driver_s {
    esp_err_t (*init_sensor)(sensor_driver_t *handle);
    esp_err_t (*read_values)(sensor_driver_t *handle, sensor_data_t *values);
    void (*get_json)(sensor_driver_t *handle, sensor_data_t values, char* message);
};


// |================================================================================================ |
// |                                           Initialisation                                        |
// |================================================================================================ |
esp_err_t sensor_driver_init_sensor(sensor_driver_t *handle);

// |================================================================================================ |
// |                          Functions that control hardware                                        |
// |================================================================================================ |
esp_err_t sensor_driver_read_values(sensor_driver_t *handle, sensor_data_t *values);
void sensor_driver_get_json(sensor_driver_t *handle, sensor_data_t values, char* message);



