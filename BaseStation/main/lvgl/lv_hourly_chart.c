/**
 * @file lv_hourly_chart.c
 *
 * Migrated to LVGL 9.5  (see header notes in lv_daily_chart.c for the rationale).
 */

/*********************
 *      INCLUDES
 *********************/
#include <stdio.h>
#include <math.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "../config/config.h"
#include "lv_hourly_chart.h"
#include "lv_common.h"

#include "lvgl.h"
/* v9: lv_obj_t as a by-value base member and the lv_obj_class_t initializer
 * require the private definitions. Adjust the path prefix to your include
 * setup if needed (see MIGRATION.md). */
#include "core/lv_obj_class_private.h"
#include "core/lv_obj_private.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <float.h>

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS &lv_hourly_chart_class

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_hourly_chart_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_hourly_chart_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_hourly_chart_event(const lv_obj_class_t * class_p, lv_event_t * e);

static void draw_hourly_y_ticks(lv_obj_t * obj, lv_layer_t * layer, lv_hourly_chart_axis_t axis);
static void draw_hourly_x_ticks(lv_obj_t * obj, lv_layer_t * layer);
static void draw_hourly_div_lines(lv_obj_t * obj, lv_layer_t * layer);
static void draw_hourly_clouds(lv_obj_t * obj, lv_layer_t * layer);
static void draw_hourly_temp(lv_obj_t * obj, lv_layer_t * layer);
static void draw_hourly_precipitation(lv_obj_t * obj, lv_layer_t * layer);

/**********************
 *  STATIC VARIABLES
 **********************/
const lv_obj_class_t lv_hourly_chart_class = {
    .constructor_cb = lv_hourly_chart_constructor,
    .destructor_cb  = lv_hourly_chart_destructor,
    .event_cb       = lv_hourly_chart_event,
    .width_def      = LV_PCT(100),
    .height_def     = LV_DPI_DEF * 2,
    .instance_size  = sizeof(lv_hourly_chart_t),
    .base_class     = &lv_obj_class
};

static const char* TAG = "hourly_chart";

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * lv_hourly_chart_create(lv_obj_t * parent)
{
    ESP_LOGI(TAG, "lv_hourly_chart_create");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

void lv_hourly_chart_refresh(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_obj_invalidate(obj);
}

/*=====================
 * Setter functions
 *====================*/

void lv_hourly_chart_set_data(lv_obj_t * obj, const lv_hourly_data *data)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    if (obj == NULL || data == NULL) {
        // Handle invalid input parameters
        return;
    }

    lv_hourly_chart_t * chart = (lv_hourly_chart_t *)obj;

    float max_temp = -FLT_MAX;
    float min_temp = FLT_MAX;

    for (int i = 0; i < NUM_HOURS; i++) {
        chart->data_array[i] = data[i];

        if (data[i].temp > max_temp) {
            max_temp = data[i].temp;
        }

        if (data[i].temp < min_temp) {
            min_temp = data[i].temp;
        }
    }

    chart->min_temp = (int32_t)(5.0 * floor(min_temp / 5.0)); // next lower 5 value
    chart->max_temp = (int32_t)(5.0 * ceil(max_temp / 5.0));   // next upper 5 value

    if (chart->min_temp == chart->max_temp) {
        chart->min_temp -= 5;
        chart->max_temp += 5;
    }

    chart->ticks_temp = (chart->max_temp - chart->min_temp) / 5 + 1;
    if (chart->ticks_temp == 2) { // e.g. 10° and 15°
        chart->ticks_temp = 6;    // -->  10°, 11°, 12°, 13°, 14°, 15°
    }
    if (chart->ticks_temp < 2) {
        chart->ticks_temp = 2;
    }

    chart->max_precipitation = MAX_HOURLY_PRECIPITATION;  // Precipitation is fix
    chart->min_precipitation = 0;
    chart->ticks_precipitation = 5;

    chart->has_data = true;

    //lv_hourly_chart_refresh(obj);
}



