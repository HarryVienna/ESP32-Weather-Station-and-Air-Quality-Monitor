/**
 * @file lora_testtool.c
 * @brief LoRa Test Tool with interactive menu for Heltec WiFi LoRa 32 V3.x & V4
 * 
 * Navigation:
 * Left Side (Menu):
 * - Short Click: Next menu item
 * - Long Press: Switch to right side (Edit mode)
 * Right Side (Edit):
 * - Short Click: Next value
 * - Long Press: Back to left side (Menu)
 * 
 * Operation:
 * - Program starts in Edit mode with all operations stopped
 * - In Edit mode (right side): All operations stopped, parameters can be changed
 * - On exit from Edit mode: Configuration applied and operations start
 * - In Menu mode (left side): Radio operates based on selected mode
 * - On enter to Edit mode: All operations stop immediately
 * 
 * Architecture:
 * - Uses async receive mode with callback instead of blocking receive
 * - No separate receive task needed - packets are handled in rx_callback()
 * - Send task is suspended when not needed (Edit mode or RECEIVE mode)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "u8g2.h"
#include "button.h"
#include "sx1262.h"

static const char* TAG = "LORA_TOOL";

// ============================================================================
// CONFIGURATION
// ============================================================================

#define BUTTON_PIN          GPIO_NUM_0
#define LED_PIN             GPIO_NUM_35
#define SEND_INTERVAL_MS    2000
#define PACKET_SIZE         32

// ============================================================================
// MENU STRUCTURE
// ============================================================================

typedef enum {
    MENU_MODE = 0,
    MENU_SF,
    MENU_BW,
    MENU_CR,
    MENU_TX_POWER,
    MENU_COUNT  // Number of menu items
} menu_item_t;

typedef enum {
    MODE_SEND = 0,
    MODE_RECEIVE
} operation_mode_t;

typedef struct {
    menu_item_t current_item;       // Current menu item
    bool editing;                   // true = Edit value, false = Navigate menu
    
    // LoRa Parameters
    operation_mode_t mode;          // Send/Receive
    uint8_t sf;                     // Spreading Factor 5-12
    uint16_t bw;                    // Bandwidth 125/250/500
    sx1262_coding_rate_t cr;        // Coding Rate 4/5, 4/6, 4/7, 4/8
    int8_t tx_power;                // TX Power -9 to 22
    
    // Status
    bool is_sending;                // Is currently sending?
    int16_t last_rssi;              // Last RSSI value
    uint32_t packets_sent;          // Number of packets sent
    uint32_t packets_received;      // Number of packets received
    uint32_t last_packet_time;      // Timestamp of the last packet
} menu_state_t;

// Global variables
static menu_state_t menu;
static u8g2_t u8g2;
static button_handle_t* button;
static TaskHandle_t send_task_handle = NULL;
static SemaphoreHandle_t display_mutex = NULL;
static volatile bool display_needs_update = false;
static volatile bool rx_mode_active = false;  // Tracks if async RX is active
static volatile bool tx_mode_active = false;  // Tracks if sending is active

// ============================================================================
// DISPLAY FUNCTIONS
// ============================================================================

/**
 * @brief Converts Bandwidth value to string
 */
static const char* bw_to_string(uint16_t bw) {
    switch(bw) {
        case 125: return "125";
        case 250: return "250";
        case 500: return "500";
        default: return "???";
    }
}

/**
 * @brief Converts Coding Rate to string
 */
static const char* cr_to_string(sx1262_coding_rate_t cr) {
    switch(cr) {
        case LORA_CR_4_5: return "4/5";
        case LORA_CR_4_6: return "4/6";
        case LORA_CR_4_7: return "4/7";
        case LORA_CR_4_8: return "4/8";
        default: return "???";
    }
}

/**
 * @brief Converts Bandwidth to SX1262 enum
 */
static sx1262_bandwidth_t bw_to_enum(uint16_t bw) {
    switch(bw) {
        case 125: return LORA_BW_125;
        case 250: return LORA_BW_250;
        case 500: return LORA_BW_500;
        default: return LORA_BW_125;
    }
}

/**
 * @brief Draws a menu item
 */
