/**
 * @file lv_common.h
 *
 * LVGL 9.5: include switched to "lvgl.h" (covers lv_color_t / lv_opa_t).
 */

#ifndef LV_COMMON_H
#define LV_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lvgl.h"
#include <stdint.h>


/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    float x;
    float y;
} lv_temp_t;

/*********************
 *      DEFINES
 *********************/

/* v9 dropped the chart-specific LV_PART_TICKS part. Re-declare it as a custom
 * part (same mechanism LVGL itself used in v8's lv_chart.h) so the hourly/daily
 * chart widgets can keep a dedicated style part for their tick labels, separate
 * from LV_PART_ITEMS (div lines, columns). */
#ifndef LV_PART_TICKS
#define LV_PART_TICKS LV_PART_CUSTOM_FIRST
#endif

/**
 * Get the mapped of a number given an input and output range
 * @param x integer which mapped value should be calculated
 * @param min_in min input range
 * @param max_in max input range
 * @param min_out max output range
 * @param max_out max output range
 * @return the mapped number
 */
int32_t lv_map_float(float x, int32_t min_in, int32_t max_in, int32_t min_out, int32_t max_out);

float cubicInterpolation(lv_temp_t points[], int numPoints, float x);

lv_color_t map_dewpoint_to_color(float value);

lv_opa_t map_value_to_opacity(uint8_t value);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
