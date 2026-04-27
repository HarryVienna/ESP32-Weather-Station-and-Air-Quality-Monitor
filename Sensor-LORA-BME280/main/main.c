/**
 * @file main.c
 * @brief LoRa Sensor Sender — Heltec WiFi LoRa 32 V3 (ESP32-S3 / SX1262)
 *
 * Boot paths:
 *   Reset → config menu on OLED → measure → send → (deep sleep)
 *   Deep sleep wake → load config from NVS → measure → send → (deep sleep)
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_sleep.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "u8g2_esp32_hal.h"
#include "sx1262.h"
#include "bme280_sensor.h"

#include "../common/packet_format.h"
#include "config.h"

/* ============================================================================
 * Constants
 * ============================================================================ */

enum MessageType { PAIRING_REQ, PAIRING_RESP, DATA };

#define SLEEP_TIME_SECONDS 600

/* LED */
#define PIN_LED      GPIO_NUM_35

/* Heltec V4 FEM / power pins */
#define PIN_VEXT     GPIO_NUM_36  // Display power (LOW = on)
// #define PIN_VFEM     GPIO_NUM_7   // Amplifier power (HIGH = on)
// #define PIN_PA_CSD   GPIO_NUM_2   // GC1109 chip shutdown (HIGH = enabled)
// #define PIN_PA_CPS   GPIO_NUM_46  // GC1109 RX/TX path (HIGH = high-power TX)

/* Display (SSD1306 128x64) */
#define DISPLAY_SDA  GPIO_NUM_17
#define DISPLAY_SCL  GPIO_NUM_18
#define DISPLAY_RST  GPIO_NUM_21

/* ADC — GPIO37 = ADC_CTRL (HIGH enables voltage divider)
 *         GPIO1  = VBAT_Read = ADC1_CH0
 *         Divider: 390k : 100k = 4.9×                      */
#define ADC_CTRL       GPIO_NUM_37
#define ADC_CHANNEL    ADC_CHANNEL_0
#define ADC_UNIT       ADC_UNIT_1
#define ADC_ATTEN      ADC_ATTEN_DB_2_5
#define ADC_VBAT_R1    390.0f
#define ADC_VBAT_R2    100.0f
#define ADC_VBAT_CALIB (4076.0f / 4204.0f)   // empirical correction

/* BME280 I2C bus */
#define I2C_SDA     GPIO_NUM_48
#define I2C_SCL     GPIO_NUM_47

/* UART */
#define UART0_RXD     GPIO_NUM_44
#define UART0_TXD     GPIO_NUM_43

/* LoRa (SX1262 / GC1109) — settings that never change */
#define LORA_FREQUENCY        869525000UL  // 869.525 MHz — G3 band centre
#define LORA_BANDWIDTH        LORA_BW_125
#define LORA_CODING_RATE      LORA_CR_4_5
#define LORA_PREAMBLE_LENGTH  8
#define LORA_PAYLOAD_LENGTH   0
#define LORA_CRC_ON           true
#define LORA_IQ_INVERTED      true
#define LORA_RX_GAIN_BOOSTED  true
#define LORA_SYNC_WORD        0x1424

static const char *TAG = "main";

/* ============================================================================
 * LED
 * ============================================================================ */

static void blink(void) {
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << PIN_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(PIN_LED, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LED, 0);
}

/* ============================================================================
 * Board init
 * ============================================================================ */

/**
 * Configure Heltec V3 FEM and optionally the display power rail.
 * Pass enable_display=false on deep-sleep wake to skip VEXT.
 */
