#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "u8g2_esp32_hal.h"
#include "button.h"
#include "driver/ledc.h"
#include <string.h>
#include <time.h>

/* LED PWM configuration */
#define LED_LEDC_TIMER      LEDC_TIMER_1
#define LED_LEDC_CHANNEL    LEDC_CHANNEL_1
#define LED_LEDC_FREQ_HZ    1000
#define LED_LEDC_RESOLUTION LEDC_TIMER_8_BIT   /* Duty 0-255 */
#define LED_DUTY_ON         40                 /* ~31% brightness, no blinding white */
#define LED_DUTY_OFF        0

/* Display task priority: low so it never blocks sensor path */
#define DISPLAY_TASK_PRIORITY  (tskIDLE_PRIORITY + 1)
#define DISPLAY_TASK_STACK     (2048)

static const char* TAG = "display";

static u8g2_t g_u8g2;
static SemaphoreHandle_t g_mutex;
static bool g_display_on = false;
static button_handle_t *g_button_handle = NULL;

/* Display update queue (async from sensor callbacks) */
static QueueHandle_t g_display_queue;

/* Display scroll buffer */
static display_line_t g_buffer[DISPLAY_BUFFER_SIZE];
static uint8_t g_count;      // valid entries (0..DISPLAY_BUFFER_SIZE)
static uint8_t g_write_idx;  // next write position

/* Display task handle */
static TaskHandle_t g_display_task_handle = NULL;

/* Pending toggle state (for when mutex is unavailable) */
static bool g_pending_toggle = false;

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static void get_time_str(char *buf, size_t buf_size) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, buf_size, "%H:%M", &timeinfo);
}

/* Formats a received sensor packet into a single display line. This is the
 * only place that knows about sensor_packet_t fields - everything past the
 * queue (display_task, redraw()) just deals with ready-to-draw text. */
static void format_sensor_line(char *buf, size_t buf_len, const sensor_packet_t *packet) {
    char time_str[6];
    get_time_str(time_str, sizeof(time_str));

    if (packet->link.msg_source == SENSOR_SOURCE_LORA) {
        snprintf(buf, buf_len, "%s [L] %d %ddBm %+.1fdB",
                 time_str, packet->header.sensor_nr, packet->link.rssi, (double)packet->link.snr);
    } else if (packet->link.msg_source == SENSOR_SOURCE_ESPNOW) {
        snprintf(buf, buf_len, "%s [N] %d %ddBm",
                 time_str, packet->header.sensor_nr, packet->link.rssi);
    } else {
        ESP_LOGE(TAG, "Unknown source: %d", packet->link.msg_source);
        snprintf(buf, buf_len, "%s [?] %d", time_str, packet->header.sensor_nr);
    }
}

/* Shared non-blocking enqueue path for display_update_async() and
 * display_log_line() - both just build a text line and hand it off here. */
