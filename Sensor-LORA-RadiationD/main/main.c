/**
 * @file main.c
 * @brief LoRa Geigerzähler Sender — Heltec WiFi LoRa 32 V4 (ESP32-S3 / SX1262)
 *
 * Misst die Strahlung über einen Geigerzähler (RadiationD-v1.1, GPIO41),
 * zeigt den aktuellen µSv/h-Wert auf dem OLED an und sendet CPM, µSv/h
 * sowie die Batteriespannung in regelmäßigen Abständen per LoRa.
 *
 * Kein Deep Sleep — das Gerät läuft dauerhaft (kontinuierliche Zählung
 * für den gleitenden Mittelwert + Live-Anzeige).
 *
 * Boot: Reset → Config-Menü auf OLED → Messung & Versand-Loop
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_log.h"
#include "esp_pm.h"
#include "nvs_flash.h"

#include "u8g2_esp32_hal.h"
#include "sx1262.h"
#include "geiger.h"
#include "button.h"

#include "../common/packet_format.h"
#include "config.h"

/* ============================================================================
 * Constants
 * ============================================================================ */

#define TX_INTERVAL_SECONDS  60   // Sendeintervall (CPM/µSv/h + Batteriespannung)

/* LED */
#define PIN_LED      GPIO_NUM_35

/* Heltec V4 FEM / power pins — V4 nutzt einen externen GC1109-Verstärker,
 * der explizit aktiviert werden muss. */
#define PIN_VEXT     GPIO_NUM_36  // Display power (LOW = on)
#define PIN_VFEM     GPIO_NUM_7   // Verstärker-Versorgung (HIGH = an)
#define PIN_PA_CSD   GPIO_NUM_2   // GC1109 Chip-Enable (HIGH = aktiv)
#define PIN_PA_CPS   GPIO_NUM_46  // GC1109 RX/TX-Pfad (HIGH = High-Power TX)

/* Display (SSD1306 128x64) */
#define DISPLAY_SDA  GPIO_NUM_17
#define DISPLAY_SCL  GPIO_NUM_18
#define DISPLAY_RST  GPIO_NUM_21

/* Taste — schaltet nach dem Setup das Display ein/aus.
 * GPIO0 wird vom Config-Menü genutzt und beim Verlassen wieder
 * freigegeben (button_delete() in config.c), daher hier wiederverwendbar. */
#define BUTTON_GPIO  GPIO_NUM_0

/* ADC — GPIO37 = ADC_CTRL (HIGH enables voltage divider)
 *         GPIO1  = VBAT_Read = ADC1_CH0
 *         Divider: 390k : 100k = 4.9×                      */
#define ADC_CTRL       GPIO_NUM_37
#define ADC_CHANNEL    ADC_CHANNEL_0
#define ADC_UNIT       ADC_UNIT_1
#define ADC_ATTEN      ADC_ATTEN_DB_2_5
#define ADC_VBAT_R1    390.0f
#define ADC_VBAT_R2    100.0f
#define ADC_VBAT_CALIB (4140.0f / 4037.0f)    // empirical correction (avg 4037 mV, multimeter 4140 mV)
#define ADC_SAMPLES    16                      // average N readings to reduce noise

/* Geigerzähler (RadiationD-v1.1, J321/M4011-Röhre) */
#define PIN_GEIGER                   GPIO_NUM_42
#define GEIGER_CONVERSION_FACTOR     153.8f   // CPM → µSv/h (https://muman.ch/muman/muman-geiger-counter.htm)
#define GEIGER_ROLLING_AVG_SECONDS   600       // gleitender Mittelwert über 10 Minuten

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
 * Configure Heltec V4 power rails: display (VEXT) and the GC1109 RF
 * front-end amplifier (VFEM/CSD/CPS). The amplifier is left OFF here —
 * it draws significant current continuously, but is only actually
 * needed for the brief LoRa TX window. See rf_amp_enable()/rf_amp_disable().
 */
