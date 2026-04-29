/**
 * @file main.c
 * @brief ESP32-P4 I2C Master: Liest Sensor-Daten vom ESP32-S3 Slave
 *
 * Hardware:
 *   ESP32-P4 SDA -> (P4-seitiger Pin) <-> S3 GPIO_47 (SDA)
 *   ESP32-P4 SCL -> (P4-seitiger Pin) <-> S3 GPIO_48 (SCL)
 *   I2C Speed: 400 kHz
 *
 * Protokoll: Zwei separate Transaktionen (kein Repeated-Start), weil der
 * S3-Slave-Driver v2 kein Clock Stretching unterstützt:
 *   1. WRITE reg_addr, STOP
 *   2. (kurze Pause, damit der Slave-Task den FIFO füllen kann)
 *   3. READ data, STOP
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#include "../../common/packet_format.h"

static const char *TAG = "I2C_MASTER";

/* I2C Master Konfiguration */
#define I2C_MASTER_SDA  GPIO_NUM_20
#define I2C_MASTER_SCL  GPIO_NUM_19   /* GPIO_21 ist Display-RST auf dem S3-Board */
#define I2C_MASTER_FREQ 400000

/* Slave */
#define I2C_SLAVE_ADDR  0x38

/* Register Map (muss zur i2c_slave.h auf S3-Seite passen) */
#define I2C_REG_COUNT        0x00
#define I2C_REG_PACKET_READ  0x01
#define I2C_REG_SET_TIME     0x10   /* Write: UTC Unix-Timestamp (4 Byte LE) */
#define I2C_REG_SET_TZ       0x11   /* Write: POSIX Timezone-String (z.B. "CET-1CEST,...") */
#define I2C_REG_RESET_DROP   0x23
#define I2C_REG_STATS_RECV   0x24
#define I2C_REG_STATS_OVERWR 0x28

/* Zeitzone die an den S3-Slave übertragen wird */
#define SLAVE_TIMEZONE  "CET-1CEST,M3.5.0,M10.5.0/3"

/* Delay zwischen WRITE (register) und READ (data) in ms.
 * Gibt dem S3-Slave-Task Zeit, den FIFO zu befüllen. */
#define I2C_SLAVE_PREPARE_MS  5

#define PACKET_MIN_SIZE  (sizeof(packet_header_t) + sizeof(link_metadata_t))  /* 15 Byte */
#define PACKET_MAX_SIZE  (PACKET_MIN_SIZE + MAX_PAYLOAD_SIZE)                  /* 79 Byte */

static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;

/* ==========================================================================
 * I2C Hilfsfunktionen
 * ========================================================================== */

/**
 * @brief Register-Adresse schreiben, dann Daten lesen (zwei separate Transaktionen).
 */
