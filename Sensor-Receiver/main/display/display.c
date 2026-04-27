#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "u8g2_esp32_hal.h"
#include <string.h>
#include <time.h>

static const char* TAG = "display";

static u8g2_t           g_u8g2;
static SemaphoreHandle_t g_mutex;
static TaskHandle_t      g_blink_task;
static bool              g_display_on;
static TickType_t        g_last_activity;

static display_entry_t g_buffer[DISPLAY_BUFFER_SIZE];
static uint8_t         g_count;
static uint8_t         g_write_idx;

/* ============================================================================
 * LED blink task
 * ============================================================================ */

static void blink_task(void *arg) {
    while (1) {
        gpio_set_level(DISPLAY_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_LED_BLINK_MS));
        gpio_set_level(DISPLAY_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_LED_BLINK_MS));
    }
}

/* ============================================================================
 * Screensaver task — polls elapsed time, no timer callbacks
 * ============================================================================ */

static void screensaver_task(void *arg) {
    const TickType_t timeout = pdMS_TO_TICKS(DISPLAY_SCREENSAVER_TIMEOUT_S * 1000);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!g_display_on) {
            continue;
        }

        if ((xTaskGetTickCount() - g_last_activity) >= timeout) {
            if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                u8g2_SetPowerSave(&g_u8g2, 1);
                g_display_on = false;
                xSemaphoreGive(g_mutex);
            }
            vTaskResume(g_blink_task);
            ESP_LOGI(TAG, "Screensaver: display off");
        }
    }
}

/* ============================================================================
 * Display rendering
 * ============================================================================ */

static void get_time_str(char *buf, size_t buf_size) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, buf_size, "%H:%M", &timeinfo);
}

static void redraw(void) {
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, DISPLAY_FONT);

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

/* ============================================================================
 * Public API
 * ============================================================================ */

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

    g_count        = 0;
    g_write_idx    = 0;
    g_display_on   = true;
    g_last_activity = xTaskGetTickCount();
    memset(g_buffer, 0, sizeof(g_buffer));

    // LED GPIO
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << DISPLAY_LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(DISPLAY_LED_PIN, 0);

    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SendBuffer(&g_u8g2);

    // Blink task: created suspended, resumed by screensaver task
    xTaskCreate(blink_task, "led_blink", 1024, NULL, 1, &g_blink_task);
    vTaskSuspend(g_blink_task);

    // Screensaver task: runs independently, no timer callbacks
    xTaskCreate(screensaver_task, "screensaver", 2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "Display initialized (SSD1306 128x64, screensaver %ds)",
             DISPLAY_SCREENSAVER_TIMEOUT_S);
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

    if (g_display_on) {
        redraw();
    }

    xSemaphoreGive(g_mutex);

    // Update activity timestamp so screensaver resets
    g_last_activity = xTaskGetTickCount();

    return ESP_OK;
}

void display_wake(void) {
    // Suspend blink task and turn LED off
    vTaskSuspend(g_blink_task);
    gpio_set_level(DISPLAY_LED_PIN, 0);

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return;
    }

    if (!g_display_on) {
        u8g2_SetPowerSave(&g_u8g2, 0);
        g_display_on = true;
        redraw();
        ESP_LOGI(TAG, "Display woken by button");
    }

    xSemaphoreGive(g_mutex);

    // Reset activity so screensaver timer restarts from now
    g_last_activity = xTaskGetTickCount();
}
