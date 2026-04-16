/**
 * @file button.c
 * @brief Implementation of button handler library for ESP-IDF
 */

#include "button.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char* TAG = "BUTTON";

/**
 * @brief Internal button state structure
 */
struct button_handle {
    gpio_num_t gpio_num;                    /**< GPIO pin number */
    bool active_low;                        /**< Active low configuration */
    TaskHandle_t task_handle;               /**< FreeRTOS task handle */
    
    // Callback functions
    button_callback_t short_press_callback; /**< Short press callback */
    button_callback_t long_press_callback;  /**< Long press callback */
    button_callback_t double_click_callback; /**< Double-click callback */
    
    // State variables
    volatile bool click_detected;           /**< Flag for wait function */
    volatile button_press_type_t last_press_type; /**< Type of last detected press */
    bool enable_repeat;                     /**< Enable repeat for long press */
    
    // Debounce and timing
    int last_state;                         /**< Last known button state */
    uint32_t last_debounce_time;           /**< Last debounce timestamp */
    uint32_t press_start_time;             /**< Button press start timestamp */
    uint32_t last_repeat_time;             /**< Last repeat timestamp */
    bool long_press_triggered;             /**< Flag if long press was triggered */
    
    // Double-click detection
    uint32_t last_release_time;            /**< Last button release timestamp */
    bool waiting_for_double_click;         /**< Flag if waiting for second click */
};

/**
 * @brief Read the current button state (accounting for active_low)
 * 
 * @param handle Button handle
 * @return true if button is pressed, false otherwise
 */
static inline bool button_read_state(const button_handle_t* handle) {
    int level = gpio_get_level(handle->gpio_num);
    return handle->active_low ? (level == 0) : (level == 1);
}

/**
 * @brief Call short press callback if set
 * 
 * @param handle Button handle
 */
static void button_call_short_press(button_handle_t* handle) {
    handle->click_detected = true;
    handle->last_press_type = BUTTON_PRESS_SHORT;
    
    if (handle->short_press_callback) {
        handle->short_press_callback(BUTTON_PRESS_SHORT);
    }
}

/**
 * @brief Call long press callback if set
 * 
 * @param handle Button handle
 */
static void button_call_long_press(button_handle_t* handle) {
    handle->click_detected = true;
    handle->last_press_type = BUTTON_PRESS_LONG;
    
    if (handle->long_press_callback) {
        handle->long_press_callback(BUTTON_PRESS_LONG);
    }
}

/**
 * @brief Call double-click callback if set
 * 
 * @param handle Button handle
 */
static void button_call_double_click(button_handle_t* handle) {
    handle->click_detected = true;
    handle->last_press_type = BUTTON_PRESS_DOUBLE;
    
    if (handle->double_click_callback) {
        handle->double_click_callback(BUTTON_PRESS_DOUBLE);
    }
}

/**
 * @brief Button monitoring task
 * 
 * This task continuously monitors the button state, handles debouncing,
 * and triggers callbacks for short and long presses.
 * 
 * @param pvParameter Button handle passed as parameter
 */