/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_hourly_chart_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    lv_hourly_chart_t * chart = (lv_hourly_chart_t *)obj;

    chart->has_data = false;
}

static void lv_hourly_chart_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    lv_hourly_chart_t * chart = (lv_hourly_chart_t *)obj;
    LV_UNUSED(chart);
}

static void draw_hourly_y_ticks(lv_obj_t * obj, lv_layer_t * layer, lv_hourly_chart_axis_t axis)
{
    lv_hourly_chart_t * chart  = (lv_hourly_chart_t *)obj;

    if (!chart->has_data) {
        return;
    }

    int32_t ticks_cnt;
    int32_t min;
    int32_t max;

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);

    int32_t h     = lv_obj_get_content_height(obj);
    int32_t w     = lv_obj_get_content_width(obj);
    LV_UNUSED(w);

    int32_t y_ofs = obj_coords.y1;

    int32_t label_gap;
    int32_t x_ofs;
    if(axis == LV_HOURLY_CHART_AXIS_PRIMARY_Y) {
        ticks_cnt = chart->ticks_temp;
        min = chart->min_temp;
        max = chart->max_temp;

        label_gap = lv_obj_get_style_pad_left(obj, LV_PART_TICKS);
        x_ofs = obj_coords.x1;
    }
    else {
        ticks_cnt = chart->ticks_precipitation;
        min = chart->min_precipitation;
        max = chart->max_precipitation;

        label_gap = lv_obj_get_style_pad_right(obj, LV_PART_TICKS);
        x_ofs = obj_coords.x2;
    }

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    lv_obj_init_draw_label_dsc(obj, LV_PART_TICKS, &label_dsc);

    for(uint32_t i = 0; i < ticks_cnt; i++) {
        int32_t py = y_ofs + (int32_t)(h * i) / (ticks_cnt - 1);
        int32_t px = x_ofs;

        int32_t tick_value = lv_map(ticks_cnt - 1 - i, 0, (ticks_cnt - 1), min, max);

        char buf[LV_HOURLY_CHART_LABEL_MAX_TEXT_LENGTH];
        lv_snprintf(buf, sizeof(buf), "%ld", (long)tick_value);

        lv_point_t size;
        lv_text_get_size(&size, buf, label_dsc.font, label_dsc.letter_space,
                         label_dsc.line_space, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        lv_area_t a;
        a.y1 = py - size.y / 2;
        a.y2 = py + size.y / 2;

        if(axis == LV_HOURLY_CHART_AXIS_PRIMARY_Y) {
            a.x1 = px - size.x - label_gap;
            a.x2 = px - label_gap;
        }
        else {
            a.x1 = px + label_gap;
            a.x2 = px + size.x + label_gap;
        }

        label_dsc.text = buf;
        label_dsc.text_local = 1;
        lv_draw_label(layer, &label_dsc, &a);
    }
}

static void draw_hourly_x_ticks(lv_obj_t * obj, lv_layer_t * layer)
{
    lv_hourly_chart_t * chart  = (lv_hourly_chart_t *)obj;

    if (!chart->has_data) {
        return;
    }

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);

    int32_t w         = lv_obj_get_content_width(obj);
    int32_t label_gap = lv_obj_get_style_pad_bottom(obj, LV_PART_TICKS);
    int32_t block_w   = w / NUM_HOURS;

    int32_t x_ofs = obj_coords.x1;
    int32_t y_ofs = obj_coords.y2;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    lv_obj_init_draw_line_dsc(obj, LV_PART_ITEMS, &line_dsc);
    line_dsc.color = lv_color_hex(COLOR_BLACK);
    line_dsc.opa = LV_OPA_50;
    line_dsc.width = 2;

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    lv_obj_init_draw_label_dsc(obj, LV_PART_TICKS, &label_dsc);

    x_ofs += block_w / 2;

    for(uint32_t i = 0; i < NUM_HOURS; i++) {
        int32_t px = x_ofs + (int32_t)(w * i / (NUM_HOURS));

        line_dsc.p1.x = px;
        line_dsc.p1.y = y_ofs;
        line_dsc.p2.x = px;
        line_dsc.p2.y = y_ofs + 8;
        lv_draw_line(layer, &line_dsc);

        struct tm dt = chart->data_array[i].dt;
        if (dt.tm_hour % 12 == 0) {

            char time_value[8];
            strftime(time_value, sizeof(time_value), "%H:%M", &dt);

            lv_point_t size;
            lv_text_get_size(&size, time_value, label_dsc.font, label_dsc.letter_space,
                             label_dsc.line_space, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

            lv_area_t a;
            a.x1 = (px - size.x / 2);
            a.x2 = (px + size.x / 2);
            a.y1 = y_ofs + label_gap;
            a.y2 = a.y1 + size.y;

            label_dsc.text = time_value;
            label_dsc.text_local = 1;
            lv_draw_label(layer, &label_dsc, &a);
        }
    }
}

