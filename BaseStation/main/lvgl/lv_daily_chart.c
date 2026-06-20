/**
 * @file lv_daily_chart.c
 *
 * Migrated to LVGL 9.5
 * ---------------------
 * Key changes vs. the original v8.4 code:
 *  - The whole lv_obj_draw_part_dsc_t / LV_EVENT_DRAW_PART_BEGIN/END mechanism
 *    was removed in v9 and is no longer needed here (no external listeners).
 *  - lv_draw_ctx_t  ->  lv_layer_t  (obtained via lv_event_get_layer()).
 *  - Geometry for lines moved INTO lv_draw_line_dsc_t (p1/p2 as lv_point_precise_t),
 *    lv_draw_line(layer, &dsc).
 *  - lv_draw_label(layer, &dsc, &area): the text now lives in dsc.text. Because we
 *    pass stack buffers and draw tasks are dispatched asynchronously, dsc.text_local
 *    must be set to 1 so LVGL copies the string.
 *  - lv_draw_rect(layer, &dsc, &area).
 *  - obj->coords is private in v9 -> use lv_obj_get_coords(obj, &area).
 *  - lv_txt_get_size -> lv_text_get_size, lv_res_t -> lv_result_t,
 *    LV_RES_OK -> LV_RESULT_OK.
 */

/*********************
 *      INCLUDES
 *********************/
#include <stdio.h>
#include <math.h>

#include "esp_log.h"

#include "lv_daily_chart.h"
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
#define MY_CLASS &lv_daily_chart_class

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_daily_chart_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_daily_chart_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_daily_chart_event(const lv_obj_class_t * class_p, lv_event_t * e);

static void draw_daily_y_ticks(lv_obj_t * obj, lv_layer_t * layer, lv_daily_chart_axis_t axis);
static void draw_daily_x_ticks(lv_obj_t * obj, lv_layer_t * layer);
static void draw_daily_div_lines(lv_obj_t * obj, lv_layer_t * layer);
static void draw_daily_clouds(lv_obj_t * obj, lv_layer_t * layer);
static void draw_daily_temp(lv_obj_t * obj, lv_layer_t * layer);
static void draw_daily_precipitation(lv_obj_t * obj, lv_layer_t * layer);

/**********************
 *  STATIC VARIABLES
 **********************/
const lv_obj_class_t lv_daily_chart_class = {
    .constructor_cb = lv_daily_chart_constructor,
    .destructor_cb  = lv_daily_chart_destructor,
    .event_cb       = lv_daily_chart_event,
    .width_def      = LV_PCT(100),
    .height_def     = LV_DPI_DEF * 2,
    .instance_size  = sizeof(lv_daily_chart_t),
    .base_class     = &lv_obj_class
};

static const char* TAG = "daily_chart";

/**********************
 *      MACROS
 **********************/


/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * lv_daily_chart_create(lv_obj_t * parent)
{
    ESP_LOGI(TAG, "lv_daily_chart_create");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

void lv_daily_chart_refresh(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_obj_invalidate(obj);
}

/*=====================
 * Setter functions
 *====================*/

void lv_daily_chart_set_data(lv_obj_t * obj, const lv_daily_data *data)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    if (obj == NULL || data == NULL) {
        // Handle invalid input parameters
        return;
    }

    lv_daily_chart_t * chart = (lv_daily_chart_t *)obj;

    float max_temp = -FLT_MAX;
    float min_temp = FLT_MAX;

    for (int i = 0; i < NUM_DAYS; i++) {
        chart->data_array[i] = data[i];

        if (data[i].high_temp > max_temp) {
            max_temp = data[i].high_temp;
        }
        if (data[i].low_temp < min_temp) {
            min_temp = data[i].low_temp;
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

    chart->max_precipitation = MAX_DAILY_PRECIPITATION;  // Precipitation is fix
    chart->min_precipitation = 0;
    chart->ticks_precipitation = 5;

    chart->has_data = true;

    lv_daily_chart_refresh(obj);
}



/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_daily_chart_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    lv_daily_chart_t * chart = (lv_daily_chart_t *)obj;

    chart->has_data = false;
}

static void lv_daily_chart_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    lv_daily_chart_t * chart = (lv_daily_chart_t *)obj;
    LV_UNUSED(chart);
}


