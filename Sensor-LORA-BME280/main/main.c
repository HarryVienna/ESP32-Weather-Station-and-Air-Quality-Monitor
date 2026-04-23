/**
 * @file main.c
 * @brief LoRa Sensor Sender with Heltec WiFi LoRa 32 V4
 * 
 * This firmware reads sensor data (BME280 + ADC voltage), packages it into
 * a LoRa packet, and transmits it via SX1262. The device uses deep sleep
 * between readings (600 second intervals).
 * 
 * Hardware: Heltec WiFi LoRa 32 V4 (GC1109 amplifier)
 * Display:  SSD1306 OLED 128x64 via I2C
 * Sensor:  BME280 via I2C with DIP switch for sensor number
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_sleep.h"
#include "esp_log.h"

#include "u8g2_esp32_hal.h"

#include "sx1262.h"

#include "bme280_sensor_driver.h"

/* Common packet format - shared with receiver project */
#include "../common/packet_format.h"

#define SLEEP_TIME_SECONDS 600

enum MessageType {
    PAIRING_REQ,
    PAIRING_RESP,
    DATA,
};

/* Heltec V4 FEM pins */
#define PIN_VEXT     GPIO_NUM_36  // Display power (LOW = on)
#define PIN_VFEM     GPIO_NUM_7   // Amplifier power (HIGH = on)
#define PIN_PA_CSD   GPIO_NUM_2   // Chip Shut Down (HIGH = enable GC1109)
#define PIN_PA_CPS   GPIO_NUM_46  // RX/TX Path Control (HIGH = High-Power TX)

/* Display pins */
#define DISPLAY_SDA  GPIO_NUM_17
#define DISPLAY_SCL  GPIO_NUM_18
#define DISPLAY_RST  GPIO_NUM_21

/* Sensor pins */
#define SENSOR_NR_0  GPIO_NUM_39
#define SENSOR_NR_1  GPIO_NUM_40
#define SENSOR_NR_2  GPIO_NUM_41

/* ADC - Heltec V3: GPIO37 = ADC_CTRL (HIGH = enable voltage divider) */
/*                  GPIO1  = VBAT_Read = ADC1_CH0                     */
/*                  Divider: 390k : 100k = 4.9x                       */
#define ADC_CTRL          GPIO_NUM_37
#define ADC_CHANNEL       ADC_CHANNEL_0
#define ADC_UNIT          ADC_UNIT_1
#define ADC_ATTEN         ADC_ATTEN_DB_2_5
#define ADC_VBAT_R1       390.0f   // kOhm
#define ADC_VBAT_R2       100.0f   // kOhm
#define ADC_VBAT_CALIB    (4076.0f / 4204.0f)  // empirical: measured 4058 mV, raw formula gave 4155 mV

/* I2C - Heltec V4 ESP32-S3: GPIO17/18 */
#define I2C_SDA      GPIO_NUM_48
#define I2C_SCL      GPIO_NUM_47
#define I2C_FREQ_HZ  100000

/* ============================================================================
 * LoRa Receiver Configuration
 * ============================================================================ */

#define LORA_FREQUENCY          869525000      // 869,525 MHz == Middle of G3 band
#define LORA_BANDWIDTH          LORA_BW_125
#define LORA_SPREADING_FACTOR   7
#define LORA_CODING_RATE        LORA_CR_4_5
#define LORA_TX_POWER           12             // dBm (RX mode, lower power OK)
#define LORA_PREAMBLE_LENGTH    8
#define LORA_PAYLOAD_LENGTH     0
#define LORA_CRC_ON             true
#define LORA_IQ_INVERTED        true
#define LORA_RX_GAIN_BOOSTED    true
#define LORA_SYNC_WORD          0x1424         // Public LoRa network



/* Display status timeout */
#define STATUS_DISPLAY_MS 3000

static const char* TAG = "main";

static i2c_master_bus_handle_t i2c_master_bus = NULL;

/**
 * @brief Initialize Heltec V4 board (FEM pins, display power)
 * 
 * Configures VEXT, VFEM, PA_CSD, and PA_CPS GPIOs to enable
 * the display and GC1109 amplifier for high-power LoRa transmission.
 */