static void draw_hourly_div_lines(lv_obj_t * obj, lv_layer_t * layer)
{
    lv_hourly_chart_t * chart  = (lv_hourly_chart_t *)obj;

    if (!chart->has_data) {
        return;
    }

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);

    int32_t h     = lv_obj_get_content_height(obj);
    int32_t w     = lv_obj_get_content_width(obj);

    int32_t y_ofs = obj_coords.y1;
    int32_t x_ofs = obj_coords.x1;

    int32_t ticks_cnt = chart->ticks_temp;
    int32_t block_w = w / NUM_HOURS;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    lv_obj_init_draw_line_dsc(obj, LV_PART_ITEMS, &line_dsc);
    line_dsc.color = lv_color_hex(COLOR_BLACK);
    line_dsc.opa = LV_OPA_50;
    line_dsc.width = 2;

    /* Horizontal division lines */
    for(uint32_t i = 0; i < ticks_cnt; i++) {
        int32_t py = y_ofs + (int32_t)(h * i) / (ticks_cnt - 1);

        line_dsc.p1.x = x_ofs;
        line_dsc.p1.y = py;
        line_dsc.p2.x = x_ofs + w;
        line_dsc.p2.y = py;
        lv_draw_line(layer, &line_dsc);
    }

    /* Vertical division lines (every 12h dashed, every 24h solid) */
    int32_t xv_ofs = x_ofs + block_w / 2;

    for(uint32_t i = 0; i < NUM_HOURS; i++) {
        int32_t px = xv_ofs + (int32_t)(w * i / NUM_HOURS);

        struct tm dt = chart->data_array[i].dt;

        line_dsc.p1.x = px;
        line_dsc.p1.y = y_ofs;
        line_dsc.p2.x = px;
        line_dsc.p2.y = y_ofs + h;

        if (dt.tm_hour % 24 == 0) {
            line_dsc.dash_gap = 0;
            lv_draw_line(layer, &line_dsc);
        }
        else if (dt.tm_hour % 12 == 0) {
            line_dsc.dash_gap = 4;
            line_dsc.dash_width = 4;
            lv_draw_line(layer, &line_dsc);
        }
    }
}

static void draw_hourly_clouds(lv_obj_t * obj, lv_layer_t * layer)
{
    lv_hourly_chart_t * chart  = (lv_hourly_chart_t *)obj;

    if (!chart->has_data) {
        return;
    }

    lv_area_t col_area;

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);

    int32_t w        = lv_obj_get_content_width(obj);
    int32_t h        = lv_obj_get_content_height(obj);
    int32_t pad_col  = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);  /*Gap on left and right side of the bars*/

    int32_t x_ofs = obj_coords.x1;
    int32_t y_ofs = obj_coords.y1;

    lv_draw_rect_dsc_t col_dsc;
    lv_draw_rect_dsc_init(&col_dsc);
    lv_obj_init_draw_rect_dsc(obj, LV_PART_ITEMS, &col_dsc);
    col_dsc.bg_color = lv_color_hex(COLOR_LIGHTYELLOW);

    int32_t y1 = y_ofs;
    int32_t y2 = y_ofs + h;

    for(uint32_t i = 0; i < NUM_HOURS; i++) {
        col_area.x1 = x_ofs + (int32_t)(w * i / NUM_HOURS) + pad_col;
        col_area.y1 = y1;
        col_area.x2 = x_ofs + (int32_t)(w * (i+1) / NUM_HOURS) - pad_col;
        col_area.y2 = y2;

        col_dsc.bg_opa = chart->data_array[i].sun * 255;

        lv_draw_rect(layer, &col_dsc, &col_area);
    }
}



