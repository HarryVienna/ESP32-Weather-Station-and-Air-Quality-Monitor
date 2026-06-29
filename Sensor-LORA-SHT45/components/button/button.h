/**
 * @file button.h
 * @brief Button handler library for ESP-IDF with debouncing and press detection
 * 
 * This library provides a robust button handling implementation with:
 * - Debouncing to filter electrical noise
 * - Short press detection
 * - Double-click detection
 * - Long press detection with optional repeat
 * - Non-blocking operation using FreeRTOS tasks
 * 
 * @author Harald Kreuzer
 * @date 2025
 */

#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Button configuration constants
 */
#define BUTTON_DEBOUNCE_TIME_MS     50      /**< Debounce time in milliseconds */
#define BUTTON_LONG_PRESS_MS        500     /**< Threshold for long press in milliseconds */
#define BUTTON_REPEAT_INTERVAL_MS   200     /**< Repeat interval for continuous long press */
#define BUTTON_DOUBLE_CLICK_MS      400     /**< Maximum time between clicks for double-click */
#define BUTTON_TASK_STACK_SIZE      4096    /**< Stack size for button task */
#define BUTTON_TASK_PRIORITY        1       /**< Priority for button task */

/**
 * @brief Button press types
 */
typedef enum {
    BUTTON_PRESS_SHORT = 0,     /**< Short press detected */
    BUTTON_PRESS_LONG,          /**< Long press detected */
    BUTTON_PRESS_DOUBLE         /**< Double-click detected */
} button_press_type_t;

/**
 * @brief Callback function type for button events
 * 
 * @param press_type Type of button press that occurred
 */
typedef void (*button_callback_t)(button_press_type_t press_type);

/**
 * @brief Button handle structure (opaque to user)
 */
typedef struct button_handle button_handle_t;

/**
 * @brief Configuration structure for button initialization
 */
typedef struct {
    gpio_num_t gpio_num;                    /**< GPIO pin number for the button */
    bool active_low;                        /**< true if button is active low (with pull-up) */
    button_callback_t short_press_callback; /**< Callback for short press (can be NULL) */
    button_callback_t long_press_callback;  /**< Callback for long press (can be NULL) */
    button_callback_t double_click_callback; /**< Callback for double-click (can be NULL) */
    bool enable_repeat;                     /**< Enable repeat events for long press */
} button_config_t;

/**
 * @brief Initialize and create a button handler
 * 
 * Creates a FreeRTOS task to monitor the button state and handle debouncing.
 * The GPIO pin is automatically configured as input with pull-up resistor.
 * 
 * @param config Pointer to button configuration structure
 * @return Button handle on success, NULL on failure
 * 
 * @note The returned handle must be freed with button_delete() when no longer needed
 */
button_handle_t* button_create(const button_config_t* config);

/**
 * @brief Delete button handler and free resources
 * 
 * Stops the monitoring task and releases all allocated resources.
 * 
 * @param handle Button handle to delete
 */
void button_delete(button_handle_t* handle);

/**
 * @brief Set callback for short press events
 * 
 * @param handle Button handle
 * @param callback Callback function (can be NULL to disable)
 */
void button_set_short_press_callback(button_handle_t* handle, button_callback_t callback);

/**
 * @brief Set callback for long press events
 * 
 * @param handle Button handle
 * @param callback Callback function (can be NULL to disable)
 */
void button_set_long_press_callback(button_handle_t* handle, button_callback_t callback);

/**
 * @brief Set callback for double-click events
 * 
 * @param handle Button handle
 * @param callback Callback function (can be NULL to disable)
 */
void button_set_double_click_callback(button_handle_t* handle, button_callback_t callback);

/**
 * @brief Enable or disable repeat for long press
 * 
 * When enabled, the long press callback will be called repeatedly
 * at BUTTON_REPEAT_INTERVAL_MS intervals while button is held.
 * 
 * @param handle Button handle
 * @param enable true to enable repeat, false to disable
 */
void button_set_repeat_enabled(button_handle_t* handle, bool enable);

/**
 * @brief Wait (blocking) until any button press is detected
 * 
 * This function blocks until either a short or long press is detected.
 * Useful for simple applications that need to wait for user input.
 * 
 * @param handle Button handle
 * @return Type of press detected (short or long)
 * 
 * @note This function blocks the calling task
 */
button_press_type_t button_wait_for_press(button_handle_t* handle);

/**
 * @brief Get current button state
 * 
 * @param handle Button handle
 * @return true if button is currently pressed, false otherwise
 */
bool button_is_pressed(button_handle_t* handle);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_H