static esp_err_t init_heltec_v4(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_VEXT | 1ULL << PIN_VFEM | 
                         1ULL << PIN_PA_CSD | 1ULL << PIN_PA_CPS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Enable display (LOW = on for VEXT)
    gpio_set_level(PIN_VEXT, 0);
    ESP_LOGI(TAG, "VEXT activated (display power)");

    // Enable GC1109 amplifier
    gpio_set_level(PIN_PA_CSD, 1);  // Chip Shut Down HIGH = enabled
    gpio_set_level(PIN_PA_CPS, 1);  // High-Power TX mode
    gpio_set_level(PIN_VFEM, 1);    // Amplifier power on
    ESP_LOGI(TAG, "GC1109 amplifier enabled (PA_CSD, PA_CPS, VFEM)");

    // Stabilization delay
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Heltec V4 board initialized");
    return ESP_OK;
}

/**
 * @brief Initialize OLED display (SSD1306 128x64)
 */
static esp_err_t init_display(u8g2_t* u8g2) {
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = DISPLAY_SDA;
    u8g2_esp32_hal.bus.i2c.scl = DISPLAY_SCL;
    u8g2_esp32_hal.reset = DISPLAY_RST;
    u8g2_esp32_hal_init(u8g2_esp32_hal);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        u8g2,
        U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb
    );
    u8x8_SetI2CAddress(&u8g2->u8x8, 0x3C << 1);
    
    u8g2_InitDisplay(u8g2);
    u8g2_SetPowerSave(u8g2, 0);
    
    ESP_LOGI(TAG, "OLED display initialized (SSD1306, I2C 0x3C)");
    return ESP_OK;
}

/**
 * @brief Display status message on OLED
 */
static void display_status(u8g2_t* u8g2, uint8_t sensor_nr, const char* msg, bool success) {
    char buf[16];
    
    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
    
    // Header line
    snprintf(buf, sizeof(buf), "Sensor %d", sensor_nr);
    u8g2_DrawStr(u8g2, 5, 20, buf);
    
    // Status line
    if (success) {
        u8g2_DrawStr(u8g2, 5, 35, "TX OK");
    } else {
        u8g2_DrawStr(u8g2, 5, 35, "FAIL");
    }
    
    // Info line
    u8g2_DrawStr(u8g2, 5, 50, msg);
    
    u8g2_SendBuffer(u8g2);
}

/**
 * @brief Get sensor number from DIP switch GPIO pins
 */
static esp_err_t get_sensor_number(uint8_t *nr) {
    gpio_set_direction(SENSOR_NR_0, GPIO_MODE_INPUT);   
    gpio_set_direction(SENSOR_NR_1, GPIO_MODE_INPUT);  
    gpio_set_direction(SENSOR_NR_2, GPIO_MODE_INPUT); 

    gpio_set_pull_mode(SENSOR_NR_0, GPIO_PULLUP_PULLDOWN);
    gpio_set_pull_mode(SENSOR_NR_1, GPIO_PULLUP_PULLDOWN);
    gpio_set_pull_mode(SENSOR_NR_2, GPIO_PULLUP_PULLDOWN);

    const int bit_0 = gpio_get_level(SENSOR_NR_0);
    const int bit_1 = gpio_get_level(SENSOR_NR_1);
    const int bit_2 = gpio_get_level(SENSOR_NR_2);

    *nr = (uint8_t)((bit_2 << 2) | (bit_1 << 1) | bit_0);

    return ESP_OK;
}

/**
 * @brief Get voltage value from ADC reading (ESP32-S3 ADC1)
 *
 * Uses curve fitting calibration for accurate voltage readings.
 * GPIO1 (VBAT_Read = ADC1_CH0)
 * Voltage divider: 390k : 100k (ratio 4.9x)
 */
