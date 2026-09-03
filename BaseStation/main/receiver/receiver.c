#include "receiver.h"
#include "i2c/i2c_manager.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#include "gui/sensors/gui_sensors.h"
#include "nvs/preferences.h"

#include "../../common/packet_format.h"

static const char *TAG = "RECEIVER";

/* Slave */
#define I2C_SLAVE_ADDR  0x38
#define I2C_SLAVE_FREQ  50000

/* Register map (must match i2c_slave.h on the S3 side) */
#define I2C_REG_COUNT        0x00
#define I2C_REG_PACKET_READ  0x01
#define I2C_REG_SET_TIME     0x10
#define I2C_REG_SET_TZ       0x11
#define I2C_REG_RESET_DROP   0x23
#define I2C_REG_STATS_RECV   0x24
#define I2C_REG_STATS_OVERWR 0x28

/* Receiver firmware OTA (see Sensor-Receiver/main/i2c/i2c_slave.h) */
#define I2C_REG_SET_WIFI_SSID 0x12
#define I2C_REG_SET_WIFI_PASS 0x13
#define I2C_REG_OTA_START     0x14

/* Delay between WRITE (register) and READ (data) in ms */
#define I2C_SLAVE_PREPARE_MS  50

static i2c_master_dev_handle_t s_dev = NULL;

static const i2c_device_config_t s_dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = I2C_SLAVE_ADDR,
    .scl_speed_hz    = I2C_SLAVE_FREQ,
    .scl_wait_us     = 1000000,  /* 1s hardware SCL timeout — S3 is allowed to stretch */
};

/* -------------------------------------------------------------------------- */