static esp_err_t enqueue_line(const char *text) {
    if (g_display_queue == NULL) {
        return ESP_ERR_INVALID_STATE;  /* display_init() not called */
    }

    display_line_t qe;
    strncpy(qe.text, text, sizeof(qe.text) - 1);
    qe.text[sizeof(qe.text) - 1] = '\0';

    /* Non-blocking enqueue with short timeout.
     * If queue is full, the update is dropped but the caller is NOT blocked. */
    BaseType_t xRet = xQueueSendToBack(g_display_queue, &qe, pdMS_TO_TICKS(10));

    if (xRet == pdTRUE) {
        return ESP_OK;  /* Enqueued successfully */
    }

    ESP_LOGD(TAG, "Display queue full, line dropped: '%s'", text);
    return ESP_ERR_TIMEOUT;  /* Queue full */
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

/* Forward declaration */
static void redraw(void);

/* ==========================================================================
 * Display Update Task
 * ==========================================================================
 * Runs at LOW priority and reads from the display queue.
 * Sensor callbacks (LoRa/ESP-NOW) run at HIGHER priority and only
 * write to the queue (non-blocking).
 * ========================================================================== */

static void display_task(void *arg) {
    (void)arg;
    display_line_t qe;

    ESP_LOGI(TAG, "Display task started (priority=%d, stack=%d)",
             DISPLAY_TASK_PRIORITY, DISPLAY_TASK_STACK);

    while (1) {
        /* Block until a new display update is queued.
         * pdMS_TO_TICKS(100) allows periodic wake-up for pending toggle check. */
        if (xQueueReceive(g_display_queue, &qe, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (g_mutex == NULL) continue;

            if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                /* Add new entry to scroll buffer */
                g_buffer[g_write_idx] = qe;

                g_write_idx = (g_write_idx + 1) % DISPLAY_BUFFER_SIZE;
                if (g_count < DISPLAY_BUFFER_SIZE) {
                    g_count++;
                }
                /* Buffer full: overwrites oldest entry */

                if (g_display_on) {
                    redraw();
                }

                xSemaphoreGive(g_mutex);
            }
        }

        /* Check for pending toggle from display_toggle() */
        if (g_pending_toggle) {
            g_pending_toggle = false;
            if (g_mutex != NULL && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (g_display_on) {
                    u8g2_SetPowerSave(&g_u8g2, 1);
                    g_display_on = false;
                    led_set(true);
                    ESP_LOGI(TAG, "Display off, LED on (pending toggle)");
                } else {
                    u8g2_SetPowerSave(&g_u8g2, 0);
                    g_display_on = true;
                    redraw();
                    led_set(false);
                    ESP_LOGI(TAG, "Display on, LED off (pending toggle)");
                }
                xSemaphoreGive(g_mutex);
            }
        }
    }
}

static void redraw(void) {
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SetFont(&g_u8g2, DISPLAY_FONT);

    /* oldest entry first, newest at bottom */
    int start_idx = (g_write_idx - g_count + DISPLAY_BUFFER_SIZE) % DISPLAY_BUFFER_SIZE;

    for (int i = 0; i < g_count; i++) {
        int idx = (start_idx + i) % DISPLAY_BUFFER_SIZE;
        int y = DISPLAY_ENTRY_START_Y + (i * DISPLAY_LINE_HEIGHT);
        u8g2_DrawStr(&g_u8g2, 1, y, g_buffer[idx].text);
    }

    u8g2_SendBuffer(&g_u8g2);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

esp_err_t display_init(void) {
    u8g2_esp32_hal_t hal = U8G2_ESP32_HAL_DEFAULT;
    hal.bus.i2c.sda = DISPLAY_PIN_SDA;
    hal.bus.i2c.scl = DISPLAY_PIN_SCL;
    hal.reset        = DISPLAY_PIN_RST;
    u8g2_esp32_hal_init(hal);

    vTaskDelay(pdMS_TO_TICKS(100));

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &g_u8g2, U8G2_R0, // Rotation
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);

    u8x8_SetI2CAddress(&g_u8g2.u8x8, 0x3C << 1);
    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 1);  /* Display off by default after start */

    g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Create display update queue (async from sensor callbacks) */
    g_display_queue = xQueueCreate(DISPLAY_QUEUE_MAX_ENTRIES, sizeof(display_line_t));
    if (g_display_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create display queue");
        vSemaphoreDelete(g_mutex);
        g_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    g_count     = 0;
    g_write_idx = 0;
    memset(g_buffer, 0, sizeof(g_buffer));
    g_pending_toggle = false;

    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SendBuffer(&g_u8g2);

    led_init();
    led_set(true);  /* Display starts off -> LED on */
    ESP_LOGI(TAG, "Display initialized (SSD1306 128x64, I2C 0x3C), default off");

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
        vSemaphoreDelete(g_mutex);
        g_mutex = NULL;
        vQueueDelete(g_display_queue);
        g_display_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Button configured on pin %d for display toggle", DISPLAY_BUTTON_PIN);

    /* Start display update task */
    esp_err_t ret = xTaskCreate(display_task, "disp_task",
                                DISPLAY_TASK_STACK,
                                NULL,
                                DISPLAY_TASK_PRIORITY,
                                &g_display_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
        vSemaphoreDelete(g_mutex);
        g_mutex = NULL;
        vQueueDelete(g_display_queue);
        g_display_queue = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Display async update enabled");
    return ESP_OK;
}

esp_err_t display_update(const sensor_packet_t *packet) {
    /* Legacy blocking API: forwards to async queue for backwards compatibility.
     * The actual display update happens asynchronously in display_task(). */
    return display_update_async(packet);
}

esp_err_t display_update_async(const sensor_packet_t *packet) {
    if (packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char text[DISPLAY_LINE_MAX_LEN];
    format_sensor_line(text, sizeof(text), packet);
    return enqueue_line(text);
}

esp_err_t display_log_line(const char *text) {
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return enqueue_line(text);
}

void display_toggle(void) {
    /* Set pending toggle flag - display_task will apply it on next cycle.
     * This avoids blocking the caller (which may be an ISR context). */
    if (g_mutex != NULL) {
        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            /* Toggle display state directly if we get the mutex */
            if (g_display_on) {
                u8g2_SetPowerSave(&g_u8g2, 1);
                g_display_on = false;
                led_set(true);
                ESP_LOGI(TAG, "Display off, LED on");
            } else {
                u8g2_SetPowerSave(&g_u8g2, 0);
                g_display_on = true;
                redraw();
                led_set(false);
                ESP_LOGI(TAG, "Display on, LED off");
            }
            xSemaphoreGive(g_mutex);
        } else {
            /* Can't get mutex — flag will be checked by display_task */
            g_pending_toggle = true;
            /* Wake up display task by sending a dummy signal */
            if (g_display_queue != NULL) {
                display_line_t wake = {0};
                xQueueSendToBack(g_display_queue, &wake, 0);
            }
        }
    } else {
        g_pending_toggle = true;
    }
}