static void draw_daily_y_ticks(lv_obj_t * obj, lv_layer_t * layer, lv_daily_chart_axis_t axis)
{
    lv_daily_chart_t * chart  = (lv_daily_chart_t *)obj;

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
    if(axis == LV_DAILY_CHART_AXIS_PRIMARY_Y) {
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

        char buf[LV_DAILY_CHART_LABEL_MAX_TEXT_LENGTH];
        lv_snprintf(buf, sizeof(buf), "%ld", (long)tick_value);

        lv_point_t size;
        lv_text_get_size(&size, buf, label_dsc.font, label_dsc.letter_space,
                         label_dsc.line_space, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        lv_area_t a;
        a.y1 = py - size.y / 2;
        a.y2 = py + size.y / 2;

        if(axis == LV_DAILY_CHART_AXIS_PRIMARY_Y) {
            a.x1 = px - size.x - label_gap;
            a.x2 = px - label_gap;
        }
        else {
            a.x1 = px + label_gap;
            a.x2 = px + size.x + label_gap;
        }

        label_dsc.text = buf;
        label_dsc.text_local = 1;   /* buf is on the stack -> let LVGL copy it */
        lv_draw_label(layer, &label_dsc, &a);
    }
}

static void draw_daily_x_ticks(lv_obj_t * obj, lv_layer_t * layer)
{
    lv_daily_chart_t * chart  = (lv_daily_chart_t *)obj;

    if (!chart->has_data) {
        return;
    }

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);

    int32_t w     = lv_obj_get_content_width(obj);

    int32_t label_gap = lv_obj_get_style_pad_bottom(obj, LV_PART_TICKS);
    int32_t block_gap = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);  /*Gap between the columns on ~adjacent X*/

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    lv_obj_init_draw_label_dsc(obj, LV_PART_TICKS, &label_dsc);

    int32_t x_ofs = obj_coords.x1;
    int32_t y_ofs = obj_coords.y2;

    int32_t block_w = (w + block_gap) / (NUM_DAYS);

    x_ofs += (block_w - block_gap) / 2;
    w -= block_w - block_gap;

    for(uint32_t i = 0; i < NUM_DAYS; i++) {

        int32_t px = x_ofs + (int32_t)(w * i) / (NUM_DAYS - 1);
        int32_t py = y_ofs;

        char day_name[4];
        struct tm dt = chart->data_array[i].dt;
        int wday = dt.tm_wday;
        if (wday < 0 || wday > 6) wday = 0;
        snprintf(day_name, sizeof(day_name), "%s", DAY_NAMES[wday]);

        lv_point_t size;
        lv_text_get_size(&size, day_name, label_dsc.font, label_dsc.letter_space,
                         label_dsc.line_space, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        lv_area_t a;
        a.x1 = (px - size.x / 2);
        a.x2 = (px + size.x / 2);
        a.y1 = py + label_gap;
        a.y2 = a.y1 + size.y;

        label_dsc.text = day_name;
        label_dsc.text_local = 1;   /* day_name is on the stack */
        lv_draw_label(layer, &label_dsc, &a);
    }
}

static void draw_daily_div_lines(lv_obj_t * obj, lv_layer_t * layer)
{
    lv_daily_chart_t * chart  = (lv_daily_chart_t *)obj;

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

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    lv_obj_init_draw_line_dsc(obj, LV_PART_ITEMS, &line_dsc);
    line_dsc.color = lv_color_hex(0x000000);
    line_dsc.opa = LV_OPA_50;
    line_dsc.width = 2;

    for(uint32_t i = 0; i < ticks_cnt; i++) {
        int32_t py = y_ofs + (int32_t)(h * i) / (ticks_cnt - 1);

        line_dsc.p1.x = x_ofs;
        line_dsc.p1.y = py;
        line_dsc.p2.x = x_ofs + w;
        line_dsc.p2.y = py;

        lv_draw_line(layer, &line_dsc);
    }
}

static void draw_daily_clouds(lv_obj_t * obj, lv_layer_t * layer)
{
    lv_daily_chart_t * chart  = (lv_daily_chart_t *)obj;

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

    for(uint32_t i = 0; i < NUM_DAYS; i++) {
        col_area.x1 = x_ofs + (int32_t)(w * i / NUM_DAYS) + pad_col;
        col_area.y1 = y1;
        col_area.x2 = x_ofs + (int32_t)(w * (i+1) / NUM_DAYS) - pad_col;
        col_area.y2 = y2;

        col_dsc.bg_opa = chart->data_array[i].sun * 255;

        lv_draw_rect(layer, &col_dsc, &col_area);
    }
}