static esp_err_t i2c_read_reg(uint8_t reg, uint8_t *buf, size_t len)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_transmit(s_dev, &reg, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WRITE reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(I2C_SLAVE_PREPARE_MS));
    ret = i2c_master_receive(s_dev, buf, len, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "READ reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

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
    esp_err_t ret = i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SET_TIME failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t i2c_set_timezone(const char *tz)
{
    size_t tz_len = strlen(tz);
    if (tz_len > 63) {
        ESP_LOGE(TAG, "Timezone string too long (%zu > 63)", tz_len);
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buf[65];
    buf[0] = I2C_REG_SET_TZ;
    memcpy(&buf[1], tz, tz_len + 1);
    esp_err_t ret = i2c_master_transmit(s_dev, buf, 1 + tz_len + 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SET_TZ failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* Generic "register byte + NUL-terminated string" write, used for the OTA
 * WiFi credential registers (SET_WIFI_SSID/_PASS) - same wire format as
 * i2c_set_timezone() above, just for an arbitrary register/max length. */
static esp_err_t i2c_write_str(uint8_t reg, const char *str, size_t max_len)
{
    size_t len = strlen(str);
    if (len > max_len) {
        ESP_LOGE(TAG, "Reg 0x%02X: string too long (%zu > %zu)", reg, len, max_len);
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buf[1 + 64 + 1]; /* longest payload so far: password (63) + NUL */
    buf[0] = reg;
    memcpy(&buf[1], str, len + 1);
    esp_err_t ret = i2c_master_transmit(s_dev, buf, 1 + len + 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WRITE reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

/* -------------------------------------------------------------------------- */

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
        case 2: return "SHT45";
        case 3: return "GEIGER";
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

    if (pkt->header.payload_len > 0 && pkt->header.payload_len <= MAX_PAYLOAD_SIZE) {
        switch (pkt->header.sensor_type) {
            case SENSOR_TYPE_BME280: {
                if (pkt->header.payload_len >= sizeof(bme280_payload_t)) {
                    const bme280_payload_t *d = (const bme280_payload_t *)pkt->payload;
                    ESP_LOGI(TAG, "  Temp=%.2f°C  Hum=%.2f%%  Press=%.2f hPa  Voltage=%lumV",
                             d->temperature, d->humidity, d->pressure, (unsigned long)d->voltage);
                }
                break;
            }
            case SENSOR_TYPE_SHT45: {
                if (pkt->header.payload_len >= sizeof(sht45_payload_t)) {
                    const sht45_payload_t *d = (const sht45_payload_t *)pkt->payload;
                    ESP_LOGI(TAG, "  Temp=%.2f°C  Hum=%.2f%%  Voltage=%lumV",
                             d->temperature, d->humidity, (unsigned long)d->voltage);
                }
                break;
            }            
            case SENSOR_TYPE_GEIGER: {
                if (pkt->header.payload_len >= sizeof(geiger_payload_t)) {
                    const geiger_payload_t *d = (const geiger_payload_t *)pkt->payload;
                    ESP_LOGI(TAG, "  uSv/h=%.3f  CPM=%.1f  Voltage=%lumV",
                             d->usvh, d->cpm, (unsigned long)d->voltage);
                }
                break;
            }
            default: {
                char hex[3 * MAX_PAYLOAD_SIZE + 1];
                size_t pos = 0;
                for (int i = 0; i < pkt->header.payload_len; i++) {
                    pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", pkt->payload[i]);
                }
                ESP_LOGI(TAG, "  Payload: %s", hex);
                break;
            }
        }
    }
}

/* Tick rate of the sensor receiver (S3, see Sensor-Receiver/sdkconfig:
 * CONFIG_FREERTOS_HZ=100), NOT of the BaseStation (P4: CONFIG_FREERTOS_HZ=1000,
 * different chip, different tick rate). link.timestamp in sensor_packet_t is
 * an xTaskGetTickCount() value from the S3 (see esp-now.c/lora.c there) -
 * using portTICK_PERIOD_MS here would be off by a factor of 10. */
#define SENSOR_RECEIVER_TICK_MS 10

/* Watchdog: detects sensors that haven't sent anything for longer than
 * expected. The receiver doesn't know fixed send intervals for the
 * transmitters (they sleep for different durations depending on
 * type/configuration) - so the interval per sensor is derived from the
 * time between the last two received packets. Only from a sensor's second
 * packet onward is there a reference value; before that (and if no packet
 * has ever arrived) no "offline" check is done. The timeout is 3x the
 * most recently measured interval - this way it automatically adapts to
 * each sensor.
 *
 * The interval is calculated from link.timestamp (S3 receive time), NOT
 * from BaseStation processing time: the S3 buffers packets in a queue
 * that the BaseStation only drains on the next I2C poll (every 2s, see
 * receiver_task) - if several packets are backlogged there (e.g. because
 * the S3 had already been running for a while before the BaseStation
 * started polling), they get processed within a few milliseconds of each
 * other even though they arrived over the air minutes apart. Using
 * BaseStation processing time for the interval would corrupt the learned
 * reference down to a much-too-short value and produce false "offline"
 * reports. */
typedef struct {
    int64_t  last_seen_us;      /* 0 = never received yet (BaseStation time, for the "how long ago" check in watchdog_check_all) */
    uint32_t last_pkt_tick;     /* xTaskGetTickCount() of the sensor receiver at the last packet */
    int64_t  last_interval_us;  /* 0 = no second packet yet, no reference value */
    bool     offline;
} sensor_watchdog_t;

static sensor_watchdog_t s_watchdog[SENSOR_SLOT_COUNT];

/* Called for every valid packet from update_sensor_display(). Updates the
 * timestamp/interval and reports back whether the card was currently
 * marked "offline" (so the caller can restore its normal color while
 * still holding the existing lvgl lock). */
static bool watchdog_note_packet(uint8_t sensor_nr, uint32_t pkt_tick)
{
    if (sensor_nr >= SENSOR_SLOT_COUNT) {
        return false;
    }
    sensor_watchdog_t *wd = &s_watchdog[sensor_nr];
    int64_t now = esp_timer_get_time();

    if (wd->last_seen_us != 0) {
        // Unsigned subtraction automatically handles a tick overflow
        // (every ~497 days at 10ms/tick) correctly.
        uint32_t delta_ticks = pkt_tick - wd->last_pkt_tick;
        wd->last_interval_us = (int64_t)delta_ticks * SENSOR_RECEIVER_TICK_MS * 1000;
    }
    wd->last_seen_us = now;
    wd->last_pkt_tick = pkt_tick;

    bool was_offline = wd->offline;
    wd->offline = false;
    return was_offline;
}

/* Called once per receiver_task cycle: checks all slots against their
 * individual timeout (3x the last measured interval) and colors the
 * header row red as soon as it's exceeded. */
static void watchdog_check_all(void)
{
    int64_t now = esp_timer_get_time();

    for (uint8_t i = 0; i < SENSOR_SLOT_COUNT; i++) {
        sensor_watchdog_t *wd = &s_watchdog[i];

        if (wd->offline || wd->last_seen_us == 0 || wd->last_interval_us == 0) {
            continue;  /* never seen yet, or no reference interval known yet */
        }

        int64_t timeout_us = 3 * wd->last_interval_us;
        if (now - wd->last_seen_us > timeout_us) {
            wd->offline = true;
            ESP_LOGW(TAG, "Sensor %d: seit %lld s keine Daten mehr (erwartet alle ~%lld s) -> offline",
                     i, (long long)((now - wd->last_seen_us) / 1000000),
                     (long long)(wd->last_interval_us / 1000000));
            disp_sensor_offline(i, true);
        }
    }
}

/* -------------------------------------------------------------------------- */

/* Updates readings as well as the battery/signal icon of the sensor card.
 * Each sensor type is handled entirely in its own case (checking payload
 * length, type-specific cast, reading "voltage" from the matching field) -
 * deliberately no shared generic memcpy across all types, so a future
 * payload type with "voltage" at a different offset (or none at all) can
 * be added without touching the other types. */
static void update_sensor_display(const sensor_packet_t *pkt)
{
    uint32_t voltage_mv;

    switch (pkt->header.sensor_type) {
        case SENSOR_TYPE_BME280: {
            if (pkt->header.payload_len < sizeof(bme280_payload_t)) {
                return;
            }
            const bme280_payload_t *d = (const bme280_payload_t *)pkt->payload;
            voltage_mv = d->voltage;
            break;
        }
        case SENSOR_TYPE_SHT45: {
            if (pkt->header.payload_len < sizeof(sht45_payload_t)) {
                return;
            }
            const sht45_payload_t *d = (const sht45_payload_t *)pkt->payload;
            voltage_mv = d->voltage;
            break;
        }
        case SENSOR_TYPE_GEIGER: {
            if (pkt->header.payload_len < sizeof(geiger_payload_t)) {
                return;
            }
            const geiger_payload_t *d = (const geiger_payload_t *)pkt->payload;
            voltage_mv = d->voltage;
            break;
        }
        default:
            return;
    }

    bool was_offline = watchdog_note_packet(pkt->header.sensor_nr, pkt->link.timestamp);

    if (was_offline) {
        disp_sensor_offline(pkt->header.sensor_nr, false);
    }
    disp_sensor_link_quality(pkt->header.sensor_nr, voltage_mv, pkt->link.rssi);
    disp_sensor_values(pkt->header.sensor_nr, (sensor_type_t)pkt->header.sensor_type, pkt->payload);
}

/* -------------------------------------------------------------------------- */

void receiver_sync_time(void)
{
    // Return values deliberately ignored below: if I2C communication with
    // the S3 fails (e.g. because it's not ready yet at boot), this should
    // only be logged, not make the caller fail - receiver_init() propagates
    // failures via ESP_ERROR_CHECK() in main.c, which would restart the
    // whole device every time the S3 boots slowly.
    if (s_dev == NULL) {
        return;
    }

    time_t now = time(NULL);
    if (now <= 1000000000) {
        ESP_LOGW(TAG, "System time not synchronized - not sending to slave");
        return;
    }

    // Read fresh from NVS every call (not just once) so a timezone change
    // in the setup screen reaches the S3 on the next sync, without a
    // separate "timezone changed" hook - see gui/setup/
    // gui_setup_screen_actions.c: save_region_and_timezone().
    //
    // Only proceed if "tz" is actually set in NVS - before the user has
    // gone through setup once there's no real timezone to report, and the
    // slave shouldn't be told a made-up default it never confirmed.
    nvs_handle_t nvs_handle;
    if (nvs_open("weatherstation", NVS_READONLY, &nvs_handle) != ESP_OK) {
        ESP_LOGW(TAG, "NVS not available - not sending to slave");
        return;
    }
    char *tz = get_string_from_nvs(nvs_handle, "tz", NULL);
    nvs_close(nvs_handle);
    if (tz == NULL) {
        ESP_LOGW(TAG, "No timezone configured yet - not sending to slave");
        return;
    }

    i2c_set_timezone(tz);
    i2c_set_time(now);
    ESP_LOGI(TAG, "Time + timezone sent to slave (ts=%lld, tz=%s)", (long long)now, tz);

    free(tz);
}

esp_err_t receiver_init(void)
{
    ESP_LOGI(TAG, "Slave 0x%02X @ %d Hz", I2C_SLAVE_ADDR, I2C_SLAVE_FREQ);

    if (i2c_master_probe(i2c_manager_get_bus(), I2C_SLAVE_ADDR, 20) != ESP_OK) {
        ESP_LOGE(TAG, "Slave 0x%02X NICHT gefunden!", I2C_SLAVE_ADDR);
    }

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_manager_get_bus(), &s_dev_cfg, &s_dev),
                        TAG, "Failed to add slave device");

    return ESP_OK;
}

esp_err_t receiver_start_ota(const char *ssid, const char *password)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Starting receiver OTA (SSID: '%s')", ssid);

    if ((ret = i2c_write_str(I2C_REG_SET_WIFI_SSID, ssid, 32)) != ESP_OK) {
        return ret;
    }
    if ((ret = i2c_write_str(I2C_REG_SET_WIFI_PASS, password, 63)) != ESP_OK) {
        return ret;
    }

    uint8_t buf[2] = { I2C_REG_OTA_START, 0x01 };
    ret = i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA_START failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void receiver_task(void *arg)
{
    ESP_LOGI(TAG, "Polling every 2s...");

    uint8_t pkt_buf[PACKET_MAX_SIZE];

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        watchdog_check_all();

        if (s_dev == NULL) {
            ESP_LOGW(TAG, "Device handle ungültig — überspringe Zyklus");
            continue;
        }

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

        uint8_t count = 0;
        if (i2c_read_reg(I2C_REG_COUNT, &count, 1) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read count");
            continue;
        }

        // ESP_LOGI(TAG, "--- Packets: %d  recv=%lu  overwr=%lu ---",
        //          count, total_recv, total_overwr);

        for (int i = 0; i < count; i++) {
            memset(pkt_buf, 0, sizeof(pkt_buf));

            if (i2c_read_reg(I2C_REG_PACKET_READ, pkt_buf, sizeof(pkt_buf)) != ESP_OK) {
                ESP_LOGW(TAG, "Packet %d: read failed", i + 1);
                break;
            }

            packet_header_t *hdr = (packet_header_t *)pkt_buf;
            if (hdr->payload_len > MAX_PAYLOAD_SIZE) {
                ESP_LOGW(TAG, "Packet %d: invalid payload_len=%d", i + 1, hdr->payload_len);
                break;
            }

            ESP_LOGI(TAG, "Packet %d/%d:", i + 1, count);
            print_packet(pkt_buf, PACKET_MIN_SIZE + hdr->payload_len);
            update_sensor_display((const sensor_packet_t *)pkt_buf);
        }
    }

    vTaskDelete(NULL);
}

void receiver_start(void)
{
    xTaskCreate(receiver_task, "receiver", 4096, NULL, 5, NULL);
}
