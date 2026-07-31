#include "i18n.h"

/* Erste Sprache in der Liste ist der Fallback, falls ein Tag in der aktuell
 * gewaehlten Sprache fehlt (siehe lv_translation_get() in lv_translation.c).
 *
 * Layout von s_translations: pro Tag ein Block mit je einem Eintrag pro
 * Sprache, in derselben Reihenfolge wie s_languages (siehe
 * lv_translation_get(): "tr_row = translation_p + language_cnt * tag_index",
 * dann "tr_row[lang_index]" - NICHT sprachweise gruppiert, wie es der
 * Docstring von lv_translation_add_static() im LVGL-Header nahelegt, das
 * Beispiel dort passt nicht zur tatsaechlichen Indizierung im .c). */
static const char * const s_languages[] = {"de", "en", NULL};

static const char * const s_tags[] = {
    /* --- UI-Labels (Setup-/Weatherstation-Screen) ---
     * Tags sind hier Englisch, nicht Deutsch wie sonst in dieser Tabelle:
     * EEZ Studio ist auf englische Quelltexte fuer "Translated Literal"
     * umgestellt, die generierten _()-Aufrufe in screens.c nutzen also
     * jetzt Englisch als Schluessel. */
    "Altitude:",
    "API:",
    "AppId:",
    "Base:",
    "Connect",
    "Downloading:",
    "Install",
    "Latitude:",
    "Longitude:",
    "Network:",
    "Password:",
    "Scan",
    "Sensor 0:",
    "Sensor 1:",
    "Sensor 2:",
    "Sensor 3:",
    "Sensor 4:",
    "Sensor 5:",
    "Start",
    "Time zone:",
    "Update available:",

    /* --- Name+Icon-Katalog (gui_icon_catalog.c: sensor_icon_options[].label) ---
     * Dort steht bewusst weiter der deutsche Klartext als Tag/Schluessel
     * (NVS speichert nur den Index, keine Strings), uebersetzt wird erst an
     * den drei Stellen, wo das Label tatsaechlich auf den Screen kommt:
     * populate_sensor_icon_dropdown(), apply_sen66_config() und
     * apply_sensor_slot_configs().*/
    "Bad",
    "Balkon",
    "Büro",
    "Keller",
    "Küche",
    "Schlafzimmer",
    "Strahlung",
    "Werkstatt",
    "Wohnzimmer",

    /* --- Wochentags-Kurzform (lv_daily_chart.c: Wochenchart, 7 schmale
     * Spalten - deshalb bewusst bei der Kurzform belassen, siehe
     * "Wochentag ausgeschrieben" weiter unten fuer clock_task.c) ---
     * Tags sind hier bewusst Englisch statt Deutsch wie sonst ueberall in
     * dieser Tabelle: sie kommen von strftime() "%a", nicht aus Quelltext. */
    "Sun",
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
    "Sat",

    /* --- Wochentag ausgeschrieben (clock_task.c: Datumsanzeige, genug Platz
     * fuer den vollen Namen) --- Tags kommen von strftime() "%A". */
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",

    /* --- Datumsformat (clock_task.c) ---
     * Nicht nur der Wochentagsname ist locale-abhaengig, auch die
     * Reihenfolge von Tag/Monat/Jahr - im Englischen ueblicherweise
     * Monat zuerst. Der strftime()-Formatstring selbst ist hier der Tag,
     * genau wie sonst auch der (deutsche) Anzeigetext der Tag ist. */
    "%d.%m.%Y  %H:%M:%S",
    NULL,
};

/* Reihenfolge muss exakt zu s_tags passen: je Tag ein {de, en}-Paar. */
static const char * const s_translations[] = {
    /* --- UI-Labels --- */
    "Höhe:",             "Altitude:",
    "API:",              "API:",
    "AppId:",            "AppId:",
    "Basis:",            "Base:",
    "Verbinden",         "Connect",
    "Downloading:",      "Downloading:",
    "Installieren",      "Install",
    "Latitude:",         "Latitude:",
    "Longitude:",        "Longitude:",
    "Netzwerk:",         "Network:",
    "Passwort:",         "Password:",
    "Scan",              "Scan",
    "Sensor 0:",         "Sensor 0:",
    "Sensor 1:",         "Sensor 1:",
    "Sensor 2:",         "Sensor 2:",
    "Sensor 3:",         "Sensor 3:",
    "Sensor 4:",         "Sensor 4:",
    "Sensor 5:",         "Sensor 5:",
    "Starten",           "Start",
    "Zeitzone:",         "Time zone:",
    "Update verfügbar:", "Update available:",

    /* --- Name+Icon-Katalog --- */
    "Bad",               "Bathroom",
    "Balkon",            "Balcony",
    "Büro",              "Office",
    "Keller",            "Cellar",
    "Küche",             "Kitchen",
    "Schlafzimmer",      "Bedroom",
    "Strahlung",         "Radiation",
    "Werkstatt",         "Workshop",
    "Wohnzimmer",        "Living room",

    /* --- Wochentags-Kurzform (Reihenfolge muss zu s_tags oben passen: Sun,
     * Mon, Tue, Wed, Thu, Fri, Sat) --- */
    "So",                "Su",
    "Mo",                "Mo",
    "Di",                "Tu",
    "Mi",                "We",
    "Do",                "Th",
    "Fr",                "Fr",
    "Sa",                "Sa",

    /* --- Wochentag ausgeschrieben (Reihenfolge muss zu s_tags oben passen:
     * Sunday, Monday, Tuesday, Wednesday, Thursday, Friday, Saturday) --- */
    "Sonntag",           "Sunday",
    "Montag",            "Monday",
    "Dienstag",          "Tuesday",
    "Mittwoch",          "Wednesday",
    "Donnerstag",        "Thursday",
    "Freitag",           "Friday",
    "Samstag",           "Saturday",

    /* --- Datumsformat --- */
    "%d.%m.%Y  %H:%M:%S", "%m/%d/%Y  %H:%M:%S",
};

void i18n_init(void)
{
    lv_translation_init();
    lv_translation_add_static(s_languages, s_tags, s_translations);
    lv_translation_set_language("en");
}