static void button_task(void* pvParameter) {
    button_handle_t* handle = (button_handle_t*)pvParameter;
    
    // Initialize state variables
    handle->last_state = button_read_state(handle);
    handle->last_debounce_time = 0;
    handle->press_start_time = 0;
    handle->last_repeat_time = 0;
    handle->long_press_triggered = false;
    handle->last_release_time = 0;
    handle->waiting_for_double_click = false;
    
    ESP_LOGI(TAG, "Button task started for GPIO %d", handle->gpio_num);
    
    // Main monitoring loop
    while (true) {
        bool current_state = button_read_state(handle);
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Debouncing: Check if state changed and debounce time elapsed
        if (current_state != handle->last_state && 
            (now - handle->last_debounce_time) > BUTTON_DEBOUNCE_TIME_MS) {
            
            // Update debounce time and last state
            handle->last_debounce_time = now;
            handle->last_state = current_state;
            
            if (current_state) {
                // Button pressed
                handle->press_start_time = now;
                handle->long_press_triggered = false;
                ESP_LOGD(TAG, "Button pressed on GPIO %d", handle->gpio_num);
            } else {
                // Button released
                uint32_t press_duration = now - handle->press_start_time;
                
                // Only process short presses for double-click detection
                if (!handle->long_press_triggered && 
                    press_duration < BUTTON_LONG_PRESS_MS) {
                    
                    // Check if we're waiting for a second click (double-click detection)
                    if (handle->waiting_for_double_click && 
                        (now - handle->last_release_time) < BUTTON_DOUBLE_CLICK_MS) {
                        // Second click within timeout - it's a double-click!
                        ESP_LOGD(TAG, "Double-click detected on GPIO %d", handle->gpio_num);
                        button_call_double_click(handle);
                        handle->waiting_for_double_click = false;
                    } else {
                        // First click or timeout expired - start waiting for double-click
                        ESP_LOGD(TAG, "Short press detected on GPIO %d (duration: %lu ms)", 
                                 handle->gpio_num, press_duration);
                        handle->waiting_for_double_click = true;
                        handle->last_release_time = now;
                    }
                } else if (handle->long_press_triggered) {
                    ESP_LOGD(TAG, "Long press released on GPIO %d (duration: %lu ms)", 
                             handle->gpio_num, press_duration);
                }
                
                // Reset long press flag
                handle->long_press_triggered = false;
            }
        }
        
        // Long press detection and repeat
        if (current_state && 
            (now - handle->press_start_time) > BUTTON_LONG_PRESS_MS) {
            
            if (!handle->long_press_triggered) {
                // First long press trigger - cancel double-click waiting
                handle->long_press_triggered = true;
                handle->waiting_for_double_click = false;
                handle->last_repeat_time = now;
                ESP_LOGD(TAG, "Long press detected on GPIO %d", handle->gpio_num);
                button_call_long_press(handle);
            } else if (handle->enable_repeat && 
                      (now - handle->last_repeat_time) > BUTTON_REPEAT_INTERVAL_MS) {
                // Repeat long press
                handle->last_repeat_time = now;
                ESP_LOGD(TAG, "Long press repeat on GPIO %d", handle->gpio_num);
                button_call_long_press(handle);
            }
        }
        
        // Double-click timeout - trigger short press if timeout expired
        if (handle->waiting_for_double_click && 
            (now - handle->last_release_time) > BUTTON_DOUBLE_CLICK_MS) {
            // Timeout expired without second click - trigger short press
            ESP_LOGD(TAG, "Double-click timeout - triggering short press on GPIO %d", 
                     handle->gpio_num);
            button_call_short_press(handle);
            handle->waiting_for_double_click = false;
        }
        
        // Small delay to reduce CPU usage
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Initialize and create a button handler
 */
button_handle_t* button_create(const button_config_t* config) {
    if (!config) {
        ESP_LOGE(TAG, "Invalid config parameter (NULL)");
        return NULL;
    }
    
    // Allocate button handle
    button_handle_t* handle = (button_handle_t*)calloc(1, sizeof(button_handle_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate memory for button handle");
        return NULL;
    }
    
    // Initialize handle
    handle->gpio_num = config->gpio_num;
    handle->active_low = config->active_low;
    handle->short_press_callback = config->short_press_callback;
    handle->long_press_callback = config->long_press_callback;
    handle->double_click_callback = config->double_click_callback;
    handle->enable_repeat = config->enable_repeat;
    handle->click_detected = false;
    handle->task_handle = NULL;
    
    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", 
                 config->gpio_num, esp_err_to_name(err));
        free(handle);
        return NULL;
    }
    
    // Create monitoring task
    BaseType_t ret = xTaskCreate(
        button_task,
        "button_task",
        BUTTON_TASK_STACK_SIZE,
        handle,
        BUTTON_TASK_PRIORITY,
        &handle->task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task for GPIO %d", config->gpio_num);
        free(handle);
        return NULL;
    }
    
    ESP_LOGI(TAG, "Button created successfully on GPIO %d", config->gpio_num);
    return handle;
}

/**
 * @brief Delete button handler and free resources
 */
void button_delete(button_handle_t* handle) {
    if (!handle) {
        return;
    }
    
    // Delete task if it exists
    if (handle->task_handle) {
        vTaskDelete(handle->task_handle);
        ESP_LOGI(TAG, "Button task deleted for GPIO %d", handle->gpio_num);
    }
    
    // Free handle memory
    free(handle);
}

/**
 * @brief Set callback for short press events
 */
void button_set_short_press_callback(button_handle_t* handle, button_callback_t callback) {
    if (handle) {
        handle->short_press_callback = callback;
    }
}

/**
 * @brief Set callback for long press events
 */
void button_set_long_press_callback(button_handle_t* handle, button_callback_t callback) {
    if (handle) {
        handle->long_press_callback = callback;
    }
}

/**
 * @brief Set callback for double-click events
 */
void button_set_double_click_callback(button_handle_t* handle, button_callback_t callback) {
    if (handle) {
        handle->double_click_callback = callback;
    }
}

/**
 * @brief Enable or disable repeat for long press
 */
void button_set_repeat_enabled(button_handle_t* handle, bool enable) {
    if (handle) {
        handle->enable_repeat = enable;
    }
}

/**
 * @brief Wait (blocking) until any button press is detected
 */
button_press_type_t button_wait_for_press(button_handle_t* handle) {
    if (!handle) {
        return BUTTON_PRESS_SHORT;
    }
    
    // Reset click detection flag
    handle->click_detected = false;
    
    // Wait until a click is detected
    while (!handle->click_detected) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return handle->last_press_type;
}

/**
 * @brief Get current button state
 */
bool button_is_pressed(button_handle_t* handle) {
    if (!handle) {
        return false;
    }
    
    return button_read_state(handle);
}
