#ifndef GUI_HISTORY_CHART_H
#define GUI_HISTORY_CHART_H

#include "lvgl.h"

#include "gui_color_scale.h"

/* ============================================================================
 * 24h history chart with peak bucketing (SEN66 PM/VOC/NOx/CO2, Geiger counter)
 *
 * Each sensor delivers readings far more often than the narrow chart widget
 * (approx. 130-200px) can render bars for, yet a full 24h history still needs
 * to fit. So instead of pushing every reading individually, each bar stores
 * the MAXIMUM of all readings since the last bar (rather than e.g. the
 * average) - short spikes don't get lost the way they would with an
 * averaged value.
 * ============================================================================ */
typedef struct {
  lv_obj_t          *chart;
  lv_chart_series_t *series;
  uint16_t bucket_samples;  /* readings per bar, see history_chart_init() */
  uint16_t sample_count;    /* readings in the current bucket so far */
  float    bucket_max;      /* maximum so far in the current bucket */
  float    scale;           /* multiplier before rounding to int32 */
} history_chart_t;

/**
 * @brief  Initializes a 24h history chart with peak bucketing (see above).
 *
 * @param  hc                       state, must live as long as the chart
 * @param  chart                    the LVGL chart object
 * @param  y_max                    upper bound of the Y axis (lower is always 0)
 * @param  series_color             bar color - overridden anyway by the
 *                                   internal per-bar threshold coloring,
 *                                   just a fallback
 * @param  thresh_arr               color_thresh_t* array with exactly one
 *                                   entry, for coloring the bars by
 *                                   threshold - must live as long as the
 *                                   chart
 * @param  history_samples_per_day  number of readings/day at the sensor's
 *                                   send rate (e.g. RADIATION_HISTORY_SAMPLES_PER_DAY
 *                                   from gui_sensors.h at 1x/minute,
 *                                   SEN66_HISTORY_SAMPLES_PER_DAY from
 *                                   gui_sen66.h at the SEN66's 10s cadence)
 * @param  scale                    multiplier before rounding to int32
 *                                   (e.g. 100.0f for uSv/h -> centi-uSv/h,
 *                                   1.0f if the native value range already
 *                                   fits)
 */
void history_chart_init(history_chart_t *hc, lv_obj_t *chart, int32_t y_max,
                         lv_color_t series_color, const color_thresh_t **thresh_arr,
                         uint32_t history_samples_per_day, float scale);

/* New reading - collected as a candidate for the bucket maximum and only
 * pushed once a bar is full (see history_chart_init()). */
void history_chart_push(history_chart_t *hc, float value);

#endif /* GUI_HISTORY_CHART_H */
