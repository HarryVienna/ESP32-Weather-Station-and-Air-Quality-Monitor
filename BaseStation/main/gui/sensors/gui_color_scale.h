#ifndef GUI_COLOR_SCALE_H
#define GUI_COLOR_SCALE_H

#include "lvgl.h"

/* Value->color scales, used by SEN66 (gui_sen66.c), the 6 remote sensors
 * (gui_sensors.c) and gui_status.c (WiFi icon). */

// Color scale thresholds. t1..t4 are the boundaries for green/light green/
// yellow/orange - anything beyond t4 is red. Whether t1 is the best (lowest)
// or worst value depends on which function consumes the values
// (level_color_desc(): descending, higher=better;
// level_color_asc(): ascending, higher=worse).
typedef struct {
    float t1, t2, t3, t4;
} color_thresh_t;

// Single-cell Li-Ion/LiPo battery: full ~4.2V, empty/cutoff ~3.0V
static const color_thresh_t THRESH_BATTERY_VOLTAGE = {4.0f, 3.8f, 3.6f, 3.4f};
// RSSI in dBm, shared scale for LoRa, ESP-NOW and WiFi
static const color_thresh_t THRESH_RSSI_DBM = {-70.0f, -85.0f, -95.0f, -105.0f};

// SEN66 air quality thresholds
static const color_thresh_t THRESH_PM1   = {11.6f,   32.0f,  50.0f,   68.0f};
static const color_thresh_t THRESH_PM2P5 = {13.0f,   35.0f,  55.0f,   75.0f};
static const color_thresh_t THRESH_PM4   = {14.4f,   38.0f,  60.0f,   82.0f};
static const color_thresh_t THRESH_PM10  = {20.0f,   50.0f,  80.0f,   110.0f};
static const color_thresh_t THRESH_VOC   = {50.0f,   150.0f, 250.0f,  400.0f};
static const color_thresh_t THRESH_NOX   = { 1.0f,   20.0f,  150.0f,  300.0f};
static const color_thresh_t THRESH_CO2   = {600.0f, 1000.0f, 1500.0f, 1900.0f};

// Geiger counter - radioactivity, ascending: higher = worse. In
// centi-uSv/h (value*100) instead of uSv/h, because the history chart
// stores values as int32 and real uSv/h (~0.05-1.0) would almost always
// round down to 0 (see render_radiation() in gui_sensors.c). Red is NOT
// the acutely dangerous value, but a value that is already clearly
// (~5-10x) above the natural background (Germany typ. 0.06-0.20 uSv/h):
// 20/30/50/100 correspond to 0.20/0.30/0.50/1.00 uSv/h.
static const color_thresh_t THRESH_RADIATION = {20.0f, 30.0f, 50.0f, 100.0f};

/* Descending: t1 = best (highest) value. For measurements where a higher
 * value is better (battery voltage, RSSI). */
lv_color_t level_color_desc(float val, const color_thresh_t *t);

/* Ascending: t1 = best (lowest) value. For measurements where a higher
 * value is worse (particulate matter, VOC, NOx, CO2, radioactivity).
 * Counterpart to level_color_desc() above. */
lv_color_t level_color_asc(float val, const color_thresh_t *t);

#endif /* GUI_COLOR_SCALE_H */