static void draw_hourly_temp(lv_obj_t * obj, lv_layer_t * layer)
{
    lv_hourly_chart_t * chart  = (lv_hourly_chart_t *)obj;

    if (!chart->has_data) {
        return;
    }

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);

    int32_t w        = lv_obj_get_content_width(obj);
    int32_t h        = lv_obj_get_content_height(obj);

    int32_t x_ofs = obj_coords.x1;
    int32_t y_ofs = obj_coords.y1;

    int32_t min = chart->min_temp;
    int32_t max = chart->max_temp;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    lv_obj_init_draw_line_dsc(obj, LV_PART_ITEMS, &line_dsc);
    line_dsc.width = 4;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;
    line_dsc.raw_end = 0;
    line_dsc.color = lv_color_hex(COLOR_ORANGE);

    lv_temp_t hourly_temps[NUM_HOURS];    // 0 .. NUM_HOURS-1
    lv_temp_t hourly_dews[NUM_HOURS];
    for(uint32_t i = 0; i < NUM_HOURS; i++) {
        hourly_temps[i].x = i;
        hourly_temps[i].y = chart->data_array[i].temp;

        hourly_dews[i].x = i;
        hourly_dews[i].y = chart->data_array[i].dew;
    }

    if (w <= 1) {
        return;
    }

    // Dynamische Speicherallokation für die Arrays
    int32_t    *hourly_temp_values = (int32_t *)heap_caps_malloc(w * sizeof(int32_t), MALLOC_CAP_32BIT | MALLOC_CAP_SPIRAM);
    lv_color_t *hourly_dew_values  = (lv_color_t *)heap_caps_malloc(w * sizeof(lv_color_t), MALLOC_CAP_32BIT | MALLOC_CAP_SPIRAM);

    if (hourly_temp_values == NULL || hourly_dew_values == NULL) {
        heap_caps_free(hourly_temp_values);
        heap_caps_free(hourly_dew_values);
        ESP_LOGE(TAG, "Failed to allocate memory for hourly chart");
        return;
    }

    for(int32_t i = 0; i < w; i++) {
        // map current i to an x in hourly_temps
        float x = (float)i * (NUM_HOURS - 1) / (w - 1);
        float y = cubicInterpolation(hourly_temps, NUM_HOURS, x);
        float dew = cubicInterpolation(hourly_dews, NUM_HOURS, x);

        hourly_temp_values[i] = lv_map_float(y, min, max, 0, h);
        hourly_dew_values[i] = map_dewpoint_to_color(dew);
    }

    for(int32_t i = 0; i < w - 1; i++) {

        line_dsc.p1.x = x_ofs + i;
        line_dsc.p1.y = y_ofs + h - hourly_temp_values[i];
        line_dsc.p2.x = x_ofs + i + 1;
        line_dsc.p2.y = y_ofs + h - hourly_temp_values[i + 1];

        line_dsc.color = hourly_dew_values[i];

        lv_draw_line(layer, &line_dsc);
    }

    // Speicher freigeben
    heap_caps_free(hourly_temp_values);
    heap_caps_free(hourly_dew_values);
}