static void draw_daily_temp(lv_obj_t * obj, lv_layer_t * layer)
{
    lv_daily_chart_t * chart  = (lv_daily_chart_t *)obj;

    if (!chart->has_data) {
        return;
    }

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);

    int32_t w        = lv_obj_get_content_width(obj);
    int32_t h        = lv_obj_get_content_height(obj);
    int32_t pad_col  = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);  /*Gap on left and right side of the bars*/

    int32_t x_ofs = obj_coords.x1;
    int32_t y_ofs = obj_coords.y1;

    int32_t min = chart->min_temp;
    int32_t max = chart->max_temp;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    lv_obj_init_draw_line_dsc(obj, LV_PART_ITEMS, &line_dsc);
    line_dsc.width = 5;
    line_dsc.round_start = 0;
    line_dsc.round_end = 0;
    line_dsc.raw_end = 0;

    for(uint32_t i = 0; i < NUM_DAYS; i++) {
        int32_t px1 = x_ofs + (int32_t)(w * i / NUM_DAYS) + pad_col;
        int32_t px2 = x_ofs + (int32_t)(w * (i+1) / NUM_DAYS) - pad_col + 1; // for some reason it is one pixel smaller than clouds, so +1

        // High temperature
        int32_t high_value = h - lv_map_float(chart->data_array[i].high_temp, min, max, 0, h);

        line_dsc.p1.x = px1;
        line_dsc.p1.y = y_ofs + high_value;
        line_dsc.p2.x = px2;
        line_dsc.p2.y = y_ofs + high_value;
        line_dsc.color = lv_color_hex(COLOR_RED);
        lv_draw_line(layer, &line_dsc);

        // Low temperature
        int32_t low_value = h - lv_map_float(chart->data_array[i].low_temp, min, max, 0, h);

        line_dsc.p1.x = px1;
        line_dsc.p1.y = y_ofs + low_value;
        line_dsc.p2.x = px2;
        line_dsc.p2.y = y_ofs + low_value;
        line_dsc.color = lv_color_hex(COLOR_DARKBLUE);
        lv_draw_line(layer, &line_dsc);
    }
}

static void draw_daily_precipitation(lv_obj_t * obj, lv_layer_t * layer)
{
    lv_daily_chart_t * chart  = (lv_daily_chart_t *)obj;

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

    for(uint32_t i = 0; i < NUM_DAYS; i++) {
        int32_t px1 = x_ofs + (int32_t)(w * i / NUM_DAYS) + pad_col;
        int32_t px2 = x_ofs + (int32_t)(w * (i+1) / NUM_DAYS) - pad_col;

        float rain = chart->data_array[i].rain;
        float snow = chart->data_array[i].snow;
        uint8_t pop = chart->data_array[i].pop;

        if (rain + snow > MAX_DAILY_PRECIPITATION) {
            float factor = MAX_DAILY_PRECIPITATION / (rain + snow);
            rain = rain * factor;
            snow = snow * factor;
        }

        int32_t rain_value = lv_map_float(rain, 0, MAX_DAILY_PRECIPITATION, 0, h);
        int32_t snow_value = lv_map_float(snow, 0, MAX_DAILY_PRECIPITATION, 0, h);


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
            col_dsc.bg_opa = map_value_to_opacity(pop);
            lv_draw_rect(layer, &col_dsc, &col_area);
        }
    }
}

static void lv_daily_chart_event(const lv_obj_class_t * class_p, lv_event_t * e)
{
    LV_UNUSED(class_p);

    lv_result_t res;

    res = lv_obj_event_base(MY_CLASS, e);
    if(res != LV_RESULT_OK) return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);

    lv_daily_chart_t * chart  = (lv_daily_chart_t *)obj;

    if(code == LV_EVENT_DRAW_MAIN) {
        lv_layer_t * layer = lv_event_get_layer(e);

        if (chart->has_data) {
            draw_daily_y_ticks(obj, layer, LV_DAILY_CHART_AXIS_PRIMARY_Y);
            draw_daily_y_ticks(obj, layer, LV_DAILY_CHART_AXIS_SECONDARY_Y);
            draw_daily_x_ticks(obj, layer);

            draw_daily_clouds(obj, layer);
            draw_daily_precipitation(obj, layer);
            draw_daily_div_lines(obj, layer);
            draw_daily_temp(obj, layer);
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