static void draw_menu_item(int y, const char* label, const char* value, bool is_active, bool is_editing) {
    // Cursor for active menu item
    if (is_active && !is_editing) {
        u8g2_DrawStr(&u8g2, 0, y, ">");
    }
    
    // Label
    u8g2_DrawStr(&u8g2, 8, y, label);
    
    // Value (right-aligned, inverted when editing)
    int value_width = u8g2_GetStrWidth(&u8g2, value);
    int value_x = 128 - value_width - 2;
    
    if (is_active && is_editing) {
        // Inverted bar for Edit mode
        u8g2_SetDrawColor(&u8g2, 1);
        u8g2_DrawBox(&u8g2, value_x - 2, y - 10, value_width + 4, 12);
        u8g2_SetDrawColor(&u8g2, 0);
        u8g2_DrawStr(&u8g2, value_x, y, value);
        u8g2_SetDrawColor(&u8g2, 1);
    } else {
        u8g2_DrawStr(&u8g2, value_x, y, value);
    }
}

/**
 * @brief Draws the status line
 */
static void draw_status_line(void) {
    char status[32];
    
    // Separator line
    u8g2_DrawHLine(&u8g2, 0, 52, 128);
    
    // Show "PAUSED" when editing
    if (menu.editing) {
        snprintf(status, sizeof(status), "PAUSED");
        u8g2_DrawStr(&u8g2, 2, 62, status);
        return;
    }
    
    if (menu.mode == MODE_SEND) {
        if (menu.is_sending) {
            snprintf(status, sizeof(status), "TX: %lu", menu.packets_sent);
            // Blink indicator
            if ((xTaskGetTickCount() / 100) % 2 == 0) {
                u8g2_DrawStr(&u8g2, 100, 60, "*");
            }
        } else {
            snprintf(status, sizeof(status), "Sent: %lu", menu.packets_sent);
        }
    } else {
        if (menu.packets_received > 0) {
            snprintf(status, sizeof(status), "RX:%ld RSSI:%d", 
                       menu.packets_received, menu.last_rssi);
        } else {
            snprintf(status, sizeof(status), "Waiting...");
        }
    }
    
    u8g2_DrawStr(&u8g2, 2, 62, status);
}

/**
 * @brief Main function for drawing the display (thread-safe)
 */
static void update_display(void) {
    // Take mutex (wait max 100ms)
    if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Could not take display mutex");
        return;
    }
    
    char value_str[16];
    
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tr);
    
    // Draw menu items
    // Mode
    snprintf(value_str, sizeof(value_str), "%s", 
             menu.mode == MODE_SEND ? "Send" : "Recv");
    draw_menu_item(10, "Mode:", value_str, 
                   menu.current_item == MENU_MODE, menu.editing);
    
    // SF
    snprintf(value_str, sizeof(value_str), "%d", menu.sf);
    draw_menu_item(20, "SF:", value_str, 
                   menu.current_item == MENU_SF, menu.editing);
    
    // BW
    snprintf(value_str, sizeof(value_str), "%s", bw_to_string(menu.bw));
    draw_menu_item(30, "BW:", value_str, 
                   menu.current_item == MENU_BW, menu.editing);
    
    // CR
    snprintf(value_str, sizeof(value_str), "%s", cr_to_string(menu.cr));
    draw_menu_item(40, "CR:", value_str, 
                   menu.current_item == MENU_CR, menu.editing);
    
    // TX Power
    snprintf(value_str, sizeof(value_str), "%d", menu.tx_power);
    draw_menu_item(50, "Pwr:", value_str, 
                   menu.current_item == MENU_TX_POWER, menu.editing);
    
    // Status line
    draw_status_line();
    
    u8g2_SendBuffer(&u8g2);
    
    // Release mutex
    xSemaphoreGive(display_mutex);
}

/**
 * @brief Marks display as "needs update" - will be updated asynchronously
 */
static void request_display_update(void) {
    display_needs_update = true;
}

// ============================================================================
// LORA FUNCTIONS
// ============================================================================

/**
 * @brief Callback function for asynchronous packet reception
 * 
 * This is called from the SX1262 driver's receive task when a packet arrives.
 * The callback runs in a HIGH PRIORITY task context (not ISR), so we can 
 * safely use logging and GPIO operations.
 */
static void rx_callback(uint8_t *data, uint8_t len, sx1262_packet_status_t *status) {
    // Increment receive counter
    menu.packets_received++;
    
    // Turn on LED
    gpio_set_level(LED_PIN, 1);
    
    // Store RSSI and timestamp
    menu.last_rssi = status->rssi_pkt;
    menu.last_packet_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Log packet reception
    ESP_LOGI(TAG, "RX #%lu: %d bytes, RSSI:%d, SNR:%.1f", 
             menu.packets_received, len, 
             status->rssi_pkt, status->snr_pkt);
    
    // Print packet data (null-terminate safely)
    if (len < 255) {
        data[len] = '\0';
        ESP_LOGI(TAG, "Data: %s", data);
    }
    
    // Request display update
    request_display_update();
    
    // Turn off LED after short delay
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LED_PIN, 0);
}