static esp_err_t init_heltec_v4(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_VEXT | 1ULL << PIN_VFEM | 1ULL << PIN_PA_CSD | 1ULL << PIN_PA_CPS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(PIN_VEXT, 0);    // LOW = Display an
    gpio_set_level(PIN_PA_CSD, 0);  // GC1109 Chip aus — nur für TX aktiviert
    gpio_set_level(PIN_PA_CPS, 0);
    gpio_set_level(PIN_VFEM, 0);

    vTaskDelay(pdMS_TO_TICKS(200));   // Rail-Stabilisierung (VEXT)

    ESP_LOGI(TAG, "Heltec V4 init: display on, GC1109 amplifier off (on-demand for TX)");
    return ESP_OK;
}

/**
 * Power the GC1109 RF front-end amplifier on/off around a transmission.
 * sx1262_send() blocks until SX1262_IRQ_TX_DONE (or a timeout), so it's
 * safe to disable the amplifier as soon as it returns.
 */
static void rf_amp_enable(void) {
    gpio_set_level(PIN_PA_CSD, 1);  // GC1109 Chip aktivieren
    gpio_set_level(PIN_PA_CPS, 1);  // High-Power TX-Pfad
    gpio_set_level(PIN_VFEM, 1);    // Verstärker-Versorgung an
    vTaskDelay(pdMS_TO_TICKS(200)); // Rail-Stabilisierung
}

static void rf_amp_disable(void) {
    gpio_set_level(PIN_VFEM, 0);
    gpio_set_level(PIN_PA_CSD, 0);
    gpio_set_level(PIN_PA_CPS, 0);
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

/**
 * Show the current radiation reading: large µSv/h value plus
 * CPM and battery voltage as secondary status line.
 */
static void update_display(u8g2_t *u8g2, uint8_t sensor_nr,
                            float usvh, float cpm, uint32_t voltage_mv) {
    char buf[24];

    u8g2_ClearBuffer(u8g2);

    u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
    snprintf(buf, sizeof(buf), "Geiger #%d", sensor_nr);
    u8g2_DrawStr(u8g2, 0, 9, buf);
    u8g2_DrawHLine(u8g2, 0, 12, 128);

    u8g2_SetFont(u8g2, u8g2_font_logisoso20_tn);
    snprintf(buf, sizeof(buf), "%.4f", usvh);
    u8g2_DrawStr(u8g2, 0, 42, buf);
    int value_w = u8g2_GetStrWidth(u8g2, buf);

    u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
    u8g2_DrawStr(u8g2, value_w + 6, 42, "uSv/h");

    snprintf(buf, sizeof(buf), "%.1f CPM   %lu mV", cpm, voltage_mv);
    u8g2_DrawStr(u8g2, 0, 62, buf);

    u8g2_SendBuffer(u8g2);
}

/* Vom Setup gewünschter Display-Zustand — von der Taste umgeschaltet,
 * von der Hauptschleife angewandt (u8g2_SetPowerSave bleibt damit auf
 * einem einzigen Task, kein Mutex nötig). */
static volatile bool display_on = false;

static void display_button_callback(button_press_type_t type) {
    (void)type;
    display_on = !display_on;
}

/* ============================================================================
 * ADC
 * ============================================================================ */

static esp_err_t get_voltage(uint32_t *voltage) {
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

    int32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        int adc_raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw));
        int adc_mv;
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, adc_raw, &adc_mv));
        sum += adc_mv;
    }
    int adc_avg_mv = sum / ADC_SAMPLES;
    ESP_LOGI(TAG, "ADC avg: %d mV (%d samples)", adc_avg_mv, ADC_SAMPLES);

    *voltage = (uint32_t)(adc_avg_mv * (ADC_VBAT_R1 + ADC_VBAT_R2) / ADC_VBAT_R2 * ADC_VBAT_CALIB);
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

/**
 * Build and transmit a geiger_payload_t packet (µSv/h, CPM, battery voltage).
 */
