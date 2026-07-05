#include "styles.h"
#include "images.h"
#include "fonts.h"

#include "ui.h"
#include "screens.h"

//
// Style: Setup Button
//

void init_style_setup_button_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xf56101));
    lv_style_set_pad_top(style, 0);
    lv_style_set_pad_bottom(style, 0);
    lv_style_set_pad_left(style, 0);
    lv_style_set_pad_right(style, 0);
};

lv_style_t *get_style_setup_button_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_setup_button_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_setup_button(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_setup_button_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_setup_button(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_setup_button_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: Setup Button Label
//

void init_style_setup_button_label_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_font(style, &ui_font_free_sans30);
    lv_style_set_align(style, LV_ALIGN_CENTER);
};

lv_style_t *get_style_setup_button_label_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_setup_button_label_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_setup_button_label(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_setup_button_label_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_setup_button_label(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_setup_button_label_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: Setup Dropdown
//

void init_style_setup_dropdown_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0xe0e0e0));
    lv_style_set_border_color(style, lv_color_hex(0x000000));
    lv_style_set_border_width(style, 1);
    lv_style_set_text_font(style, &ui_font_free_sans30);
};

lv_style_t *get_style_setup_dropdown_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_setup_dropdown_MAIN_DEFAULT(style);
    }
    return style;
};

void init_style_setup_dropdown_INDICATOR_DEFAULT(lv_style_t *style) {
    lv_style_set_text_font(style, &lv_font_montserrat_26);
};

lv_style_t *get_style_setup_dropdown_INDICATOR_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_setup_dropdown_INDICATOR_DEFAULT(style);
    }
    return style;
};

void add_style_setup_dropdown(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_setup_dropdown_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(obj, get_style_setup_dropdown_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
};

void remove_style_setup_dropdown(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_setup_dropdown_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_style(obj, get_style_setup_dropdown_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
};

//
// Style: Setup Dropdown List
//

void init_style_setup_dropdown_list_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_font(style, &ui_font_free_sans30);
};

lv_style_t *get_style_setup_dropdown_list_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_setup_dropdown_list_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_setup_dropdown_list(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_setup_dropdown_list_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_setup_dropdown_list(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_setup_dropdown_list_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: Setup Textarea
//

void init_style_setup_textarea_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_pad_top(style, 8);
    lv_style_set_pad_bottom(style, 8);
    lv_style_set_pad_left(style, 8);
    lv_style_set_pad_right(style, 8);
    lv_style_set_bg_color(style, lv_color_hex(0xe0e0e0));
    lv_style_set_border_color(style, lv_color_hex(0x000000));
    lv_style_set_border_width(style, 1);
    lv_style_set_text_font(style, &ui_font_free_sans30);
};

lv_style_t *get_style_setup_textarea_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_setup_textarea_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_setup_textarea(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_setup_textarea_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_setup_textarea(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_setup_textarea_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: Setup Label
//

void init_style_setup_label_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_font(style, &ui_font_free_sans30);
    lv_style_set_length(style, 52);
};

lv_style_t *get_style_setup_label_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_setup_label_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_setup_label(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_setup_label_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_setup_label(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_setup_label_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
//
//

void add_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*AddStyleFunc)(lv_obj_t *obj);
    static const AddStyleFunc add_style_funcs[] = {
        add_style_setup_button,
        add_style_setup_button_label,
        add_style_setup_dropdown,
        add_style_setup_dropdown_list,
        add_style_setup_textarea,
        add_style_setup_label,
    };
    add_style_funcs[styleIndex](obj);
}

void remove_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*RemoveStyleFunc)(lv_obj_t *obj);
    static const RemoveStyleFunc remove_style_funcs[] = {
        remove_style_setup_button,
        remove_style_setup_button_label,
        remove_style_setup_dropdown,
        remove_style_setup_dropdown_list,
        remove_style_setup_textarea,
        remove_style_setup_label,
    };
    remove_style_funcs[styleIndex](obj);
}