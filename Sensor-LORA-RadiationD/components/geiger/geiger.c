#include "geiger.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "stdatomic.h"
#include "esp_timer.h"

static const char *TAG = "geiger_lib";

#define DEBOUNCE_TIME_US 200

// Internal variables (static, so they remain private to this file)
static atomic_uint pulse_count = ATOMIC_VAR_INIT(0);
static float current_usvh = 0.0f;
static float current_cpm = 0.0f;

// Store configuration
static geiger_config_t active_config;
static uint32_t *rolling_buffer = NULL;

// ISR Handler
static void IRAM_ATTR geiger_isr_handler(void* arg) {
    // Debounce logic
    // static int64_t last_pulse_time = 0;
    // int64_t current_time = esp_timer_get_time();

    // if (current_time - last_pulse_time > DEBOUNCE_TIME_US) {
    //     atomic_fetch_add(&pulse_count, 1);
    //     last_pulse_time = current_time;
    // }

    atomic_fetch_add(&pulse_count, 1);
}

// Background task (handles the calculation loop)
static void geiger_task(void *pvParameters) {
    int buffer_index = 0;
    uint32_t total_counts_in_period = 0;

    // Initialize buffer (allocate memory)
    rolling_buffer = calloc(active_config.rolling_avg_seconds, sizeof(uint32_t));
    if (rolling_buffer == NULL) {
        ESP_LOGE(TAG, "No memory for rolling buffer!");
        vTaskDelete(NULL);
    }

    while (1) {
        // Wait 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 1. Atomically retrieve and reset the counter
        uint32_t counts_this_second = atomic_exchange(&pulse_count, 0);

        // 2. Rolling Average Logic
        total_counts_in_period -= rolling_buffer[buffer_index];
        total_counts_in_period += counts_this_second;
        rolling_buffer[buffer_index] = counts_this_second;

        buffer_index = (buffer_index + 1) % active_config.rolling_avg_seconds;

        // 3. Calculation
        float cpm = (float)total_counts_in_period * (60.0f / (float)active_config.rolling_avg_seconds);
        
        // Write values to global (static) variables
        current_cpm = cpm;
        current_usvh = cpm / active_config.conversion_factor;
        
        // Optional: Debug log directly from the lib (can be commented out)
        ESP_LOGI(TAG, "CPM: %.1f, uSv/h: %.4f", current_cpm, current_usvh);
    }
}

void geiger_init(const geiger_config_t *config) {
    // Copy configuration
    active_config = *config;

    // GPIO Setup
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = (1ULL << config->gpio_pin);
    io_conf.mode = GPIO_MODE_INPUT;
    // No pullup here!
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Install ISR
    // Check if service is already installed to avoid crashes
    gpio_install_isr_service(0); 
    ESP_ERROR_CHECK(gpio_isr_handler_add(config->gpio_pin, geiger_isr_handler, NULL));

    ESP_LOGI(TAG, "Geiger initialized at pin %d. Starting task...", config->gpio_pin);

    // Start task (stack size 4096 is safe, priority 5 is medium)
    xTaskCreate(geiger_task, "geiger_task", 4096, NULL, 5, NULL);
}

// Getter functions (thread-safe enough for read access)
float geiger_get_usvh(void) { return current_usvh; }
float geiger_get_cpm(void) { return current_cpm; }