static esp_err_t get_voltage(uint32_t *voltage) {
    int adc_raw;

    // Enable voltage divider
    gpio_set_direction(ADC_CTRL, GPIO_MODE_OUTPUT);
    gpio_set_level(ADC_CTRL, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &config));

    adc_cali_handle_t adc1_cali_chan0_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_chan0_handle));

    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL, &adc_raw));
    ESP_LOGI(TAG, "ADC1 Channel[%d] Raw: %d", ADC_CHANNEL, adc_raw);

    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw, (int*)voltage));
    ESP_LOGI(TAG, "ADC1 Channel[%d] Voltage: %lu mV", ADC_CHANNEL, *voltage);

    *voltage = (uint32_t)(*voltage * (ADC_VBAT_R1 + ADC_VBAT_R2) / ADC_VBAT_R2 * ADC_VBAT_CALIB);
    ESP_LOGI(TAG, "ADC1 Channel[%d] Battery: %lu mV", ADC_CHANNEL, *voltage);

    // Disable voltage divider to save power
    gpio_set_level(ADC_CTRL, 0);

    return ESP_OK;
}

/**
 * @brief Initialize I2C communication
 */
static void i2c_init(i2c_master_bus_config_t *bus_config) {
    ESP_ERROR_CHECK(i2c_new_master_bus(bus_config, &i2c_master_bus));
}

/**
 * @brief Initialize LoRa (SX1262/GC1109)
 */
static esp_err_t init_lora(void) {
    if (sx1262_init_bus() != ESP_OK) {
        ESP_LOGE(TAG, "SX1262 bus init failed");
        return ESP_FAIL;
    }
    
    if (sx1262_init_radio() != ESP_OK) {
        ESP_LOGE(TAG, "SX1262 radio init failed");
        return ESP_FAIL;
    }
    
    sx1262_config_t config = {
        .modem_mode = SX1262_MODEM_LORA,
        .frequency = LORA_FREQUENCY,
        .tx_power = LORA_TX_POWER,
        .bandwidth = LORA_BANDWIDTH,
        .spreading_factor = LORA_SPREADING_FACTOR,
        .coding_rate = LORA_CODING_RATE,
        .iq_inverted = LORA_IQ_INVERTED,
        .rx_gain_boosted = LORA_RX_GAIN_BOOSTED,
        .preamble_length = LORA_PREAMBLE_LENGTH,
        .payload_length = LORA_PAYLOAD_LENGTH,
        .crc_on = LORA_CRC_ON,
        .sync_word = LORA_SYNC_WORD
    };
    
    if (sx1262_configure(&config) != ESP_OK) {
        ESP_LOGE(TAG, "SX1262 configure failed");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "LoRa configured: SF%d BW%d Power%d @ %lu MHz",
             LORA_SPREADING_FACTOR, LORA_BANDWIDTH, LORA_TX_POWER, LORA_FREQUENCY / 1000000);
    
    return ESP_OK;
}

/**
 * @brief Configure deep sleep for SLEEP_TIME_SECONDS
 */
static void start_deep_sleep(void) {
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds...", SLEEP_TIME_SECONDS);
    
    // Disable power domain for lowest current
    esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);

    // Release DIP switch GPIOs for deep sleep
    gpio_reset_pin(SENSOR_NR_0);
    gpio_reset_pin(SENSOR_NR_1);
    gpio_reset_pin(SENSOR_NR_2);

    // Configure wake on RTC timer (default after reset)
    esp_deep_sleep(1000000ULL * SLEEP_TIME_SECONDS);
}

