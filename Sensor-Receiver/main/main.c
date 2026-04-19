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
#include "esp_log.h"
#include "esp_sleep.h"

// U8g2 and Hardware
#include "u8g2_esp32_hal.h"
#include "sx1262.h"

// Project modules
#include "sensor_stack.h"
#include "display_driver.h"
#include "lora/lora_receiver.h"
#include "esp-now/network.h"

static const char* TAG = "MAIN";

// Display Pins for Heltec WiFi LoRa 32 V3.x & V4
#define PIN_SDA      GPIO_NUM_17
#define PIN_SCL      GPIO_NUM_18
#define PIN_RST      GPIO_NUM_21
#define PIN_VEXT     GPIO_NUM_36

// -------------------------------------------------------------------------
// PIN DEFINITIONS FOR RF FRONT-END MODULE (FEM)
// -------------------------------------------------------------------------
#define PIN_VFEM     GPIO_NUM_7   // Amplifier power
#define PIN_PA_CSD   GPIO_NUM_2   // Chip Shut Down / Enable
#define PIN_PA_CPS   GPIO_NUM_46  // RX/TX Path Control

// U8g2 Display Handle
static u8g2_t u8g2;

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
 * @brief Initialize OLED Display
 */
static esp_err_t init_display(void) {
    ESP_LOGI(TAG, "Configuring U8g2 HAL...");
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = PIN_SDA;
    u8g2_esp32_hal.bus.i2c.scl = PIN_SCL;
    u8g2_esp32_hal.reset = PIN_RST;
    u8g2_esp32_hal_init(u8g2_esp32_hal);
    
    ESP_LOGI(TAG, "U8g2 HAL initialized");
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Initialize display (SSD1306 128x64 OLED)
    ESP_LOGI(TAG, "Setup U8g2 Display Structure...");
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &u8g2,
        U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb
    );
    
    // Set I2C address
    u8x8_SetI2CAddress(&u8g2.u8x8, 0x3C << 1 );
    
    ESP_LOGI(TAG, "Initializing Display (I2C Address 0x3C/0x78)...");
    
    // Initialize display
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0); // Turn on display
    
    ESP_LOGI(TAG, "Display initialized");
    return ESP_OK;
}

/**
 * @brief Initialize LoRa (SX1262) hardware
 */
static esp_err_t init_lora(void) {
    ESP_LOGI(TAG, "Initializing LoRa (SX1262)...");
    
    // Phase 1: Initialize SPI bus and GPIOs
    esp_err_t ret = sx1262_init_bus();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LoRa bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Phase 2: Cold start radio
    ret = sx1262_init_radio();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LoRa radio: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LoRa hardware initialized");
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
        uint32_t received, dropped;
        sensor_stack_stats(&received, &dropped);
        
        if (received != last_count) {
            ESP_LOGI(TAG, "Stack: %d packets, Total: %lu, Dropped: %lu",
                     sensor_stack_count(), received, dropped);
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

    // Initialize Display Hardware
    if (init_display() != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed!");
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

    // Set timezone for ESP32 system time (CET/CEST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to CET/CEST");
    
    // Initialize Display Driver (sets g_u8g2 pointer, creates mutex)
    if (display_driver_init(&u8g2) != ESP_OK) {
        ESP_LOGE(TAG, "Display driver initialization failed!");
        return;
    }
    
    // Initialize Sensor Stack
    if (sensor_stack_init() != ESP_OK) {
        ESP_LOGE(TAG, "Sensor stack initialization failed!");
        return;
    }
    

    
    // Initialize LoRa Receiver
    if (lora_receiver_init() != ESP_OK) {
        ESP_LOGE(TAG, "LoRa receiver initialization failed!");
        return;
    }
    

    
    // Initialize ESP-NOW
    if (esp_now_start() != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW initialization failed!");
        return;
    }
    
    // Start LoRa Receiver (configures SX1262 and starts async RX)
    if (lora_receiver_start() != ESP_OK) {
        ESP_LOGE(TAG, "LoRa receiver start failed!");
        return;
    }
    
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
