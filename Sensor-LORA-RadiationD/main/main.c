/**
 * @file main.c
 * @brief LoRa Geigerzähler Sender — Heltec WiFi LoRa 32 V4 (ESP32-S3 / SX1262)
 *
 * Architektur: Deep Sleep + ULP-FSM für verlustfreie Impulszählung.
 *
 * Normalbetrieb  → Deep Sleep
 * ULP läuft in einer permanenten Endschleife (Dauerlauf)
 * und zählt Impulse auf GPIO4 im Unter-Mikrosekunden-Takt.
 * RTC-Timer weckt alle 60s für LoRa-TX.
 * GPIO0 (Taste) weckt für Display-Modus.
 *
 * Timer-Wakeup   → ULP edge_count lesen → Gleitenden 60-Minuten-Durchschnitt 
 * berechnen → LoRa TX → Sleep.
 * Button-Wakeup  → Display an, Live-Werte aus dem RTC-Speicher anzeigen.
 * Nach 5 Sekunden Timeout → Sleep.
 *
 * Geiger-Sensor (RadiationD-v1.1) auf GPIO4 (RTC-IO4).
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "nvs_flash.h"

#include "ulp.h"
#include "ulp_main.h"   /* Auto-generiert aus pulse_cnt.S */

#include "u8g2_esp32_hal.h"
#include "sx1262.h"

#include "../common/packet_format.h"
#include "config.h"

/* ============================================================================
 * Konstanten
 * ============================================================================ */

#define TX_INTERVAL_SECONDS    60
#define ROLLING_WINDOW_MINUTES 60

/* --- Board-Pins --- */
#define PIN_LED      GPIO_NUM_35
#define PIN_VEXT     GPIO_NUM_36   /* Display-Stromversorgung (LOW = an) */
#define PIN_VFEM     GPIO_NUM_7    /* RF-Verstärker-Versorgung (HIGH = an) */
#define PIN_PA_CSD   GPIO_NUM_2    /* GC1109 Chip-Enable (HIGH = aktiv) */
#define PIN_PA_CPS   GPIO_NUM_46   /* GC1109 RX/TX-Pfad (HIGH = High-Power TX) */

#define DISPLAY_SDA  GPIO_NUM_17
#define DISPLAY_SCL  GPIO_NUM_18
#define DISPLAY_RST  GPIO_NUM_21

/* GPIO0: Deep-Sleep-Wakeup (Taste) und Config-Menü (Boot-Taste) */
#define BUTTON_GPIO  GPIO_NUM_0

/* ADC */
#define ADC_CTRL       GPIO_NUM_37
#define ADC_CHANNEL    ADC_CHANNEL_0
#define ADC_UNIT       ADC_UNIT_1
#define ADC_ATTEN      ADC_ATTEN_DB_2_5
#define ADC_VBAT_R1    390.0f
#define ADC_VBAT_R2    100.0f
#define ADC_VBAT_CALIB (4140.0f / 4037.0f)
#define ADC_SAMPLES    16

/* Geigerzähler */
#define PIN_GEIGER                   GPIO_NUM_4
#define GEIGER_CONVERSION_FACTOR     153.8f

/* LoRa */
#define LORA_FREQUENCY        869525000UL
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
 * RTC-Speicher (überlebt Deep Sleep)
 * ============================================================================ */

static RTC_DATA_ATTR sensor_config_t  rtc_cfg;
static RTC_DATA_ATTR bool             rtc_cfg_valid  = false;
static RTC_DATA_ATTR uint32_t         rtc_voltage_mv = 3700;
static RTC_DATA_ATTR float            rtc_last_cpm   = 0.0f;
static RTC_DATA_ATTR float            rtc_last_usvh  = 0.0f;

/* Gleitendes Fenster im RTC-Speicher */
static RTC_DATA_ATTR uint32_t         rtc_history[ROLLING_WINDOW_MINUTES];
static RTC_DATA_ATTR int              rtc_history_idx = 0;
static RTC_DATA_ATTR int              rtc_minutes_elapsed = 0;
static RTC_DATA_ATTR uint32_t         rtc_total_counts = 0;

/* ============================================================================
 * ULP-Binär
 * ============================================================================ */

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

/* ============================================================================
 * Hilfsfunktionen & Peripherie-Init
 * ============================================================================ */

static void blink(void) {
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << PIN_LED),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(PIN_LED, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LED, 0);
}