/**
 * @brief Main application entry point
 */
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "LoRa Sensor Sender starting...");
    ESP_LOGI(TAG, "========================================");

    // 1. Initialize Heltec V4 board (FEM, display power)
    ESP_ERROR_CHECK(init_heltec_v4());

    // 2. Initialize OLED display
    u8g2_t u8g2;
    ESP_ERROR_CHECK(init_display(&u8g2));
    display_status(&u8g2, 0, "Starting...", true);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 3. Read DIP switch for sensor number
    uint8_t sensor_nr;
    ESP_ERROR_CHECK(get_sensor_number(&sensor_nr));
    ESP_LOGI(TAG, "Sensor number: %d", sensor_nr);

    // 4. Read ADC voltage
    uint32_t voltage = 0;
    ESP_ERROR_CHECK(get_voltage(&voltage));

    // 5. Initialize I2C and read BME280 sensor
    ESP_LOGI(TAG, "Initializing I2C...");
    gpio_reset_pin(I2C_SDA);
    gpio_reset_pin(I2C_SCL);

    i2c_master_bus_config_t i2c_config = {
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL,
        .sda_io_num = I2C_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = 1,
    };

    i2c_init(&i2c_config);
    ESP_LOGI(TAG, "I2C initialized");

    bme280_set_i2c_bus(i2c_master_bus);

    sensor_data_t bme280_data;
    const sensor_driver_bme280_conf_t bme280_config = {
        .osr_p = BME280_OVERSAMPLING_1X,
        .osr_t = BME280_OVERSAMPLING_1X,
        .osr_h = BME280_OVERSAMPLING_1X,
        .filter = BME280_FILTER_COEFF_OFF,
        .dev_id = BME280_I2C_ADDR_PRIM
    };

    sensor_driver_t *bme280_driver = sensor_driver_new_bme280(&bme280_config);
    if (bme280_driver == NULL) {
        ESP_LOGE(TAG, "Failed to create BME280 driver");
    } else {
        if (sensor_driver_init_sensor(bme280_driver) == ESP_OK) {
            if (sensor_driver_read_values(bme280_driver, &bme280_data) == ESP_OK) {
                ESP_LOGI(TAG, "BME280: %.2f°C / %.2f%% / %.2fhPa",
                         bme280_data.temperature, bme280_data.humidity, bme280_data.pressure);
            } else {
                ESP_LOGE(TAG, "BME280 read failed");
            }
        } else {
            ESP_LOGE(TAG, "BME280 init failed");
        }
        // Note: sensor_driver_delete() is not implemented in sensor_driver.c
    }

    // 6. Initialize LoRa
    ESP_LOGI(TAG, "Initializing LoRa...");
    ESP_ERROR_CHECK(init_lora());

    // 7. Build LoRa packet
    lora_sensor_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.header.msg_type = DATA;  // DATA message type
    packet.header.sensor_nr = sensor_nr;
    packet.header.sensor_type = SENSOR_TYPE_BME280;

    // Pack payload: voltage (4B) + pressure (4B) + temperature (4B) + humidity (4B) = 16 bytes
    uint8_t payload_offset = 0;
    memcpy(&packet.payload[payload_offset], &voltage, sizeof(uint32_t));
    payload_offset += sizeof(uint32_t);
    memcpy(&packet.payload[payload_offset], &bme280_data.pressure, sizeof(float));
    payload_offset += sizeof(float);
    memcpy(&packet.payload[payload_offset], &bme280_data.temperature, sizeof(float));
    payload_offset += sizeof(float);
    memcpy(&packet.payload[payload_offset], &bme280_data.humidity, sizeof(float));
    payload_offset += sizeof(float);
    packet.header.payload_len = payload_offset;

    const uint32_t total_len = sizeof(packet_header_t) + packet.header.payload_len;
    ESP_LOGI(TAG, "Sending LoRa packet: sensor=%d, type=%d, len=%lu bytes",
             sensor_nr, SENSOR_TYPE_BME280, total_len);

    // 8. Send via LoRa
    esp_err_t send_err = sx1262_send((uint8_t*)&packet, total_len);

    // 9. Display result
    if (send_err == ESP_OK) {
        ESP_LOGI(TAG, "LoRa send successful");
        display_status(&u8g2, sensor_nr, "TX OK", true);
    } else {
        ESP_LOGE(TAG, "LoRa send failed: %s", esp_err_to_name(send_err));
        display_status(&u8g2, sensor_nr, esp_err_to_name(send_err), false);
    }

    // Show status for STATUS_DISPLAY_MS milliseconds
    vTaskDelay(pdMS_TO_TICKS(STATUS_DISPLAY_MS));

    // 10. Enter deep sleep
    //start_deep_sleep();
}
