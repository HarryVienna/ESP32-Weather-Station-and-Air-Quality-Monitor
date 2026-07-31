#ifndef GUI_HISTORY_CHART_H
#define GUI_HISTORY_CHART_H

#include "lvgl.h"

#include "gui_color_scale.h"

/* ============================================================================
 * 24h-Verlaufschart mit Peak-Bucketing (SEN66 PM/VOC/NOx/CO2, Geigerzaehler)
 *
 * Jeder Sensor liefert seine Messwerte deutlich haeufiger, als das schmale
 * Chart-Widget (ca. 130-200px) Balken darstellen kann, ein Verlauf ueber
 * echte 24h soll aber trotzdem reinpassen. Deshalb wird nicht jeder Messwert
 * einzeln gepusht, sondern pro Balken das MAXIMUM aller Messwerte seit dem
 * letzten Balken (statt z.B. dem Mittelwert) gespeichert - kurze Spitzen
 * gehen so nicht unter, wie es bei einem gemittelten Wert der Fall
 * waere.
 * ============================================================================ */
typedef struct {
  lv_obj_t          *chart;
  lv_chart_series_t *series;
  uint16_t bucket_samples;  /* Messwerte pro Balken, siehe history_chart_init() */
  uint16_t sample_count;    /* Messwerte im aktuellen Bucket bisher */
  float    bucket_max;      /* bisheriges Maximum im aktuellen Bucket */
  float    scale;           /* Multiplikator vor dem Runden auf int32 */
} history_chart_t;

/**
 * @brief  Initialisiert ein 24h-Verlaufschart mit Peak-Bucketing (siehe oben).
 *
 * @param  hc                       Zustand, muss so lange leben wie der Chart
 * @param  chart                    das LVGL-Chart-Objekt
 * @param  y_max                    obere Grenze der Y-Achse (untere immer 0)
 * @param  series_color             Balkenfarbe - wird von der internen
 *                                   Schwellwertfaerbung pro Balken ohnehin
 *                                   ueberschrieben, nur Fallback
 * @param  thresh_arr               color_thresh_t* Array mit genau einem
 *                                   Eintrag, fuers Einfaerben der Balken nach
 *                                   Schwellwert - muss so lange leben wie
 *                                   der Chart
 * @param  history_samples_per_day  Anzahl Messwerte/Tag bei der Sende-Rate
 *                                   des Sensors (z.B. RADIATION_HISTORY_SAMPLES_PER_DAY
 *                                   aus gui_sensors.h bei 1x/Minute,
 *                                   SEN66_HISTORY_SAMPLES_PER_DAY aus
 *                                   gui_sen66.h bei SEN66s 10s-Takt)
 * @param  scale                    Multiplikator vor dem Runden auf int32
 *                                   (z.B. 100.0f fuer µSv/h -> Centi-µSv/h,
 *                                   1.0f wenn der native Wertebereich schon
 *                                   passt)
 */
void history_chart_init(history_chart_t *hc, lv_obj_t *chart, int32_t y_max,
                         lv_color_t series_color, const color_thresh_t **thresh_arr,
                         uint32_t history_samples_per_day, float scale);

/* Neuer Messwert - wird als Kandidat fuers Bucket-Maximum gesammelt und erst
 * gepusht, wenn ein Balken voll ist (siehe history_chart_init()). */
void history_chart_push(history_chart_t *hc, float value);

#endif /* GUI_HISTORY_CHART_H */