static void init_heltec_v4(bool display_power) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_VEXT | 1ULL << PIN_VFEM |
                         1ULL << PIN_PA_CSD | 1ULL << PIN_PA_CPS),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(PIN_VEXT,   display_power ? 0 : 1);  /* 0 = an, 1 = aus */
    gpio_set_level(PIN_PA_CSD, 0);
    gpio_set_level(PIN_PA_CPS, 0);
    gpio_set_level(PIN_VFEM,   0);

    if (display_power) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void rf_amp_enable(void) {
    gpio_set_level(PIN_PA_CSD, 1);
    gpio_set_level(PIN_PA_CPS, 1);
    gpio_set_level(PIN_VFEM,   1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

static void rf_amp_disable(void) {
    gpio_set_level(PIN_VFEM,   0);
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

    return ESP_OK;
}

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

/* ============================================================================
 * ADC / Batteriespannung
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
        int adc_raw, adc_mv;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw));
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, adc_raw, &adc_mv));
        sum += adc_mv;
    }
    int adc_avg_mv = sum / ADC_SAMPLES;
    *voltage = (uint32_t)(adc_avg_mv * (ADC_VBAT_R1 + ADC_VBAT_R2) / ADC_VBAT_R2 * ADC_VBAT_CALIB);

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
    return ESP_OK;
}

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
    ESP_LOGI(TAG, "TX: sensor=%d usvh=%.4f uSv/h cpm=%.1f vbat=%lumV",
             cfg->sensor_nr, usvh, cpm, voltage_mv);

    rf_amp_enable();
    esp_err_t send_err = sx1262_send((uint8_t *)&packet, total_len);
    rf_amp_disable();

    if (send_err == ESP_OK) {
        ESP_LOGI(TAG, "LoRa OK");
        blink();
    } else {
        ESP_LOGE(TAG, "LoRa TX fehler: %s", esp_err_to_name(send_err));
    }
}

/* ============================================================================
 * ULP
 * ============================================================================ */

static void init_ulp_program(void) {
    ESP_ERROR_CHECK(ulp_load_binary(
        0,
        ulp_main_bin_start,
        (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t)
    ));

    ESP_ERROR_CHECK(rtc_gpio_init(PIN_GEIGER));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(PIN_GEIGER, RTC_GPIO_MODE_INPUT_ONLY));
    rtc_gpio_pullup_dis(PIN_GEIGER);
    rtc_gpio_pulldown_dis(PIN_GEIGER);

    ulp_io_number             = rtc_io_number_get(PIN_GEIGER);
    ulp_next_edge             = 0;       /* Start mit fallender Flanke */
    ulp_debounce_max_count    = 0;       /* 0 = Software-Debounce inaktiv */
    ulp_debounce_counter      = 0;
    ulp_edge_count            = 0;

    ESP_LOGI(TAG, "ULP geladen für RTC-IO=%lu (Dauerlauf-Modus)", ulp_io_number);
}

static void start_ulp(void) {
    ulp_next_edge        = 0;
    ulp_debounce_counter = 0;
    ESP_ERROR_CHECK(ulp_run(&ulp_entry - RTC_SLOW_MEM));
}

/* ============================================================================
 * Deep Sleep Vorbereitung
 * ============================================================================ */

static void enter_deep_sleep(void) {
    gpio_set_level(PIN_PA_CSD, 0);
    gpio_set_level(PIN_PA_CPS, 0);
    gpio_set_level(PIN_VFEM,   0);
    gpio_set_level(PIN_VEXT,   1);  /* Rail trennen */

    rtc_gpio_hold_dis(PIN_GEIGER);
    rtc_gpio_init(PIN_GEIGER);
    rtc_gpio_set_direction(PIN_GEIGER, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_dis(PIN_GEIGER);
    rtc_gpio_pulldown_dis(PIN_GEIGER);

    rtc_gpio_init(BUTTON_GPIO);
    rtc_gpio_set_direction(BUTTON_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(BUTTON_GPIO);
    rtc_gpio_pulldown_dis(BUTTON_GPIO);

    while (rtc_gpio_get_level(BUTTON_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    esp_sleep_enable_timer_wakeup((uint64_t)TX_INTERVAL_SECONDS * 1000000ULL);
    esp_sleep_enable_ext0_wakeup(BUTTON_GPIO, 0);

    start_ulp();

    ESP_LOGI(TAG, "→ Deep Sleep (%ds Timer)", TX_INTERVAL_SECONDS);
    esp_deep_sleep_start();
}

/* ============================================================================
 * Wakeup-Handhabung
 * ============================================================================ */

static void handle_tx_wakeup(void) {
    uint32_t raw   = ulp_edge_count;
    uint32_t edges = raw & 0xFFFF;
    ulp_edge_count = 0;

    ESP_LOGI(TAG, "Timer-Wakeup: Flanken in dieser Periode: %lu", edges);

    /* 2 Flanken = 1 Impuls */
    uint32_t pulses_this_minute = edges / 2;

    /* Gleitendes 60-Minuten-Fenster (Rolling Average im RTC-RAM) */
    rtc_total_counts -= rtc_history[rtc_history_idx];
    rtc_history[rtc_history_idx] = pulses_this_minute;
    rtc_total_counts += pulses_this_minute;
    
    rtc_history_idx = (rtc_history_idx + 1) % ROLLING_WINDOW_MINUTES;
    
    if (rtc_minutes_elapsed < ROLLING_WINDOW_MINUTES) {
        rtc_minutes_elapsed++;
    }
    
    float cpm  = (float)rtc_total_counts / (float)rtc_minutes_elapsed;
    float usvh = cpm / GEIGER_CONVERSION_FACTOR;

    rtc_last_cpm  = cpm;
    rtc_last_usvh = usvh;

    ESP_LOGI(TAG, "→ %lu Impulse/min | Ø %.1f CPM | %.4f uSv/h", pulses_this_minute, cpm, usvh);

    init_heltec_v4(false);
    get_voltage(&rtc_voltage_mv);

    if (init_lora(rtc_cfg.tx_power, rtc_cfg.spreading_factor) == ESP_OK) {
        send_packet(&rtc_cfg, usvh, cpm, rtc_voltage_mv);
    }
    sx1262_deinit_bus();
}

static void handle_display_mode(void) {
    rtc_gpio_deinit(BUTTON_GPIO);
    rtc_gpio_deinit(PIN_GEIGER);

    init_heltec_v4(true);

    u8g2_t u8g2;
    ESP_ERROR_CHECK(init_display(&u8g2));

    update_display(&u8g2, rtc_cfg.sensor_nr, rtc_last_usvh, rtc_last_cpm, rtc_voltage_mv);

    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);
    while (gpio_get_level(BUTTON_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
    u8g2_SetPowerSave(&u8g2, 1);
}

static void handle_cold_start(void) {
    init_heltec_v4(true);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    sensor_config_t cfg;
    config_load(&cfg);

    u8g2_t u8g2;
    ESP_ERROR_CHECK(init_display(&u8g2));
    config_run_menu(&u8g2, &cfg);

    rtc_cfg       = cfg;
    rtc_cfg_valid = true;

    /* RTC-Variablen für das gleitende Fenster beim Kaltstart initialisieren */
    memset(rtc_history, 0, sizeof(rtc_history));
    rtc_history_idx     = 0;
    rtc_minutes_elapsed = 0;
    rtc_total_counts    = 0;
    rtc_last_cpm        = 0.0f;
    rtc_last_usvh       = 0.0f;

    get_voltage(&rtc_voltage_mv);

    if (init_lora(cfg.tx_power, cfg.spreading_factor) == ESP_OK) {
        send_packet(&cfg, 0.0f, 0.0f, rtc_voltage_mv);
    }
    sx1262_deinit_bus();

    init_ulp_program();
}

/* ============================================================================
 * app_main
 * ============================================================================ */

void app_main(void) {
    /* DFS aktivieren: 80 MHz bei Last, 40 MHz im Idle zur Verbrauchsminimierung */
    esp_pm_config_t pm_config = {
        .max_freq_mhz       = 80,
        .min_freq_mhz       = 40,
        .light_sleep_enable = false,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (!rtc_cfg_valid || cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "Kaltstart erkannt.");
        handle_cold_start();
    } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "Wakeup: Timer-Intervall erreicht.");
        handle_tx_wakeup();
    } else if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        ESP_LOGI(TAG, "Wakeup: Manueller Tastendruck.");
        handle_display_mode();
    } else {
        ESP_LOGW(TAG, "Unerwarteter Wakeup (%d). Erzwungener Messzyklus.", cause);
        handle_tx_wakeup();
    }

    enter_deep_sleep();
}