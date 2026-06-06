#pragma once

#include <stdint.h>

// Configuration structure
typedef struct {
    int gpio_pin;
    float conversion_factor;  // e.g., 151.0 for J321
    int rolling_avg_seconds;  // e.g., 60 or 300
} geiger_config_t;

/**
 * @brief Initializes the Geiger counter and starts the background task
 * @param config Pointer to the configuration
 */
void geiger_init(const geiger_config_t *config);

/**
 * @brief Returns the current µSv/h value
 */
float geiger_get_usvh(void);

/**
 * @brief Returns the current CPM (Counts Per Minute)
 */
float geiger_get_cpm(void);