static esp_err_t i2c_read_reg(uint8_t reg, uint8_t *buf, size_t len)
{
    esp_err_t ret = i2c_master_transmit(dev_handle, &reg, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WRITE reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(I2C_SLAVE_PREPARE_MS));

    ret = i2c_master_receive(dev_handle, buf, len, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "READ reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Nur schreiben (für Write-Only Register wie 0x23).
 */
static esp_err_t i2c_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    esp_err_t ret = i2c_master_transmit(dev_handle, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WRITE reg 0x%02X = 0x%02X failed: %s",
                 reg, value, esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief UTC Unix-Timestamp an den S3-Slave schicken (Register 0x10).
 *
 * Der S3 ruft damit settimeofday() auf. Die Zeitzone muss separat gesetzt
 * werden (i2c_set_timezone). Quelle des Timestamps ist typischerweise SNTP.
 */
static esp_err_t i2c_set_time(time_t utc_ts)
{
    uint32_t ts = (uint32_t)utc_ts;
    uint8_t buf[5] = {
        I2C_REG_SET_TIME,
        (uint8_t)(ts & 0xFF),
        (uint8_t)((ts >> 8)  & 0xFF),
        (uint8_t)((ts >> 16) & 0xFF),
        (uint8_t)((ts >> 24) & 0xFF),
    };
    esp_err_t ret = i2c_master_transmit(dev_handle, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SET_TIME failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief POSIX Timezone-String an den S3-Slave schicken (Register 0x11).
 *
 * Beispiel: "CET-1CEST,M3.5.0,M10.5.0/3"
 * Der S3 ruft damit setenv("TZ", ...) + tzset() auf.
 */
static esp_err_t i2c_set_timezone(const char *tz)
{
    size_t tz_len = strlen(tz);
    if (tz_len > 63) {
        ESP_LOGE(TAG, "Timezone string too long (%zu > 63)", tz_len);
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buf[65];
    buf[0] = I2C_REG_SET_TZ;
    memcpy(&buf[1], tz, tz_len + 1);  /* inkl. Null-Terminator */
    esp_err_t ret = i2c_master_transmit(dev_handle, buf, 1 + tz_len + 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SET_TZ failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ==========================================================================
 * Paket-Auswertung
 * ========================================================================== */

static const char *sensor_source_str(uint8_t source)
{
    switch (source) {
        case 1: return "LoRa";
        case 2: return "ESP-NOW";
        default: return "unknown";
    }
}

static const char *sensor_type_str(uint8_t type)
{
    switch (type) {
        case 1: return "BME280";
        case 2: return "HDC1080";
        case 3: return "DHT22";
        case 4: return "WIND";
        case 5: return "RAIN";
        case 6: return "LIGHT";
        default: return "unknown";
    }
}

static void print_packet(const uint8_t *buf, size_t buf_len)
{
    if (buf_len < PACKET_MIN_SIZE) {
        ESP_LOGW(TAG, "Buffer too small for packet (%zu < %zu)", buf_len, PACKET_MIN_SIZE);
        return;
    }

    const sensor_packet_t *pkt = (const sensor_packet_t *)buf;

    ESP_LOGI(TAG, "  Sensor #%d  type=%s  payload=%d B  via=%s",
             pkt->header.sensor_nr,
             sensor_type_str(pkt->header.sensor_type),
             pkt->header.payload_len,
             sensor_source_str(pkt->link.msg_source));
    ESP_LOGI(TAG, "  RSSI=%d dBm  SNR=%.1f  ts=%lu",
             pkt->link.rssi, pkt->link.snr, pkt->link.timestamp);

    if (pkt->header.payload_len > 0 &&
        pkt->header.payload_len <= MAX_PAYLOAD_SIZE) {
        /* Payload-Bytes als Hex */
        char hex[3 * MAX_PAYLOAD_SIZE + 1];
        size_t pos = 0;
        for (int i = 0; i < pkt->header.payload_len; i++) {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", pkt->payload[i]);
        }
        ESP_LOGI(TAG, "  Payload: %s", hex);
    }
}

/* ==========================================================================
 * app_main
 * ========================================================================== */

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-P4 I2C Master Reader");
    ESP_LOGI(TAG, "Slave 0x%02X  SDA=GPIO%d  SCL=GPIO%d  %d Hz",
             I2C_SLAVE_ADDR, I2C_MASTER_SDA, I2C_MASTER_SCL, I2C_MASTER_FREQ);

    /* Bus initialisieren */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port    = I2C_NUM_0,
        .sda_io_num  = I2C_MASTER_SDA,
        .scl_io_num  = I2C_MASTER_SCL,
        .clk_source  = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    /* Slave-Gerät hinzufügen */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_SLAVE_ADDR,
        .scl_speed_hz    = I2C_MASTER_FREQ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    /* Zeitzone und aktuelle Systemzeit einmalig an den S3-Slave übertragen.
     * time(NULL) liefert die P4-Systemzeit — diese sollte vorher per SNTP
     * synchronisiert worden sein. */
    i2c_set_timezone(SLAVE_TIMEZONE);
    time_t now = time(NULL);
    if (now > 1000000000) {  /* Sanity-Check: > Jahr 2001, also keine Epoch-0-Zeit */
        i2c_set_time(now);
        ESP_LOGI(TAG, "Zeit + Timezone an Slave übertragen (ts=%lld, tz=%s)",
                 (long long)now, SLAVE_TIMEZONE);
    } else {
        ESP_LOGW(TAG, "Systemzeit nicht synchronisiert (ts=%lld) — Zeit nicht übertragen",
                 (long long)now);
    }

    ESP_LOGI(TAG, "Polling every 2s...");

    uint8_t pkt_buf[PACKET_MAX_SIZE];

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        /* --- Statistiken lesen --- */
        uint8_t stats_buf[4];
        uint32_t total_recv = 0, total_overwr = 0;

        if (i2c_read_reg(I2C_REG_STATS_RECV, stats_buf, sizeof(stats_buf)) == ESP_OK) {
            total_recv = stats_buf[0] | ((uint32_t)stats_buf[1] << 8) |
                         ((uint32_t)stats_buf[2] << 16) | ((uint32_t)stats_buf[3] << 24);
        }
        if (i2c_read_reg(I2C_REG_STATS_OVERWR, stats_buf, sizeof(stats_buf)) == ESP_OK) {
            total_overwr = stats_buf[0] | ((uint32_t)stats_buf[1] << 8) |
                           ((uint32_t)stats_buf[2] << 16) | ((uint32_t)stats_buf[3] << 24);
        }

        /* --- Anzahl verfügbarer Pakete --- */
        uint8_t count = 0;
        if (i2c_read_reg(I2C_REG_COUNT, &count, 1) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read count");
            continue;
        }

        ESP_LOGI(TAG, "--- Packets: %d  recv=%lu  overwr=%lu ---",
                 count, total_recv, total_overwr);

        /* --- Alle Pakete abrufen --- */
        for (int i = 0; i < count; i++) {
            memset(pkt_buf, 0, sizeof(pkt_buf));

            /* Paket triggern und lesen */
            if (i2c_read_reg(I2C_REG_PACKET_READ, pkt_buf, sizeof(pkt_buf)) != ESP_OK) {
                ESP_LOGW(TAG, "Packet %d: read failed", i + 1);
                break;
            }

            /* Plausibilitätsprüfung: payload_len muss im gültigen Bereich sein */
            packet_header_t *hdr = (packet_header_t *)pkt_buf;
            if (hdr->payload_len > MAX_PAYLOAD_SIZE) {
                ESP_LOGW(TAG, "Packet %d: invalid payload_len=%d", i + 1, hdr->payload_len);
                break;
            }

            ESP_LOGI(TAG, "Packet %d/%d:", i + 1, count);
            print_packet(pkt_buf, PACKET_MIN_SIZE + hdr->payload_len);
        }
    }
}
