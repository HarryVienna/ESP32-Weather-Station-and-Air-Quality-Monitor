#include "gui_history_chart.h"

/* Bar chart draw callback for history_chart_init() - colors each 1px bar
 * via level_color_asc() according to its threshold. user_data is a
 * `const color_thresh_t *arr[]` with exactly one entry. */
static void chart_bar_fill_cb(lv_event_t *e)
{
    lv_draw_task_t *draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);
    if (base_dsc->part != LV_PART_ITEMS) return;

    lv_draw_fill_dsc_t *fill_dsc = lv_draw_task_get_fill_dsc(draw_task);
    if (!fill_dsc) return;

    lv_obj_t *chart = lv_event_get_target_obj(e);
    lv_chart_series_t *ser = lv_chart_get_series_next(chart, NULL);
    if (!ser) return;

    uint32_t  pt_cnt = lv_chart_get_point_count(chart);
    uint32_t  start  = lv_chart_get_x_start_point(chart, ser);
    int32_t  *y      = lv_chart_get_series_y_array(chart, ser);
    int32_t   val    = y[(start + base_dsc->id2) % pt_cnt];
    if (val == LV_CHART_POINT_NONE) return;

    const color_thresh_t *thresh = ((const color_thresh_t **)lv_event_get_user_data(e))[0];
    fill_dsc->color = level_color_asc((float)val, thresh);
}

void history_chart_init(history_chart_t *hc, lv_obj_t *chart, int32_t y_max,
                         lv_color_t series_color, const color_thresh_t **thresh_arr,
                         uint32_t history_samples_per_day, float scale)
{
  lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, y_max);

  uint32_t point_count = lv_obj_get_width(chart);
  if (point_count < 1) point_count = 1;
  lv_chart_set_point_count(chart, point_count);

  /* lv_chart's plot area is only inset by padding, not by the border
   * width - without this padding the bars draw all the way to the edge
   * of the box and thus right over the border (most visible at the
   * bottom/right, where value=max or the most recent bars sit). Padding =
   * border width leaves enough room to avoid that. */
  int32_t border_width = lv_obj_get_style_border_width(chart, LV_PART_MAIN);
  lv_obj_set_style_pad_all(chart, border_width, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_column(chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

  hc->chart = chart;
  hc->series = lv_chart_add_series(chart, series_color, LV_CHART_AXIS_PRIMARY_Y);
  hc->bucket_samples = (uint16_t)((history_samples_per_day + point_count - 1) / point_count);
  hc->sample_count = 0;
  hc->bucket_max = 0.0f;
  hc->scale = scale;

  lv_obj_add_flag(chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
  lv_obj_add_event_cb(chart, chart_bar_fill_cb, LV_EVENT_DRAW_TASK_ADDED, thresh_arr);
}

void history_chart_push(history_chart_t *hc, float value)
{
  if (value > hc->bucket_max) hc->bucket_max = value;

  hc->sample_count++;
  if (hc->sample_count >= hc->bucket_samples) {
    lv_chart_set_next_value(hc->chart, hc->series, (int32_t)(hc->bucket_max * hc->scale + 0.5f));
    hc->bucket_max = 0.0f;
    hc->sample_count = 0;
  }
}
