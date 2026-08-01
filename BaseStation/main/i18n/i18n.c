#include "i18n.h"

#include "nvs_flash.h"
#include "nvs/preferences.h"
#include "ui/ui.h"

/* Set by i18n_init(), read by i18n_apply_keyboard_layout() - avoids a
 * second NVS round-trip for the same value. */
static uint8_t s_language;

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
    "Neustart erforderlich", "Restart required to change language",
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
    s_language = get_uint8_from_nvs(nvs_handle, "language", 0);
    nvs_close(nvs_handle);

    lv_translation_init();
    lv_translation_add_static(s_languages, s_tags, s_translations);
    lv_translation_set_language(s_language == 1 ? "en" : "de");
}

/* ============================================================================
 * German on-screen keyboard layout (QWERTZ + umlauts/eszett)
 *
 * LVGL's built-in default map (left in place when this isn't applied) is
 * QWERTY/English. Only the two text modes get a German map -
 * LV_KEYBOARD_MODE_SPECIAL (digits/symbols via "1#") stays LVGL's default,
 * it's already language-independent. lv_keyboard's default event handler
 * recognizes the "ABC"/"abc"/"1#" buttons by their fixed strings/control
 * flags and switches modes on its own - no custom callback needed.
 *
 * The keyboard's default font (lv_font_montserrat_24, see screens.c) only
 * has ASCII + LVGL's symbol icons compiled in (checked its cmaps[] - no
 * Latin-1 supplement), so umlauts/eszett wouldn't render with it. Reusing
 * ui_font_free_sans24 (already used for German UI labels elsewhere, and
 * confirmed via its cmaps[] to cover the full Latin-1 range 160-255) avoids
 * having to generate a new font just for this.
 * ============================================================================ */
/* Row 2's last key is "_" instead of LV_SYMBOL_NEW_LINE/Enter - on a
 * one-line textarea (password/SSID) Enter does the same thing as the
 * LV_SYMBOL_OK button in row 4 anyway (see lv_keyboard.c: inserts '\n',
 * then immediately fires LV_EVENT_READY because lv_textarea_get_one_line()
 * is true), so it's redundant there - closing still works via
 * LV_SYMBOL_KEYBOARD (cancel) or LV_SYMBOL_OK (ready) in row 4. Underscore
 * has no key of its own on this layout otherwise, but is common in WiFi
 * passwords/SSIDs. */
static const char * const kb_map_de_lower[] = {
    "1#", "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "ü", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "ö", "ä", "_", "\n",
    "-", "y", "x", "c", "v", "b", "n", "m", "ß", ".", ",", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const char * const kb_map_de_upper[] = {
    "1#", "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "Ü", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ö", "Ä", "_", "\n",
    "-", "Y", "X", "C", "V", "B", "N", "M", "ß", ".", ",", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

/* One entry per button (no "\n"), rows: 13 / 13 / 11 / 5 - must match
 * kb_map_de_lower/upper exactly. Row 2's last entry dropped
 * LV_KEYBOARD_CTRL_BUTTON_FLAGS (no longer a control key, just "_" - the
 * "checked" flag in there gave it the special-key highlight style). */
static const lv_buttonmatrix_ctrl_t kb_ctrl_de[] = {
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, 6,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};

/* ui_font_free_sans24 alone isn't enough: it has the umlauts/eszett but,
 * unlike lv_font_montserrat_24, none of the LV_SYMBOL_* icon glyphs used
 * for backspace/enter/keyboard-collapse/arrows/ok (those come from a
 * FontAwesome-derived icon set baked into LVGL's built-in fonts at fixed
 * Unicode Private-Use-Area code points, e.g. LV_SYMBOL_OK = U+F00C - EEZ
 * Studio's "Symbols" field can only pull extra glyphs from the one TTF
 * already embedded for this font, which doesn't contain those icons).
 * Solution: a non-const runtime copy of ui_font_free_sans24 with its
 * "fallback" pointer (see lv_font.h) set to lv_font_montserrat_24 - LVGL
 * then transparently uses the fallback for any glyph missing in the
 * primary font, i.e. exactly the icons. Copying is safe (lv_font_t is a
 * plain POD struct); mutating ui_font_free_sans24/lv_font_montserrat_24
 * themselves is not, since both are `extern const` and live in flash. */
static lv_font_t kb_font_de;

void i18n_apply_keyboard_layout(void)
{
    if (s_language == 1) {
        return; // English: LVGL's built-in QWERTY map/font is already correct
    }

    lv_keyboard_set_map(objects.keyboard_text, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_de_lower, kb_ctrl_de);
    lv_keyboard_set_map(objects.keyboard_text, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_de_upper, kb_ctrl_de);

    kb_font_de = ui_font_free_sans24;
    kb_font_de.fallback = &lv_font_montserrat_24;
    lv_obj_set_style_text_font(objects.keyboard_text, &kb_font_de, LV_PART_MAIN | LV_STATE_DEFAULT);
}