/**
 * @brief Updates the LoRa configuration based on menu settings
 */
static void update_lora_config(void) {
    sx1262_config_t config = {
        .modem_mode = SX1262_MODEM_LORA,
        .frequency = 869525000,  // 869,525 MHz == Middle of G3 band
        .tx_power = menu.tx_power,
        .bandwidth = bw_to_enum(menu.bw),
        .spreading_factor = menu.sf,
        .coding_rate = menu.cr,
        .iq_inverted = false,
        .rx_gain_boosted = true,
        .preamble_length = 8,
        .payload_length = 0,  // Variable length
        .crc_on = true,
        .sync_word = 0x1424  // Private network
    };
    
    esp_err_t err = sx1262_configure(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure SX1262: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LoRa configured: SF%d BW%d CR%s Power%d", 
                 menu.sf, menu.bw, cr_to_string(menu.cr), menu.tx_power);
    }
}

/**
 * @brief LoRa Send Task
 */
static void lora_send_task(void* parameter) {
    uint8_t packet[PACKET_SIZE];
    
    while (true) {
        // Task is suspended when not in SEND mode
        // If we're running, we send
        
        // Prepare packet
        snprintf((char*)packet, PACKET_SIZE, 
                 "PKT:%lu SF:%d BW:%d", menu.packets_sent, menu.sf, menu.bw);
        
        // Send
        menu.is_sending = true;
        request_display_update();

        esp_err_t err = sx1262_send(packet, strlen((char*)packet));
        
        if (err == ESP_OK) {
            menu.packets_sent++;
            ESP_LOGI(TAG, "Sent packet #%lu", menu.packets_sent);
        } else {
            ESP_LOGE(TAG, "Send failed: %s", esp_err_to_name(err));
        }

        menu.is_sending = false;
        request_display_update();
        
        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}

/**
 * @brief Start sending packets
 */
static void start_send(void) {
    if (tx_mode_active) {
        return;  // Already running
    }
    
    // Create task if it doesn't exist yet
    if (send_task_handle == NULL) {
        BaseType_t ret = xTaskCreate(
            lora_send_task,
            "lora_send",
            4096,
            NULL,
            5,
            &send_task_handle
        );
        
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create send task");
            return;
        }
        ESP_LOGI(TAG, "Send task created");
    } else {
        // Task exists, resume it
        vTaskResume(send_task_handle);
    }
    
    tx_mode_active = true;
    ESP_LOGI(TAG, "Sending started");
}

/**
 * @brief Stop sending packets
 */
static void stop_send(void) {
    if (!tx_mode_active) {
        return;  // Already stopped
    }
    
    if (send_task_handle != NULL) {
        vTaskSuspend(send_task_handle);
        tx_mode_active = false;
        ESP_LOGI(TAG, "Sending stopped");
    }
}

/**
 * @brief Start asynchronous receive mode
 */
static void start_receive(void) {
    if (rx_mode_active) {
        return;  // Already active
    }
    
    ESP_LOGI(TAG, "Starting async receive mode...");
    esp_err_t err = sx1262_start_receive_async(rx_callback);
    
    if (err == ESP_OK) {
        rx_mode_active = true;
        ESP_LOGI(TAG, "Receiving started");
    } else {
        ESP_LOGE(TAG, "Failed to start async RX: %s", esp_err_to_name(err));
    }
}

/**
 * @brief Stop asynchronous receive mode
 */
static void stop_receive(void) {
    if (!rx_mode_active) {
        return;  // Already stopped
    }
    
    ESP_LOGI(TAG, "Stopping async receive mode...");
    sx1262_stop_receive_async();
    rx_mode_active = false;
    ESP_LOGI(TAG, "Receiving stopped");
}

/**
 * @brief Display Update Task - updates display when needed
 */
static void display_update_task(void* parameter) {
    while (true) {
        // Check if update is needed
        if (display_needs_update) {
            display_needs_update = false;
            update_display();
        }
        
        // Regular update for blink indicator in Send mode
        if (menu.mode == MODE_SEND && menu.is_sending) {
            update_display();
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));  // 10 Hz update rate
    }
}

