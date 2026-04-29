#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "u8g2_esp32_hal.h"
#include "button.h"
#include "driver/ledc.h"
#include <string.h>
#include <time.h>

/* LED PWM-Konfiguration */
#define LED_LEDC_TIMER      LEDC_TIMER_1
#define LED_LEDC_CHANNEL    LEDC_CHANNEL_1
#define LED_LEDC_FREQ_HZ    1000
#define LED_LEDC_RESOLUTION LEDC_TIMER_8_BIT   /* Duty 0-255 */
#define LED_DUTY_ON         40                 /* ~31% Helligkeit, kein blendendes Weiß */
#define LED_DUTY_OFF        0

static const char* TAG = "display";

static u8g2_t g_u8g2;
static SemaphoreHandle_t g_mutex;
static bool g_display_on = true;
static button_handle_t *g_button_handle = NULL;

static display_entry_t g_buffer[DISPLAY_BUFFER_SIZE];
static uint8_t g_count;      // valid entries (0..DISPLAY_BUFFER_SIZE)
static uint8_t g_write_idx;  // next write position

static void get_time_str(char *buf, size_t buf_size) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, buf_size, "%H:%M", &timeinfo);
}

static void led_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LED_LEDC_TIMER,
        .duty_resolution = LED_LEDC_RESOLUTION,
        .freq_hz         = LED_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LED_LEDC_CHANNEL,
        .timer_sel  = LED_LEDC_TIMER,
        .gpio_num   = DISPLAY_LED_PIN,
        .duty       = LED_DUTY_OFF,
        .hpoint     = 0,
    };
    ledc_channel_config(&channel);
}

static void led_set(bool on)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_LEDC_CHANNEL, on ? LED_DUTY_ON : LED_DUTY_OFF);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LED_LEDC_CHANNEL);
}

static void display_button_callback(button_press_type_t type) {
    (void)type;
    display_toggle();
}

static void redraw(void) {
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, DISPLAY_FONT);

    // oldest entry first, newest at bottom
    int start_idx = (g_write_idx - g_count + DISPLAY_BUFFER_SIZE) % DISPLAY_BUFFER_SIZE;

    for (int i = 0; i < g_count; i++) {
        int idx = (start_idx + i) % DISPLAY_BUFFER_SIZE;
        const display_entry_t *e = &g_buffer[idx];

        char line[42];
        if (e->source == SENSOR_SOURCE_LORA) {
            snprintf(line, sizeof(line), "%s [L] %d %ddBm %+.1fdB",
                     e->time_str, e->sensor_nr, e->rssi, (double)e->snr);
        } else if (e->source == SENSOR_SOURCE_ESPNOW) {
            snprintf(line, sizeof(line), "%s [N] %d %ddBm",
                     e->time_str, e->sensor_nr, e->rssi);
        } else {
            ESP_LOGE(TAG, "Unknown source: %d", e->source);
            snprintf(line, sizeof(line), "%s [?] %d", e->time_str, e->sensor_nr);
        }

        int y = DISPLAY_ENTRY_START_Y + (i * DISPLAY_LINE_HEIGHT);
        u8g2_DrawStr(&g_u8g2, 1, y, line);
    }

    u8g2_SendBuffer(&g_u8g2);
}

esp_err_t display_init(void) {
    u8g2_esp32_hal_t hal = U8G2_ESP32_HAL_DEFAULT;
    hal.bus.i2c.sda = DISPLAY_PIN_SDA;
    hal.bus.i2c.scl = DISPLAY_PIN_SCL;
    hal.reset        = DISPLAY_PIN_RST;
    u8g2_esp32_hal_init(hal);

    vTaskDelay(pdMS_TO_TICKS(100));

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &g_u8g2, U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);

    u8x8_SetI2CAddress(&g_u8g2.u8x8, 0x3C << 1);
    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 0);

    g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    g_count     = 0;
    g_write_idx = 0;
    memset(g_buffer, 0, sizeof(g_buffer));

    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SendBuffer(&g_u8g2);

    led_init();
    ESP_LOGI(TAG, "Display initialized (SSD1306 128x64, I2C 0x3C)");

    button_config_t btn_cfg = {
        .gpio_num             = DISPLAY_BUTTON_PIN,
        .active_low           = true,
        .short_press_callback = display_button_callback,
        .long_press_callback  = display_button_callback,
        .double_click_callback = NULL,
        .enable_repeat        = false,
    };
    g_button_handle = button_create(&btn_cfg);
    if (g_button_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create button handler");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Button configured on pin %d for display toggle", DISPLAY_BUTTON_PIN);
    return ESP_OK;
}

esp_err_t display_update(const sensor_packet_t *packet) {
    if (g_mutex == NULL || packet == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    display_entry_t *entry = &g_buffer[g_write_idx];
    entry->source      = packet->link.msg_source;
    entry->sensor_nr   = packet->header.sensor_nr;
    entry->sensor_type = packet->header.sensor_type;
    entry->payload_len = packet->header.payload_len;
    entry->rssi        = packet->link.rssi;
    entry->snr         = packet->link.snr;
    entry->timestamp   = packet->link.timestamp;
    get_time_str(entry->time_str, sizeof(entry->time_str));

    g_write_idx = (g_write_idx + 1) % DISPLAY_BUFFER_SIZE;
    if (g_count < DISPLAY_BUFFER_SIZE) {
        g_count++;
    }

    redraw();

    xSemaphoreGive(g_mutex);
    return ESP_OK;
}

void display_toggle(void) {
    if (g_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    // Toggle display state
    if (g_display_on) {
        u8g2_SetPowerSave(&g_u8g2, 1);
        g_display_on = false;
        led_set(true);   /* LED an — zeigt dass das Board noch läuft */
        ESP_LOGI(TAG, "Display off, LED on");
    } else {
        u8g2_SetPowerSave(&g_u8g2, 0);
        g_display_on = true;
        redraw();
        led_set(false);  /* LED aus — Display übernimmt die Anzeige */
        ESP_LOGI(TAG, "Display on, LED off");
    }
    
    xSemaphoreGive(g_mutex);
}