static void send_packet(const sensor_config_t *cfg, float usvh, float cpm, uint32_t voltage_mv) {
    lora_sensor_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.header.msg_type    = DATA;
    packet.header.sensor_nr   = cfg->sensor_nr;
    packet.header.sensor_type = SENSOR_TYPE_GEIGER;

    geiger_payload_t geiger_payload = {
        .voltage = voltage_mv,
        .usvh    = usvh,
        .cpm     = cpm,
    };
    memcpy(packet.payload, &geiger_payload, sizeof(geiger_payload_t));
    packet.header.payload_len = sizeof(geiger_payload_t);

    uint32_t total_len = sizeof(packet_header_t) + packet.header.payload_len;
    ESP_LOGI(TAG, "TX: sensor=%d usvh=%.4f µSv/h cpm=%.1f vbat=%lumV len=%lu",
             cfg->sensor_nr, usvh, cpm, voltage_mv, total_len);

    rf_amp_enable();
    esp_err_t send_err = sx1262_send((uint8_t *)&packet, total_len);
    rf_amp_disable();

    if (send_err == ESP_OK) {
        ESP_LOGI(TAG, "LoRa send OK");
        blink();
    } else {
        ESP_LOGE(TAG, "LoRa send failed: %s", esp_err_to_name(send_err));
    }
}

/* ============================================================================
 * app_main
 * ============================================================================ */

void app_main(void) {

    ESP_LOGI(TAG, "Boot: reset → config menu");

    /* Dynamic Frequency Scaling: 80 MHz nur, wenn ein Treiber sie aktiv
     * braucht (I2C/ADC/SPI/...), sonst 40 MHz (= XTAL, kleinste sinnvolle
     * Taktstufe). Bewusst OHNE Light Sleep — das würde die GPIO-Interrupts
     * (und damit die Impulszählung des Geigerzählers) unterbrechen. */
    esp_pm_config_t pm_config = {
        .max_freq_mhz       = 80,
        .min_freq_mhz       = 40,
        .light_sleep_enable = false,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

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

    init_heltec_v4();
    ESP_ERROR_CHECK(init_display(&u8g2));
    config_run_menu(&u8g2, &cfg);
    /* config_run_menu saves to NVS before returning */

    /* Nach dem Setup: Display ausschalten, Taste (GPIO0 — von config_run_menu
     * bereits wieder freigegeben) übernimmt ab jetzt das Ein-/Ausschalten. */
    u8g2_SetPowerSave(&u8g2, 1);

    button_config_t display_btn_cfg = {
        .gpio_num              = BUTTON_GPIO,
        .active_low            = true,
        .short_press_callback  = display_button_callback,
        .long_press_callback   = display_button_callback,
        .double_click_callback = NULL,
        .enable_repeat         = false,
    };
    button_create(&display_btn_cfg);

    /* ── Geigerzähler ── */
    const geiger_config_t geiger_cfg = {
        .gpio_pin            = PIN_GEIGER,
        .conversion_factor   = GEIGER_CONVERSION_FACTOR,
        .rolling_avg_seconds = GEIGER_ROLLING_AVG_SECONDS,
    };
    geiger_init(&geiger_cfg);

    /* ── LoRa ── */
    ESP_ERROR_CHECK(init_lora(cfg.tx_power, cfg.spreading_factor));

    /* ── Mess- & Sende-Loop (kein Deep Sleep — Gerät läuft dauerhaft) ── */
    uint32_t voltage = 0;
    get_voltage(&voltage);

    uint32_t seconds_since_tx = 0;
    bool display_was_on = false;   // Display ist nach dem Setup aus

    while (1) {
        float usvh = geiger_get_usvh();
        float cpm  = geiger_get_cpm();

        if (display_on != display_was_on) {
            u8g2_SetPowerSave(&u8g2, display_on ? 0 : 1);
            display_was_on = display_on;
        }

        if (display_on) {
            update_display(&u8g2, cfg.sensor_nr, usvh, cpm, voltage);
        }

        if (++seconds_since_tx >= TX_INTERVAL_SECONDS) {
            seconds_since_tx = 0;
            get_voltage(&voltage);
            send_packet(&cfg, usvh, cpm, voltage);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
