#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sensor-Receiver initialisieren.
 *
 * Holt den I2C-Bus vom i2c_manager, fügt den S3-Slave hinzu und
 * überträgt Zeitzone + aktuelle Systemzeit einmalig.
 *
 * Muss nach i2c_manager_init() aufgerufen werden.
 *
 * @return ESP_OK on success
 */
esp_err_t receiver_init(void);

/**
 * @brief Sensor-Polling als FreeRTOS-Task starten.
 *
 * Erstellt einen Task der alle 2 s verfügbare Pakete vom S3-Slave abholt
 * und per ESP_LOGI ausgibt. Kehrt sofort zurück.
 */
void receiver_start(void);

#ifdef __cplusplus
}
#endif
