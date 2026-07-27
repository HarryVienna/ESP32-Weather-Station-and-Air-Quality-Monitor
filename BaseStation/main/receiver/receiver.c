#include "receiver.h"
#include "i2c/i2c_manager.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#include "gui/sensors/gui_sensors.h"

#include "../../common/packet_format.h"

static const char *TAG = "RECEIVER";

/* Slave */
#define I2C_SLAVE_ADDR  0x38
#define I2C_SLAVE_FREQ  50000

/* Register Map (muss zur i2c_slave.h auf S3-Seite passen) */
#define I2C_REG_COUNT        0x00
#define I2C_REG_PACKET_READ  0x01
#define I2C_REG_SET_TIME     0x10
#define I2C_REG_SET_TZ       0x11
#define I2C_REG_RESET_DROP   0x23
#define I2C_REG_STATS_RECV   0x24
#define I2C_REG_STATS_OVERWR 0x28

/* Zeitzone die an den S3-Slave übertragen wird */
#define SLAVE_TIMEZONE  "CET-1CEST,M3.5.0,M10.5.0/3"

/* Delay zwischen WRITE (register) und READ (data) in ms */
#define I2C_SLAVE_PREPARE_MS  50

static i2c_master_dev_handle_t s_dev = NULL;

static const i2c_device_config_t s_dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = I2C_SLAVE_ADDR,
    .scl_speed_hz    = I2C_SLAVE_FREQ,
    .scl_wait_us     = 1000000,  /* 1s hardware-SCL-timeout — S3 darf stretchen */
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

/* Tick-Rate des Sensor-Receivers (S3, siehe Sensor-Receiver/sdkconfig:
 * CONFIG_FREERTOS_HZ=100), NICHT der BaseStation (P4: CONFIG_FREERTOS_HZ=1000,
 * anderer Chip, andere Tick-Rate). link.timestamp in sensor_packet_t ist ein
 * xTaskGetTickCount()-Wert vom S3 (siehe esp-now.c/lora.c dort) - portTICK_PERIOD_MS
 * hier zu verwenden waere um Faktor 10 falsch. */
#define SENSOR_RECEIVER_TICK_MS 10

/* Watchdog: erkennt Sensoren, von denen laenger nichts mehr kam als erwartet.
 * Der Receiver kennt keine festen Sende-Intervalle der Sender (die schlafen
 * unterschiedlich lange, je nach Typ/Konfiguration) - deshalb wird das
 * Intervall pro Sensor aus der Zeit zwischen den letzten zwei empfangenen
 * Paketen selbst ermittelt. Erst ab dem zweiten Paket eines Sensors gibt es
 * einen Referenzwert; vorher (und wenn noch nie ein Paket kam) wird nicht auf
 * "offline" geprueft. Als Timeout gilt das 3-fache des zuletzt gemessenen
 * Intervalls - passt sich damit automatisch an jeden Sensor an.
 *
 * Das Intervall wird aus link.timestamp (S3-Empfangszeit) berechnet, NICHT aus
 * der BaseStation-Verarbeitungszeit: der S3 puffert Pakete in einer Queue, die
 * die BaseStation erst beim naechsten I2C-Poll ausliest (alle 2s, siehe
 * receiver_task) - liegen dabei mehrere Pakete im Backlog (z.B. weil der S3
 * schon laenger lief, bevor die BaseStation zu pollen begann), werden die
 * innerhalb weniger Millisekunden hintereinander verarbeitet, obwohl sie per
 * Funk Minuten auseinander ankamen. Das hat vorher die gelernte Referenz auf
 * einen viel zu kurzen Wert verfaelscht -> false "offline"-Meldungen. */
typedef struct {
    int64_t  last_seen_us;      /* 0 = noch nie empfangen (BaseStation-Zeit, fuer den "wie lange her"-Check in watchdog_check_all) */
    uint32_t last_pkt_tick;     /* xTaskGetTickCount() des Sensor-Receivers beim letzten Paket */
    int64_t  last_interval_us;  /* 0 = noch kein zweites Paket, kein Referenzwert */
    bool     offline;
} sensor_watchdog_t;

static sensor_watchdog_t s_watchdog[SENSOR_SLOT_COUNT];