static esp_err_t init_heltec_v3(bool enable_display) {
    // Release deep sleep IO hold (no-op on first boot)
    gpio_deep_sleep_hold_dis();

    // FEM pins
    // gpio_config_t io_conf = {
    //     .pin_bit_mask = (1ULL << PIN_VFEM | 1ULL << PIN_PA_CSD | 1ULL << PIN_PA_CPS),
    //     .mode = GPIO_MODE_OUTPUT,
    //     .pull_up_en = GPIO_PULLUP_DISABLE,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .intr_type = GPIO_INTR_DISABLE,
    // };
    // gpio_config(&io_conf);
    // gpio_set_level(PIN_PA_CSD, 1);
    // gpio_set_level(PIN_PA_CPS, 1);
    // gpio_set_level(PIN_VFEM,   1);

    // VEXT: only on when display is needed (BME280 runs on permanent 3.3V)
    gpio_config_t vext_conf = {
        .pin_bit_mask = (1ULL << PIN_VEXT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&vext_conf);
    gpio_set_level(PIN_VEXT, enable_display ? 0 : 1);

    if (enable_display) {
        vTaskDelay(pdMS_TO_TICKS(200));   // VEXT rail stabilisation
    }

    ESP_LOGI(TAG, "Heltec V3 init (display=%s)", enable_display ? "on" : "off");
    return ESP_OK;
}

/* ============================================================================
 * Display
 * ============================================================================ */

static esp_err_t init_display(u8g2_t *u8g2) {
    u8g2_esp32_hal_t hal = U8G2_ESP32_HAL_DEFAULT;
    hal.bus.i2c.sda = DISPLAY_SDA;
    hal.bus.i2c.scl = DISPLAY_SCL;
    hal.reset        = DISPLAY_RST;
    u8g2_esp32_hal_init(hal);

    vTaskDelay(pdMS_TO_TICKS(100));

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        u8g2, U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&u8g2->u8x8, 0x3C << 1);

    u8g2_InitDisplay(u8g2);
    u8g2_SetPowerSave(u8g2, 0);

    ESP_LOGI(TAG, "OLED initialized");
    return ESP_OK;
}

/* ============================================================================
 * ADC
 * ============================================================================ */

static esp_err_t get_voltage(uint32_t *voltage) {
    int adc_raw;

    gpio_set_direction(ADC_CTRL, GPIO_MODE_OUTPUT);
    gpio_set_level(ADC_CTRL, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = { .unit_id = ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));

    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT,
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle));

    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw));
    ESP_LOGI(TAG, "ADC raw: %d", adc_raw);

    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, adc_raw, (int *)voltage));

    *voltage = (uint32_t)(*voltage * (ADC_VBAT_R1 + ADC_VBAT_R2) / ADC_VBAT_R2 * ADC_VBAT_CALIB);
    ESP_LOGI(TAG, "Battery: %lu mV", *voltage);

    gpio_set_level(ADC_CTRL, 0);

    adc_cali_delete_scheme_curve_fitting(cali_handle);
    adc_oneshot_del_unit(adc_handle);
    return ESP_OK;
}

/* ============================================================================
 * LoRa
 * ============================================================================ */

static esp_err_t init_lora(int8_t tx_power, uint8_t sf) {
    if (sx1262_init_bus()   != ESP_OK) return ESP_FAIL;
    if (sx1262_init_radio() != ESP_OK) return ESP_FAIL;

    sx1262_config_t config = {
        .modem_mode       = SX1262_MODEM_LORA,
        .frequency        = LORA_FREQUENCY,
        .tx_power         = tx_power,
        .bandwidth        = LORA_BANDWIDTH,
        .spreading_factor = sf,
        .coding_rate      = LORA_CODING_RATE,
        .iq_inverted      = LORA_IQ_INVERTED,
        .rx_gain_boosted  = LORA_RX_GAIN_BOOSTED,
        .preamble_length  = LORA_PREAMBLE_LENGTH,
        .payload_length   = LORA_PAYLOAD_LENGTH,
        .crc_on           = LORA_CRC_ON,
        .sync_word        = LORA_SYNC_WORD,
    };

    if (sx1262_configure(&config) != ESP_OK) return ESP_FAIL;

    ESP_LOGI(TAG, "LoRa: SF%d BW125 %d dBm @ %lu MHz",
             sf, tx_power, LORA_FREQUENCY / 1000000);
    return ESP_OK;
}

/* ============================================================================
 * Deep sleep
 * ============================================================================ */

