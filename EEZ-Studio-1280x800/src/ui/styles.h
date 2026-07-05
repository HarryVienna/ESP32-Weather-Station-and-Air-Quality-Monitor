#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: Setup Button
lv_style_t *get_style_setup_button_MAIN_DEFAULT();
void add_style_setup_button(lv_obj_t *obj);
void remove_style_setup_button(lv_obj_t *obj);

// Style: Setup Button Label
lv_style_t *get_style_setup_button_label_MAIN_DEFAULT();
void add_style_setup_button_label(lv_obj_t *obj);
void remove_style_setup_button_label(lv_obj_t *obj);

// Style: Setup Dropdown
lv_style_t *get_style_setup_dropdown_MAIN_DEFAULT();
lv_style_t *get_style_setup_dropdown_INDICATOR_DEFAULT();
void add_style_setup_dropdown(lv_obj_t *obj);
void remove_style_setup_dropdown(lv_obj_t *obj);

// Style: Setup Dropdown List
lv_style_t *get_style_setup_dropdown_list_MAIN_DEFAULT();
void add_style_setup_dropdown_list(lv_obj_t *obj);
void remove_style_setup_dropdown_list(lv_obj_t *obj);

// Style: Setup Textarea
lv_style_t *get_style_setup_textarea_MAIN_DEFAULT();
void add_style_setup_textarea(lv_obj_t *obj);
void remove_style_setup_textarea(lv_obj_t *obj);

// Style: Setup Label
lv_style_t *get_style_setup_label_MAIN_DEFAULT();
void add_style_setup_label(lv_obj_t *obj);
void remove_style_setup_label(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/