static void draw_hourly_precipitation(lv_obj_t * obj, lv_layer_t * layer)
{
    lv_hourly_chart_t * chart  = (lv_hourly_chart_t *)obj;

    if (!chart->has_data) {
        return;
    }

    lv_area_t col_area;

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);

    int32_t w        = lv_obj_get_content_width(obj);
    int32_t h        = lv_obj_get_content_height(obj);
    int32_t pad_col  = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);  /*Gap on left and right side of the bars*/

    int32_t x_ofs = obj_coords.x1;
    int32_t y_ofs = obj_coords.y1;

    lv_draw_rect_dsc_t col_dsc;
    lv_draw_rect_dsc_init(&col_dsc);
    lv_obj_init_draw_rect_dsc(obj, LV_PART_ITEMS, &col_dsc);

    for(uint32_t i = 0; i < NUM_HOURS; i++) {
        int32_t px1 = x_ofs + (int32_t)(w * i / NUM_HOURS) + pad_col;
        int32_t px2 = x_ofs + (int32_t)(w * (i+1) / NUM_HOURS) - pad_col;

        float rain = chart->data_array[i].rain;
        float snow = chart->data_array[i].snow;
        uint8_t pop = chart->data_array[i].pop;

        if (rain + snow > MAX_HOURLY_PRECIPITATION) {
            float factor = MAX_HOURLY_PRECIPITATION / (rain + snow);
            rain = rain * factor;
            snow = snow * factor;
        }

        int32_t rain_value = lv_map_float(rain, 0, MAX_HOURLY_PRECIPITATION, 0, h);
        int32_t snow_value = lv_map_float(snow, 0, MAX_HOURLY_PRECIPITATION, 0, h);

        // Rain
        if (rain_value > 0) {

            col_area.x1 = px1;
            col_area.y1 = y_ofs + h - rain_value;
            col_area.x2 = px2;
            col_area.y2 = y_ofs + h;

            col_dsc.bg_color = lv_color_hex(COLOR_WHITE);
            col_dsc.bg_opa = LV_OPA_100;
            lv_draw_rect(layer, &col_dsc, &col_area);

            col_dsc.bg_color = lv_color_hex(COLOR_BLUE);
            col_dsc.bg_opa = map_value_to_opacity(pop);
            lv_draw_rect(layer, &col_dsc, &col_area);
        }

        // Snow
        if (snow_value > 0) {

            col_area.x1 = px1;
            col_area.y1 = y_ofs + h - rain_value - snow_value;
            col_area.x2 = px2;
            col_area.y2 = y_ofs + h - rain_value;

            col_dsc.bg_color = lv_color_hex(COLOR_WHITE);
            col_dsc.bg_opa = LV_OPA_100;
            lv_draw_rect(layer, &col_dsc, &col_area);

            col_dsc.bg_color = lv_color_hex(COLOR_PINK);
            col_dsc.bg_opa =  map_value_to_opacity(pop);
            lv_draw_rect(layer, &col_dsc, &col_area);
        }
    }
}

static void lv_hourly_chart_event(const lv_obj_class_t * class_p, lv_event_t * e)
{
    LV_UNUSED(class_p);

    lv_result_t res;

    res = lv_obj_event_base(MY_CLASS, e);
    if(res != LV_RESULT_OK) return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);

    lv_hourly_chart_t * chart  = (lv_hourly_chart_t *)obj;

    if(code == LV_EVENT_DRAW_MAIN) {
        lv_layer_t * layer = lv_event_get_layer(e);

        if (chart->has_data) {
            draw_hourly_y_ticks(obj, layer, LV_HOURLY_CHART_AXIS_PRIMARY_Y);
            draw_hourly_y_ticks(obj, layer, LV_HOURLY_CHART_AXIS_SECONDARY_Y);
            draw_hourly_x_ticks(obj, layer);

            draw_hourly_clouds(obj, layer);
            draw_hourly_precipitation(obj, layer);
            draw_hourly_div_lines(obj, layer);
            draw_hourly_temp(obj, layer);
        }
    }
    else if(code == LV_EVENT_GET_SELF_SIZE) {
        lv_point_t * p = lv_event_get_param(e);
        p->x = lv_obj_get_content_width(obj);
        p->y = lv_obj_get_content_height(obj);
    }
    else if(code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        lv_event_set_ext_draw_size(e, 30);
    }
    else if(code == LV_EVENT_SIZE_CHANGED) {
        lv_obj_refresh_self_size(obj);
    }
}
