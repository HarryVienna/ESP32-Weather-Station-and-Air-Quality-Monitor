/**
 * @file main.c
 * @brief Sensor Receiver: LoRa + ESP-NOW Empfang mit Display und I2C-Speicher
 * 
 * Architektur:
 * - LoRa (SX1262): Empfängt Sensordaten von LoRa-Sensoren
 * - ESP-NOW: Empfängt Sensordaten von ESP-NOW-Sensoren
 * - Sensor-Stack: FIFO-Puffer für I2C-Abruf durch ESP32-P4
 * - Display: OLED 128x64 mit Scroll-Funktion
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"

// Project modules
#include "sensor_stack.h"
#include "display/display.h"
#include "network/lora.h"
#include "network/esp-now.h"
#include "button.h"

static const char* TAG = "MAIN";

static void on_button_press(button_press_type_t type) {
    display_wake();
}

#define PIN_VEXT     GPIO_NUM_36  // Display power supply (LOW = on)

// PIN DEFINITIONS FOR RF FRONT-END MODULE (FEM)
#define PIN_VFEM     GPIO_NUM_7   // Amplifier power
#define PIN_PA_CSD   GPIO_NUM_2   // Chip Shut Down / Enable
#define PIN_PA_CPS   GPIO_NUM_46  // RX/TX Path Control

/**
 * @brief Configure VExt, VFem and PA pins
 */
static esp_err_t init_board(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_VEXT | 1ULL << PIN_VFEM | 
                         1ULL << PIN_PA_CSD | 1ULL << PIN_PA_CPS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // 1. Display on
    gpio_set_level(PIN_VEXT, 0);   // LOW = Turn on display
    ESP_LOGI(TAG, "VExt activated (Display power supply)");

    // 2. Activate amplifier logic
    gpio_set_level(PIN_PA_CSD, 1); 
    gpio_set_level(PIN_PA_CPS, 1); 

    // 3. Amplifier power on
    gpio_set_level(PIN_VFEM, 1); 
    ESP_LOGI(TAG, "VFem activated");

    // Longer delay for stabilization after VExt activation
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Board initialized");
    return ESP_OK;
}

/**
 * @brief I2C Slave task for P4 readout
 * 
 * This task handles I2C requests from the ESP32-P4 master.
 * The P4 can read packet count and individual packets.
 */
static void i2c_slave_task(void *arg) {
    // TODO: Implement I2C slave interface for P4 readout
    // The P4 will connect via I2C to GPIO pins (e.g., GPIO20/21)
    // Register: 0x00 = packet count, 0x01 = read packet (36 bytes)
    
    ESP_LOGI(TAG, "I2C slave task started (P4 readout)");
    
    // Placeholder: Just monitor stack stats
    uint32_t last_count = 0;
    while (1) {
        uint32_t received, overwritten;
        sensor_stack_stats(&received, &overwritten);

        if (received != last_count) {
            ESP_LOGI(TAG, "Stack: %d sensors with data, Total: %lu, Overwritten: %lu",
                     sensor_stack_count(), received, overwritten);
            last_count = received;
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=====================================================");
    ESP_LOGI(TAG, "   Sensor Receiver: LoRa + ESP-NOW                  ");
    ESP_LOGI(TAG, "   Heltec WiFi LoRa 32 ESP32-S3                     ");
    ESP_LOGI(TAG, "=====================================================");

    // Initialize Board (VExt, VFem, PA)
    if (init_board() != ESP_OK) {
        ESP_LOGE(TAG, "Board initialization failed!");
        return;
    }

    // Initialize LoRa Hardware
    if (init_lora() != ESP_OK) {
        ESP_LOGE(TAG, "LoRa initialization failed!");
        return;
    }

        // Initialize WiFi for ESP-NOW
    if (init_wifi() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi initialization failed!");
        return;
    }

    // TODO Later via I2C
    // Set timezone for ESP32 system time (CET/CEST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to CET/CEST");
    
    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Display driver initialization failed!");
        return;
    }
    
    // Initialize Sensor Stack
    if (sensor_stack_init() != ESP_OK) {
        ESP_LOGE(TAG, "Sensor stack initialization failed!");
        return;
    }
    

    
    // Initialize ESP-NOW
    if (esp_now_start() != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW initialization failed!");
        return;
    }
    
    // Start LoRa Receiver (configures SX1262 and starts async RX)
    if (lora_start() != ESP_OK) {
        ESP_LOGE(TAG, "LoRa receiver start failed!");
        return;
    }
    
    // Button: wake display on short or long press
    button_config_t btn_cfg = {
        .gpio_num             = DISPLAY_BUTTON_PIN,
        .active_low           = true,
        .short_press_callback = on_button_press,
        .long_press_callback  = on_button_press,
        .double_click_callback = NULL,
        .enable_repeat        = false,
    };
    button_create(&btn_cfg);

    // Start I2C Slave Task (for P4 readout)
    xTaskCreate(i2c_slave_task, "i2c_slave", 4096, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "Sensor Receiver is running!");
    ESP_LOGI(TAG, "LoRa: Receiving on 868.1 MHz, SF10, BW125");
    ESP_LOGI(TAG, "ESP-NOW: Channel 13, waiting for pairing");
    ESP_LOGI(TAG, "Display: OLED 128x64 SSD1306");
    ESP_LOGI(TAG, "I2C: Slave mode for P4 readout");
    
    // Main loop - everything runs via callbacks
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