// ============================================================================
// MENU NAVIGATION
// ============================================================================

/**
 * @brief Next menu item (cyclic)
 */
static void menu_next_item(void) {
    menu.current_item = (menu.current_item + 1) % MENU_COUNT;
    ESP_LOGI(TAG, "Menu item: %d", menu.current_item);
    request_display_update();
}

/**
 * @brief Next value for current menu item
 */
static void menu_next_value(void) {
    switch(menu.current_item) {
        case MENU_MODE:
            // Switch mode (actual start/stop happens when exiting edit mode)
            menu.mode = (menu.mode == MODE_SEND) ? MODE_RECEIVE : MODE_SEND;
            
            // Reset counters on mode change
            menu.packets_sent = 0;
            menu.packets_received = 0;
            menu.last_rssi = 0;
            ESP_LOGI(TAG, "Mode: %s (will be applied on exit)", 
                     menu.mode == MODE_SEND ? "SEND" : "RECEIVE");
            break;
            
        case MENU_SF:
            menu.sf++;
            if (menu.sf > 12) menu.sf = 5;
            ESP_LOGI(TAG, "SF: %d", menu.sf);
            break;
            
        case MENU_BW:
            if (menu.bw == 125) menu.bw = 250;
            else if (menu.bw == 250) menu.bw = 500;
            else menu.bw = 125;
            ESP_LOGI(TAG, "BW: %d", menu.bw);
            break;
            
        case MENU_CR:
            // Cycle through coding rates: 4/5 -> 4/6 -> 4/7 -> 4/8 -> 4/5
            switch(menu.cr) {
                case LORA_CR_4_5: menu.cr = LORA_CR_4_6; break;
                case LORA_CR_4_6: menu.cr = LORA_CR_4_7; break;
                case LORA_CR_4_7: menu.cr = LORA_CR_4_8; break;
                case LORA_CR_4_8: menu.cr = LORA_CR_4_5; break;
                default: menu.cr = LORA_CR_4_5; break;
            }
            ESP_LOGI(TAG, "CR: %s", cr_to_string(menu.cr));
            break;
            
        case MENU_TX_POWER:
            menu.tx_power++;
            if (menu.tx_power > 22) menu.tx_power = -9;
            ESP_LOGI(TAG, "TX Power: %d dBm", menu.tx_power);
            break;
            
        default:
            break;
    }
    
    request_display_update();
}

/**
 * @brief Switch to Edit mode
 */
static void menu_enter_edit_mode(void) {
    menu.editing = true;
    
    // Always stop all operations
    stop_send();
    stop_receive();
    
    ESP_LOGI(TAG, "Edit mode: ON - All operations stopped");
    request_display_update();
}

/**
 * @brief Switch back to Menu mode
 */
static void menu_exit_edit_mode(void) {
    menu.editing = false;
    
    // Apply new configuration
    update_lora_config();
    ESP_LOGI(TAG, "Configuration applied");
    
    // Start operations based on current mode
    if (menu.mode == MODE_RECEIVE) {
        start_receive();
    } else {
        start_send();
    }
    
    ESP_LOGI(TAG, "Edit mode: OFF");
    request_display_update();
}

// ============================================================================
// BUTTON CALLBACKS
// ============================================================================

/**
 * @brief Button Callback - handles all button events
 * * NAVIGATION:
 * Left Side (Menu):
 * - Short Click: Next menu item
 * - Long Press: Switch to right side (Edit mode)
 * * Right Side (Edit):
 * - Short Click: Next value
 * - Long Press: Back to left side (Menu mode)
 */
