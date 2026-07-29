#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: Button
lv_style_t *get_style_button_MAIN_DEFAULT();
void add_style_button(lv_obj_t *obj);
void remove_style_button(lv_obj_t *obj);

// Style: Button Label
lv_style_t *get_style_button_label_MAIN_DEFAULT();
void add_style_button_label(lv_obj_t *obj);
void remove_style_button_label(lv_obj_t *obj);

// Style: Dropdown
lv_style_t *get_style_dropdown_MAIN_DEFAULT();
lv_style_t *get_style_dropdown_INDICATOR_DEFAULT();
void add_style_dropdown(lv_obj_t *obj);
void remove_style_dropdown(lv_obj_t *obj);

// Style: Dropdown List
lv_style_t *get_style_dropdown_list_MAIN_DEFAULT();
void add_style_dropdown_list(lv_obj_t *obj);
void remove_style_dropdown_list(lv_obj_t *obj);

// Style: Textarea
lv_style_t *get_style_textarea_MAIN_DEFAULT();
void add_style_textarea(lv_obj_t *obj);
void remove_style_textarea(lv_obj_t *obj);

// Style: Label
lv_style_t *get_style_label_MAIN_DEFAULT();
void add_style_label(lv_obj_t *obj);
void remove_style_label(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/