static void start_deep_sleep(void) {

    // DEN SPI-GLITCH VERHINDERN
    // NSS Pin hart auf HIGH zwingen
    gpio_set_level(LORA_PIN_NSS, 1); 
    gpio_set_direction(LORA_PIN_NSS, GPIO_MODE_OUTPUT);

    // RESTLICHE LORA PINS ISOLIEREN
    gpio_set_direction(LORA_PIN_SCK, GPIO_MODE_INPUT);  // SCK
    gpio_set_direction(LORA_PIN_MOSI, GPIO_MODE_INPUT); // MOSI
    gpio_set_direction(LORA_PIN_MISO, GPIO_MODE_INPUT); // MISO
    
    gpio_set_direction(LORA_PIN_RST, GPIO_MODE_OUTPUT); // RST
    gpio_set_level(LORA_PIN_RST, 1); // HIGH lassen!

    // VEXT (OLED) hart aus
    gpio_set_direction(PIN_VEXT, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_VEXT, 1); 

    // UART / USB-Backfeeding isolieren
    gpio_set_direction(UART0_TXD, GPIO_MODE_INPUT);
    gpio_pullup_dis(UART0_TXD);
    gpio_set_direction(UART0_RXD, GPIO_MODE_INPUT);
    gpio_pullup_dis(UART0_RXD);

    // ALLES EINFRIEREN UND SCHLAFEN
    gpio_deep_sleep_hold_en();
    
    esp_sleep_enable_timer_wakeup(1000000ULL * SLEEP_TIME_SECONDS);

    ESP_LOGI(TAG, "Deep sleep...");
    esp_deep_sleep_start();

}

/* ============================================================================
 * app_main
 * ============================================================================ */

void app_main(void) {
 
      bool show_menu = (esp_reset_reason() != ESP_RST_DEEPSLEEP);

    ESP_LOGI(TAG, "Boot: %s", show_menu ? "reset → config menu" : "timer wake");

    /* NVS — required by config module */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition wiped, reinitialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Load persistent config (fills defaults on first boot) */
    sensor_config_t cfg;
    config_load(&cfg);

    u8g2_t u8g2;

    init_heltec_v3(show_menu);                                                                                                                                                                                                                                                                                                                   

    if (show_menu) {
        ESP_ERROR_CHECK(init_display(&u8g2));
        config_run_menu(&u8g2, &cfg);
        /* config_run_menu saves to NVS before returning */
    }

    /* ── Measure ── */

    uint32_t voltage = 0;
    get_voltage(&voltage);

    bme280_data_t bme280_data = {0};
    bme280_sensor_t bme280    = {0};
    const bme280_config_t bme280_conf = {
        .sda_pin = I2C_SDA,
        .scl_pin = I2C_SCL,
        .osr_p   = BME280_OVERSAMPLING_1X,
        .osr_t   = BME280_OVERSAMPLING_1X,
        .osr_h   = BME280_OVERSAMPLING_1X,
        .filter  = BME280_FILTER_COEFF_OFF,
        .dev_id  = BME280_I2C_ADDR_PRIM,
    };

    if (bme280_sensor_init(&bme280, &bme280_conf) != ESP_OK) {
        ESP_LOGE(TAG, "BME280 init failed");
    } else if (bme280_sensor_read(&bme280, &bme280_data) != ESP_OK) {
        ESP_LOGE(TAG, "BME280 read failed");
    } else {
        ESP_LOGI(TAG, "BME280: %.2f°C / %.2f%% / %.2f hPa",
                 bme280_data.temperature, bme280_data.humidity, bme280_data.pressure);
    }

    /* ── Send ── */
    ESP_ERROR_CHECK(init_lora(cfg.tx_power, cfg.spreading_factor));

    lora_sensor_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.header.msg_type    = DATA;
    packet.header.sensor_nr   = cfg.sensor_nr;
    packet.header.sensor_type = SENSOR_TYPE_BME280;

    uint8_t off = 0;
    memcpy(&packet.payload[off], &voltage,                  sizeof(uint32_t)); off += sizeof(uint32_t);
    memcpy(&packet.payload[off], &bme280_data.pressure,     sizeof(float));    off += sizeof(float);
    memcpy(&packet.payload[off], &bme280_data.temperature,  sizeof(float));    off += sizeof(float);
    memcpy(&packet.payload[off], &bme280_data.humidity,     sizeof(float));    off += sizeof(float);
    packet.header.payload_len = off;

    uint32_t total_len = sizeof(packet_header_t) + packet.header.payload_len;
    ESP_LOGI(TAG, "TX: sensor=%d len=%lu", cfg.sensor_nr, total_len);

    esp_err_t send_err = sx1262_send((uint8_t *)&packet, total_len);

    if (send_err == ESP_OK) {
        ESP_LOGI(TAG, "LoRa send OK");
    } else {
        ESP_LOGE(TAG, "LoRa send failed: %s", esp_err_to_name(send_err));
    }
    sx1262_sleep();

    blink();

    start_deep_sleep();
}
