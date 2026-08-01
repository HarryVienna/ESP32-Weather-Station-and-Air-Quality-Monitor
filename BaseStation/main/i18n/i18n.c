#include "i18n.h"

#include "nvs_flash.h"
#include "nvs/preferences.h"

/* The first language in the list is the fallback if a tag is missing in
 * the currently selected language (see lv_translation_get() in
 * lv_translation.c).
 *
 * Layout of s_translations: one block per tag with one entry per
 * language, in the same order as s_languages (see lv_translation_get():
 * "tr_row = translation_p + language_cnt * tag_index", then
 * "tr_row[lang_index]" - NOT grouped by language, as the docstring of
 * lv_translation_add_static() in the LVGL header suggests; the example
 * there doesn't match the actual indexing in the .c file). */
static const char * const s_languages[] = {"de", "en", NULL};

static const char * const s_tags[] = {
    /* --- UI labels (setup/Weatherstation screen) ---
     * Tags are English here, not German like elsewhere in this table:
     * EEZ Studio is set to English source text for "Translated Literal",
     * so the generated _() calls in screens.c now use English as the
     * key. */
    "Altitude:",
    "API:",
    "AppId:",
    "Base:",
    "Connect",
    "Downloading:",
    "Install",
    "Language:",
    "Latitude:",
    "Longitude:",
    "Network:",
    "Password:",
    "Restart",
    "Restart required to change language",
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

    /* --- Name+icon catalog (gui_icon_catalog.c: sensor_icon_options[].label) ---
     * The German plain text is deliberately kept as the tag/key there
     * (NVS only stores the index, no strings); translation only happens
     * at the three places where the label actually ends up on screen:
     * populate_sensor_icon_dropdown(), apply_sen66_config() and
     * apply_sensor_slot_configs(). */
    "Bad",
    "Balkon",
    "Büro",
    "Keller",
    "Küche",
    "Schlafzimmer",
    "Strahlung",
    "Werkstatt",
    "Wohnzimmer",

    /* --- Weekday short form (lv_daily_chart.c: weekly chart, 7 narrow
     * columns - deliberately kept short for that reason, see "weekday
     * spelled out" further below for clock_task.c) ---
     * Tags are deliberately English here instead of German like
     * elsewhere in this table: they come from strftime() "%a", not from
     * source text. */
    "Sun",
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
    "Sat",

    /* --- Weekday spelled out (clock_task.c: date display, enough room
     * for the full name) --- Tags come from strftime() "%A". */
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",

    /* --- Date format (clock_task.c) ---
     * Not just the weekday name is locale-dependent, so is the order of
     * day/month/year - in English usually month first. The strftime()
     * format string itself is the tag here, just as the (German) display
     * text is normally the tag elsewhere. */
    "%d.%m.%Y  %H:%M:%S",
    NULL,
};

/* Order must match s_tags exactly: one {de, en} pair per tag. */
static const char * const s_translations[] = {
    /* --- UI labels --- */
    "Höhe:",             "Altitude:",
    "API:",              "API:",
    "AppId:",            "AppId:",
    "Basis:",            "Base:",
    "Verbinden",         "Connect",
    "Download:",         "Downloading:",
    "Installieren",      "Install",
    "Sprache:",          "Language:",
    "Latitude:",         "Latitude:",
    "Longitude:",        "Longitude:",
    "Netzwerk:",         "Network:",
    "Passwort:",         "Password:",
    "Neustart",          "Restart",
    "Neustart erforderlich, um die Sprache zu ändern", "Restart required to change language",
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

    /* --- Name+icon catalog --- */
    "Bad",               "Bathroom",
    "Balkon",            "Balcony",
    "Büro",              "Office",
    "Keller",            "Cellar",
    "Küche",             "Kitchen",
    "Schlafzimmer",      "Bedroom",
    "Strahlung",         "Radiation",
    "Werkstatt",         "Workshop",
    "Wohnzimmer",        "Living room",

    /* --- Weekday short form (order must match s_tags above: Sun,
     * Mon, Tue, Wed, Thu, Fri, Sat) --- */
    "So",                "Su",
    "Mo",                "Mo",
    "Di",                "Tu",
    "Mi",                "We",
    "Do",                "Th",
    "Fr",                "Fr",
    "Sa",                "Sa",

    /* --- Weekday spelled out (order must match s_tags above:
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
    nvs_handle_t nvs_handle;
    nvs_open("weatherstation", NVS_READONLY, &nvs_handle);
    uint8_t language = get_uint8_from_nvs(nvs_handle, "language", 0);
    nvs_close(nvs_handle);

    lv_translation_init();
    lv_translation_add_static(s_languages, s_tags, s_translations);
    lv_translation_set_language(language == 1 ? "en" : "de");
}
