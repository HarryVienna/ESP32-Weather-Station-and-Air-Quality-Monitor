#ifndef GUI_COLOR_SCALE_H
#define GUI_COLOR_SCALE_H

#include "lvgl.h"

/* Wert->Farbe-Skalen, gebraucht von SEN66 (gui_sen66.c), den 6 Fernsensoren
 * (gui_sensors.c) und gui_status.c (WLAN-Icon). */

// Farbskalen-Schwellwerte. t1..t4 sind die Grenzen fuer Gruen/Hellgruen/
// Gelb/Orange - alles jenseits von t4 ist Rot. Ob t1 der beste (niedrigste)
// oder schlechteste Wert ist, hängt davon ab, welche Funktion die Werte
// benutzt (level_color_desc(): absteigend, hoeher=besser;
// level_color_asc(): aufsteigend, hoeher=schlechter).
typedef struct {
    float t1, t2, t3, t4;
} color_thresh_t;

// Einzelliger Li-Ion/LiPo-Akku: voll ~4.2V, leer/Abschaltung ~3.0V
static const color_thresh_t THRESH_BATTERY_VOLTAGE = {4.0f, 3.8f, 3.6f, 3.4f};
// RSSI in dBm, gemeinsame Skala fuer LoRa, ESP-NOW und WLAN
static const color_thresh_t THRESH_RSSI_DBM = {-70.0f, -85.0f, -95.0f, -105.0f};

// SEN66-Luftqualitaets-Schwellwerte
static const color_thresh_t THRESH_PM1   = {11.6f,   32.0f,  50.0f,   68.0f};
static const color_thresh_t THRESH_PM2P5 = {13.0f,   35.0f,  55.0f,   75.0f};
static const color_thresh_t THRESH_PM4   = {14.4f,   38.0f,  60.0f,   82.0f};
static const color_thresh_t THRESH_PM10  = {20.0f,   50.0f,  80.0f,   110.0f};
static const color_thresh_t THRESH_VOC   = {50.0f,   150.0f, 250.0f,  400.0f};
static const color_thresh_t THRESH_NOX   = { 1.0f,   20.0f,  150.0f,  300.0f};
static const color_thresh_t THRESH_CO2   = {600.0f, 1000.0f, 1500.0f, 1900.0f};

// Geigerzaehler - Radioaktivitaet, aufsteigend: hoeher = schlechter. In
// Centi-µSv/h (Wert*100) statt µSv/h, weil das Verlaufschart die Werte als
// int32 speichert und echte µSv/h (~0.05-1.0) dabei fast immer auf 0 runden
// wuerden (siehe render_radiation() in gui_sensors.c). Rot ist NICHT der
// akut gefaehrliche Wert, sondern ein Wert der schon deutlich (~5-10x) ueber
// dem natuerlichen Untergrund liegt (Deutschland typ. 0.06-0.20 µSv/h):
// 20/30/50/100 entsprechen 0.20/0.30/0.50/1.00 µSv/h.
static const color_thresh_t THRESH_RADIATION = {20.0f, 30.0f, 50.0f, 100.0f};

/* Absteigend: t1 = bester (hoechster) Wert. Fuer Messgroessen bei denen ein
 * hoeherer Wert besser ist (Akkuspannung, RSSI). */
lv_color_t level_color_desc(float val, const color_thresh_t *t);

/* Aufsteigend: t1 = bester (niedrigster) Wert. Fuer Messgroessen bei denen
 * ein hoeherer Wert schlechter ist (Feinstaub, VOC, NOx, CO2,
 * Radioaktivitaet). Gegenstueck zu level_color_desc() oben. */
lv_color_t level_color_asc(float val, const color_thresh_t *t);

#endif /* GUI_COLOR_SCALE_H */
