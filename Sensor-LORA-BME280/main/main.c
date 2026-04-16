/**
 * @file main.c
 * @brief Example for using the LoRa TestTool
 * 
 * This example shows how to use the LoRa TestTool in an ESP-IDF project.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"

// U8g2 and Hardware
#include "u8g2_esp32_hal.h"
#include "lora_testtool.h"
#include "sx1262.h"

static const char* TAG = "MAIN";

// Display Pins for Heltec WiFi LoRa 32 V3.x & V4
#define PIN_SDA      GPIO_NUM_17
#define PIN_SCL      GPIO_NUM_18
#define PIN_RST      GPIO_NUM_21
#define PIN_VEXT     GPIO_NUM_36

// -------------------------------------------------------------------------
// PIN DEFINITIONS FOR RF FRONT-END MODULE (FEM)
// -------------------------------------------------------------------------
// These pins are critical for the Heltec WiFi LoRa 32 V4 (V4.2). 
// The V4 uses an external amplifier (GC1109) which must be explicitly enabled.
//
// V4 Functionality:
// - GPIO 7 (VFEM):   Power supply for the amplifier circuit.
// - GPIO 2 (CSD):    Chip Shut Down. Must be HIGH to enable the GC1109.
// - GPIO 46 (CPS):   Control Path Select. Must be HIGH for High-Power TX (+28dBm).
//                    If LOW, the board runs in low-power "bypass mode".
//
// V3 Compatibility:
// - On V3 boards, these pins (7, 2, 46) are not used internally. 
// - They are exposed on the pin headers. 
// - Setting them to HIGH is safe for V3, provided no external hardware 
//   that conflicts with a HIGH signal is connected to these headers.
// -------------------------------------------------------------------------
#define PIN_VFEM     GPIO_NUM_7   // Amplifier power
#define PIN_PA_CSD   GPIO_NUM_2   // Chip Shut Down / Enable
#define PIN_PA_CPS   GPIO_NUM_46  // RX/TX Path Control


// U8g2 Display Handle
static u8g2_t u8g2;

/**
 * @brief COnfigure VExt and VFem
 */
static esp_err_t init_board(void) {
    // Configure and enable VExt & VFem Pin
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_VEXT | 1ULL << PIN_VFEM | 1ULL << PIN_PA_CSD | 1ULL << PIN_PA_CPS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // 1. Display on
    gpio_set_level(PIN_VEXT, 0);   // LOW = Turn on display
    ESP_LOGI(TAG, "VExt activated (Display power supply)");

    // 2. Activate amplifier logic (IMPORTANT!)
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
 * @brief Initializes GC1109 & OLED Display 
 */
static esp_err_t init_display(void) {

     // Configure U8g2 ESP32 HAL
     ESP_LOGI(TAG, "Configuring U8g2 HAL...");
     u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
     u8g2_esp32_hal.bus.i2c.sda = PIN_SDA;
     u8g2_esp32_hal.bus.i2c.scl = PIN_SCL;
     u8g2_esp32_hal.reset = PIN_RST;
     u8g2_esp32_hal_init(u8g2_esp32_hal);
     
     ESP_LOGI(TAG, "U8g2 HAL initialized");
     
     // Short pause after I2C Init
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
     // Heltec V3 typically uses 0x3C
     u8x8_SetI2CAddress(&u8g2.u8x8, 0x3C << 1 );
     
     ESP_LOGI(TAG, "Initializing Display (I2C Address 0x3C/0x78)...");
     
     // Initialize display
     u8g2_InitDisplay(&u8g2);
     u8g2_SetPowerSave(&u8g2, 0); // Turn on display
     
     ESP_LOGI(TAG, "Display initialized");
     return ESP_OK;
}


void app_main(void) {
    
     ESP_LOGI(TAG, "=====================================================");
     ESP_LOGI(TAG, "   LoRa TestTool for Heltec WiFi LoRa 32 V3.x & V4   ");
     ESP_LOGI(TAG, "=====================================================");

     // Initialize Board
     if (init_board() != ESP_OK) {
         ESP_LOGE(TAG, "Board initialization failed!");
         return;
     }

     // Initialize display
     if (init_display() != ESP_OK) {
         ESP_LOGE(TAG, "Display initialization failed!");
         return;
     }
     
     // Show welcome message
     u8g2_ClearBuffer(&u8g2);
     u8g2_SetFont(&u8g2, u8g2_font_6x10_tr);
     u8g2_DrawStr(&u8g2, 5, 20, "LoRa TestTool");
     u8g2_DrawStr(&u8g2, 5, 35, "Heltec WiFi LoRa 32");
     u8g2_DrawStr(&u8g2, 5, 50, "Starting...");
     u8g2_SendBuffer(&u8g2);
     
     vTaskDelay(pdMS_TO_TICKS(2000));
     
     // Initialize and start LoRa TestTool
     if (lora_testtool_init(&u8g2) != ESP_OK) {
         ESP_LOGE(TAG, "LoRa TestTool initialization failed!");
         
         // Error message on display
         u8g2_ClearBuffer(&u8g2);
         u8g2_DrawStr(&u8g2, 5, 30, "ERROR:");
         u8g2_DrawStr(&u8g2, 5, 45, "Init failed!");
         u8g2_SendBuffer(&u8g2);
         
         return;
     }
     
     ESP_LOGI(TAG, "LoRa TestTool is running!");
     
     // Main loop - The TestTool now runs independently
     while (1) {
         vTaskDelay(pdMS_TO_TICKS(1000));
     }
}