/* Bei jedem gueltigen Paket aus update_sensor_display() aufgerufen.
 * Aktualisiert Zeitstempel/Intervall und meldet zurueck, ob die Karte gerade
 * als "offline" markiert war (damit der Aufrufer sie unter dem bestehenden
 * lvgl-Lock wieder normalfarbig machen kann). */
static bool watchdog_note_packet(uint8_t sensor_nr, uint32_t pkt_tick)
{
    if (sensor_nr >= SENSOR_SLOT_COUNT) {
        return false;
    }
    sensor_watchdog_t *wd = &s_watchdog[sensor_nr];
    int64_t now = esp_timer_get_time();

    if (wd->last_seen_us != 0) {
        // Unsigned-Subtraktion behandelt einen Tick-Ueberlauf (alle ~497 Tage
        // bei 10ms/Tick) automatisch korrekt.
        uint32_t delta_ticks = pkt_tick - wd->last_pkt_tick;
        wd->last_interval_us = (int64_t)delta_ticks * SENSOR_RECEIVER_TICK_MS * 1000;
    }
    wd->last_seen_us = now;
    wd->last_pkt_tick = pkt_tick;

    bool was_offline = wd->offline;
    wd->offline = false;
    return was_offline;
}

/* Einmal pro receiver_task-Zyklus aufgerufen: prueft alle Slots gegen ihr
 * individuelles Timeout (3x letztes gemessenes Intervall) und faerbt die
 * Kopfzeile rot, sobald es ueberschritten wird. */
static void watchdog_check_all(void)
{
    int64_t now = esp_timer_get_time();

    for (uint8_t i = 0; i < SENSOR_SLOT_COUNT; i++) {
        sensor_watchdog_t *wd = &s_watchdog[i];

        if (wd->offline || wd->last_seen_us == 0 || wd->last_interval_us == 0) {
            continue;  /* noch nie gesehen bzw. noch kein Referenzintervall bekannt */
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

/* Aktualisiert Messwerte sowie Batterie-/Signal-Icon der Sensor-Karte. Jeder
 * Sensor-Typ wird komplett in seinem eigenen case behandelt (Payload-Laenge
 * pruefen, typspezifisch casten, "voltage" aus dem passenden Feld lesen) -
 * bewusst kein gemeinsamer generischer memcpy ueber alle Typen hinweg, damit
 * ein zukuenftiger Payload-Typ mit "voltage" an anderer Stelle (oder ganz
 * ohne Spannung) problemlos ergaenzt werden kann, ohne die anderen Typen
 * anzufassen. */
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

static void i2c_scan(void)
{
    ESP_LOGI(TAG, "=== I2C Bus Scan ===");
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(i2c_manager_get_bus(), addr, 20) == ESP_OK) {
            ESP_LOGI(TAG, "  Device found at 0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGW(TAG, "  Kein Gerät gefunden — Verdrahtung prüfen!");
    }
    if (i2c_master_probe(i2c_manager_get_bus(), I2C_SLAVE_ADDR, 20) != ESP_OK) {
        ESP_LOGE(TAG, "  Slave 0x%02X NICHT gefunden!", I2C_SLAVE_ADDR);
    }
    ESP_LOGI(TAG, "===================");
}

esp_err_t receiver_init(void)
{
    ESP_LOGI(TAG, "Slave 0x%02X @ %d Hz", I2C_SLAVE_ADDR, I2C_SLAVE_FREQ);

    i2c_scan();

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_manager_get_bus(), &s_dev_cfg, &s_dev),
                        TAG, "Failed to add slave device");

    // Rueckgabewerte bewusst ignoriert: schlaegt die I2C-Kommunikation mit
    // dem S3 fehl (z.B. weil der beim Boot noch nicht bereit ist), soll das
    // nur geloggt werden, nicht receiver_init() fehlschlagen lassen - sonst
    // wuerde ESP_ERROR_CHECK(receiver_init()) in main.c das ganze Geraet bei
    // jedem langsam bootenden S3 neu starten.
    i2c_set_timezone(SLAVE_TIMEZONE);

    time_t now = time(NULL);
    if (now > 1000000000) {
        i2c_set_time(now);
        ESP_LOGI(TAG, "Zeit + Timezone an Slave übertragen (ts=%lld, tz=%s)",
                 (long long)now, SLAVE_TIMEZONE);
    } else {
        ESP_LOGW(TAG, "Systemzeit nicht synchronisiert — Zeit nicht übertragen");
    }

    return ESP_OK;
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
