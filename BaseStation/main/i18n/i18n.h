#pragma once

#include "lvgl.h"

/* EEZ Studios "Translated Literal"-Feature generiert Code wie
 * lv_label_set_text(obj, _("Wohnzimmer")); in screens.c - erwartet also das
 * gettext-uebliche _()-Makro. EEZ generiert diese Definition selbst nicht
 * mit, deshalb hier von Hand.
 *
 * _() macht daraus lv_tr(): schlaegt den Literal-String (der gleichzeitig
 * als Uebersetzungs-"Tag" dient, siehe i18n.c) fuer die aktuell aktive
 * Sprache nach. Ohne aktive Sprache/passenden Tag faellt lv_tr() sicher auf
 * den unveraenderten String zurueck (siehe lv_translation_get() in
 * lv_translation.c) - schlimmstenfalls fehlt also eine Uebersetzung, nichts
 * stuerzt ab.
 *
 * screens.c ist komplett EEZ-generiert, aber der Include-Block am Dateianfang
 * ist Teil des in EEZ Studio editierbaren "LVGL_SCREENS_DEF"-Templates (siehe
 * EEZ-Studio-1280x800/Wetterstation.eez-project) - #include "i18n/i18n.h"
 * steht dort mit drin und uebersteht damit jede Regenerierung. */
#define _(txt) lv_tr(txt)

/**
 * @brief  Initialisiert lv_translation und registriert die Uebersetzungs-
 *         tabelle(n). Muss vor ui_init() aufgerufen werden (main.c), da
 *         beim Aufbau der Screens bereits _()-Aufrufe passieren koennen.
 */
void i18n_init(void);

/* Wochentags-Kurzform: strftime(buf, sizeof(buf), "%a", &tm) liefert auf
 * diesem Build (picolibc, nur "C"-Locale verfuegbar) verlaesslich immer die
 * englische Kurzform ("Sun".."Sat", siehe die "Wochentags-Kurzform"-
 * Eintraege in i18n.c) - direkt an _() uebergeben, kein eigener Helper
 * noetig (siehe clock_task.c/lv_daily_chart.c fuer die Verwendung). */