static void button_callback(button_press_type_t type) {
    switch(type) {
        case BUTTON_PRESS_SHORT:
            if (menu.editing) {
                // In Edit mode: Next value
                menu_next_value();
            } else {
                // In Menu mode: Next menu item
                menu_next_item();
            }
            break;
            
        case BUTTON_PRESS_LONG:
            if (menu.editing) {
                // In Edit mode: Back to menu
                menu_exit_edit_mode();
            } else {
                // In Menu mode: Switch to Edit mode
                menu_enter_edit_mode();
            }
            break;
            
        case BUTTON_PRESS_DOUBLE:
            // Double-click is not used
            break;
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initializes the menu with default values
 */
static void init_menu(void) {
    memset(&menu, 0, sizeof(menu_state_t));
    
    menu.current_item = MENU_MODE;
    menu.editing = true;  // Start in edit mode
    menu.mode = MODE_SEND;
    menu.sf = 5;
    menu.bw = 125;
    menu.cr = LORA_CR_4_5;
    menu.tx_power = -9;
    menu.is_sending = false;
    menu.last_rssi = 0;
    menu.packets_sent = 0;
    menu.packets_received = 0;
}

/**
 * @brief Initializes the button
 */
static esp_err_t init_button(void) {
    button_config_t button_config = {
        .gpio_num = BUTTON_PIN,
        .active_low = true,
        .short_press_callback = button_callback,
        .long_press_callback = button_callback,
        .double_click_callback = NULL,  // Not used
        .enable_repeat = false
    };
    
    button = button_create(&button_config);
    if (!button) {
        ESP_LOGE(TAG, "Failed to create button");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Button initialized on GPIO %d", BUTTON_PIN);
    return ESP_OK;
}

/**
 * @brief Initializes the display
 */
static esp_err_t init_display(u8g2_t* display) {
    if (!display) {
        ESP_LOGE(TAG, "Display pointer is NULL");
        return ESP_FAIL;
    }
    
    // Take over display pointer
    memcpy(&u8g2, display, sizeof(u8g2_t));
    
    // Test output
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tr);
    u8g2_DrawStr(&u8g2, 10, 30, "LoRa TestTool");
    u8g2_DrawStr(&u8g2, 20, 45, "Initializing...");
    u8g2_SendBuffer(&u8g2);
    
    ESP_LOGI(TAG, "Display initialized");
    return ESP_OK;
}

/**
 * @brief Starts the Display task
 */
static esp_err_t start_display_task(void) {
    BaseType_t ret;
    
    // Display Update Task (highest priority for UI responsiveness)
    ret = xTaskCreate(
        display_update_task,
        "display_upd",
        3072,
        NULL,
        6,  // Higher priority
        NULL
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display update task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Display task started");
    return ESP_OK;
}

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * @brief Main initialization of the LoRa TestTool
 * * @param display Pointer to initialized u8g2 display
 * @return ESP_OK on success
 */
esp_err_t lora_testtool_init(u8g2_t* display) {
    ESP_LOGI(TAG, "=== LoRa TestTool Initializing ===");
    
    // Configure LED GPIO
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(LED_PIN, 0);  // LED OFF

    // Create mutex for thread-safe display access
    display_mutex = xSemaphoreCreateMutex();
    if (display_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create display mutex");
        return ESP_FAIL;
    }

    // Initialize menu
    init_menu();
    
    // Initialize display
    if (init_display(display) != ESP_OK) {
        return ESP_FAIL;
    }

    // Initialize SX1262
    ESP_LOGI(TAG, "Initializing SPI...");
    if (sx1262_init_bus() != ESP_OK) {
        ESP_LOGE(TAG, "SX1262 initialization failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Initializing SX1262...");
    if (sx1262_init_radio() != ESP_OK) {
        ESP_LOGE(TAG, "SX1262 initialization failed");
        return ESP_FAIL;
    }
    
    // Initial LoRa configuration
    update_lora_config();
    
    // Initialize button
    if (init_button() != ESP_OK) {
        return ESP_FAIL;
    }
    
    // Start display task
    if (start_display_task() != ESP_OK) {
        return ESP_FAIL;
    }
    
    // Initial Display update
    vTaskDelay(pdMS_TO_TICKS(1000));  // Short pause for "Initializing..."
    request_display_update();
    
    ESP_LOGI(TAG, "=== LoRa TestTool Ready ===");
    ESP_LOGI(TAG, "Started in Edit mode - press long to start operations");
    ESP_LOGI(TAG, "Navigation:");
    ESP_LOGI(TAG, "   Left Side (Menu):");
    ESP_LOGI(TAG, "     - Short Click: Next menu item");
    ESP_LOGI(TAG, "     - Long Press:  Enter edit mode (stops operations)");
    ESP_LOGI(TAG, "   Right Side (Edit):");
    ESP_LOGI(TAG, "     - Short Click: Next value");
    ESP_LOGI(TAG, "     - Long Press:  Exit edit mode (starts operations)");
    
    return ESP_OK;
}

/**
 * @brief Frees resources
 */
void lora_testtool_deinit(void) {
    // Stop async RX if active
    stop_receive();
    
    if (button) {
        button_delete(button);
    }
    
    if (send_task_handle) {
        vTaskDelete(send_task_handle);
    }
}
