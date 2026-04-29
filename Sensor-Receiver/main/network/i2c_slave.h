/**
 * @file i2c_slave.h
 * @brief I2C Slave driver for P4 Master readout using ESP-IDF I2C Slave Driver v2
 *
 * Architektur:
 * - ESP32-S3 als I2C Slave (Adresse 0x38)
 * - ESP32-P4 als I2C Master
 * - Register-basiertes Protokoll für Sensor-Daten Abruf
 * - Verwendet ESP-IDF I2C Slave Driver v2 (CONFIG_I2C_ENABLE_SLAVE_DRIVER_VERSION_2)
 *
 * I2C Protokoll:
 *   Master WRITE Register-Adresse → Slave empfängt Command-Byte
 *   Master READ → Slave sendet Daten basierend auf letztem Command
 *
 * Register Map:
 *   0x00: READ  → Count (1 Byte: Anzahl verfügbarer Pakete)
 *   0x01: R/W → Packet Read (variable Länge, auto-pops vom Sensor-Stack)
 *   0x23: WRITE, 0x01 → Reset Drop-Counter
 *   0x24: READ → Total received (4 Byte, uint32_t LE)
 *   0x28: READ → Total overwritten (4 Byte, uint32_t LE)
 */

#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C Slave Configuration */
#define I2C_SLAVE_SDA         GPIO_NUM_47   /* I2C SDA Pin */
#define I2C_SLAVE_SCL         GPIO_NUM_48   /* I2C SCL Pin */
#define I2C_SLAVE_ADDR        0x38          /* 7-bit I2C Slave Address */
#define I2C_SLAVE_PORT        I2C_NUM_0     /* I2C Port */
#define I2C_CLOCK_KHZ         400           /* I2C Clock Frequency (kHz) */

/* I2C Register Map */
#define I2C_REG_COUNT         0x00          /* Number of available packets (1 byte) */
#define I2C_REG_PACKET_READ   0x01          /* Read & consume next packet (variable length) */
#define I2C_REG_SET_TIME      0x10          /* Write UTC Unix timestamp (4 bytes, uint32_t LE) */
#define I2C_REG_SET_TZ        0x11          /* Write POSIX timezone string (max 63 bytes + NUL) */
#define I2C_REG_RESET_DROP    0x23          /* Write 0x01 to reset dropped counter */
#define I2C_REG_STATS_RECV    0x24          /* Total packets received (4 bytes, uint32_t LE) */
#define I2C_REG_STATS_OVERWR  0x28          /* Total overwritten packets (4 bytes, uint32_t LE) */

/* Packet size constants */
#define I2C_MIN_PACKET_SIZE   17            /* header(4) + metadata(11) + min_payload(2) */
#define I2C_MAX_PACKET_SIZE   79            /* header(4) + metadata(11) + max_payload(64) */

/**
 * @brief Initialize I2C slave interface
 *
 * Creates I2C slave device handle and registers event callbacks.
 * Must be called before any I2C communication.
 *
 * @return ESP_OK on success, appropriate error code on failure
 */
esp_err_t i2c_slave_init(void);

/**
 * @brief Deinitialize I2C slave interface
 *
 * Deletes the I2C slave device handle and frees resources.
 */
esp_err_t i2c_slave_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_SLAVE_H */