/**
 * @file lv_hourly_chart.h
 *
 * LVGL 9.5 changes:
 *  - Include switched to "lvgl.h".
 *  - lv_obj_t is used as a by-value base member of lv_hourly_chart_t, which in v9
 *    requires the private object definition -> "core/lv_obj_private.h".
 *    Adjust the path prefix to your include setup if needed (see MIGRATION.md).
 *  - The LV_HOURLY_CHART_DRAW_PART_* enum is obsolete in v9 and kept only for
 *    source compatibility.
 */

#ifndef LV_HOURLY_CHART_H
#define LV_HOURLY_CHART_H

#ifdef __cplusplus
extern "C" {
#endif



/*********************
 *      INCLUDES
 *********************/
#include "lvgl.h"
#include "core/lv_obj_private.h"   /* lv_obj_t as by-value base member */
#include <time.h>

/*********************
 *      DEFINES
 *********************/

#define LV_HOURLY_CHART_LABEL_MAX_TEXT_LENGTH 16

/* Number of forecast hours the chart displays - hardwired because
 * lv_hourly_chart_t.data_array[] needs a fixed size (no dynamic array).
 * weather_task.c requests exactly NUM_HOURS hours from the provider, so
 * the fetch buffer and chart capacity match 1:1. */
#define NUM_HOURS 48
#define MAX_HOURLY_PRECIPITATION 5

/**********************
 *      TYPEDEFS
 **********************/
// Hourly Weather data structure
typedef struct {
    struct tm dt;
    double temp;
    double dew;
    double rain;
    double snow;
    uint8_t pop;
    double sun;
} lv_hourly_data;


/*Data of line*/
typedef struct {
    lv_obj_t obj;

    lv_hourly_data data_array[NUM_HOURS];
    bool has_data;

    int32_t max_temp; // next upper value by 5
    int32_t min_temp; // next lower value by 5
    uint32_t ticks_temp;
    int32_t max_precipitation;
    int32_t min_precipitation;
    uint32_t ticks_precipitation;

} lv_hourly_chart_t;

/**
 * Enumeration of the axis'
 */
enum {
    LV_HOURLY_CHART_AXIS_PRIMARY_Y     = 0x00,
    LV_HOURLY_CHART_AXIS_SECONDARY_Y   = 0x01,
    _LV_HOURLY_CHART_AXIS_LAST
};
typedef uint8_t lv_hourly_chart_axis_t;


/**
 * OBSOLETE in LVGL 9: the lv_obj_draw_part_dsc_t / LV_EVENT_DRAW_PART_BEGIN/END
 * mechanism was removed. Kept only so existing references still compile; it is
 * no longer used by lv_hourly_chart.c.
 */
typedef enum {
    LV_HOURLY_CHART_DRAW_PART_DIV_LINE_HOR,     /**< horizontal division lines*/
    LV_HOURLY_CHART_DRAW_PART_TEMP,             /**< temperature lines*/
    LV_HOURLY_CHART_DRAW_PART_CLOUDS,           /**< cloud rectangles*/
    LV_HOURLY_CHART_DRAW_PART_PRECIPITATION,
    LV_HOURLY_CHART_DRAW_PART_TICK_LABEL,       /**< tick labels*/
} lv_hourly_chart_draw_part_type_t;

extern const lv_obj_class_t lv_hourly_chart_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create an hourly chart object
 * @param parent pointer to an object, it will be the parent of the new chart
 * @return pointer to the created chart
 */
lv_obj_t * lv_hourly_chart_create(lv_obj_t * parent);

/*=====================
 * Setter functions
 *====================*/

/**
 * Set the hourly weather data. The values are copied into the widget.
 * @param obj   pointer to an hourly chart object
 * @param data  array of NUM_HOURS lv_hourly_data entries
 */
void lv_hourly_chart_set_data(lv_obj_t * obj, const lv_hourly_data *data);


void lv_hourly_chart_refresh(lv_obj_t * obj);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_HOURLY_CHART_H*/
