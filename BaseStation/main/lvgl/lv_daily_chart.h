/**
 * @file lv_daily_chart.h
 *
 * LVGL 9.5 changes:
 *  - Include switched to "lvgl.h".
 *  - lv_obj_t is used as a by-value base member of lv_daily_chart_t, which in v9
 *    requires the private object definition -> "core/lv_obj_private.h".
 *    Adjust the path prefix to your include setup if needed (see MIGRATION.md).
 *  - The LV_DAILY_CHART_DRAW_PART_* enum is obsolete in v9 (the
 *    lv_obj_draw_part_dsc_t / DRAW_PART_BEGIN/END mechanism was removed). It is
 *    kept only for source compatibility and is no longer referenced.
 */

#ifndef LV_DAILY_CHART_H
#define LV_DAILY_CHART_H

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

#define LV_DAILY_CHART_LABEL_MAX_TEXT_LENGTH 16

/* Anzahl Forecast-Tage, die der Chart darstellt - fest verdrahtet, weil
 * lv_daily_chart_t.data_array[] eine feste Groesse braucht (kein
 * dynamisches Array). weather_task.c fragt beim Provider exakt NUM_DAYS
 * Tage ab, damit Fetch-Puffer und Chart-Kapazitaet 1:1 zusammenpassen. */
#define NUM_DAYS 8
#define MAX_DAILY_PRECIPITATION 20

/**********************
 *      TYPEDEFS
 **********************/
// Daily Weather data structure
typedef struct {
    struct tm dt;
    double low_temp;
    double high_temp;
    double rain;
    double snow;
    uint8_t pop;
    double sun;
} lv_daily_data;


/*Data of line*/
typedef struct {
    lv_obj_t obj;

    lv_daily_data data_array[NUM_DAYS];
    bool has_data;

    int32_t max_temp;
    int32_t min_temp;
    uint32_t ticks_temp;
    int32_t max_precipitation;
    int32_t min_precipitation;
    uint32_t ticks_precipitation;

} lv_daily_chart_t;

/**
 * Enumeration of the axis'
 */
enum {
    LV_DAILY_CHART_AXIS_PRIMARY_Y     = 0x00,
    LV_DAILY_CHART_AXIS_SECONDARY_Y   = 0x01,
    _LV_DAILY_CHART_AXIS_LAST
};
typedef uint8_t lv_daily_chart_axis_t;


/**
 * OBSOLETE in LVGL 9: the lv_obj_draw_part_dsc_t / LV_EVENT_DRAW_PART_BEGIN/END
 * mechanism was removed. Kept only so existing references still compile; it is
 * no longer used by lv_daily_chart.c.
 */
typedef enum {
    LV_DAILY_CHART_DRAW_PART_DIV_LINE_HOR,     /**< horizontal division lines*/
    LV_DAILY_CHART_DRAW_PART_TEMP,             /**< temperature lines*/
    LV_DAILY_CHART_DRAW_PART_CLOUDS,           /**< cloud rectangles*/
    LV_DAILY_CHART_DRAW_PART_PRECIPITATION,
    LV_DAILY_CHART_DRAW_PART_TICK_LABEL,       /**< tick labels*/
} lv_daily_chart_draw_part_type_t;

extern const lv_obj_class_t lv_daily_chart_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a daily chart object
 * @param parent pointer to an object, it will be the parent of the new chart
 * @return pointer to the created chart
 */
lv_obj_t * lv_daily_chart_create(lv_obj_t * parent);

/*=====================
 * Setter functions
 *====================*/

/**
 * Set the daily weather data. The values are copied into the widget.
 * @param obj   pointer to a daily chart object
 * @param data  array of NUM_DAYS lv_daily_data entries
 */
void lv_daily_chart_set_data(lv_obj_t * obj, const lv_daily_data *data);


void lv_daily_chart_refresh(lv_obj_t * obj);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_DAILY_CHART_H*/
