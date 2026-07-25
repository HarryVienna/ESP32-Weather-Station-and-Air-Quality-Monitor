#include "gui_color_scale.h"

#include "config/config.h"

lv_color_t level_color_desc(float val, const color_thresh_t *t)
{
    if      (val >= t->t1) return lv_color_hex(COLOR_GREEN);
    else if (val >= t->t2) return lv_color_hex(COLOR_LIGHTGREEN);
    else if (val >= t->t3) return lv_color_hex(COLOR_YELLOW);
    else if (val >= t->t4) return lv_color_hex(COLOR_ORANGE);
    else                   return lv_color_hex(COLOR_RED);
}

lv_color_t level_color_asc(float val, const color_thresh_t *t)
{
    if      (val <= t->t1) return lv_color_hex(COLOR_GREEN);
    else if (val <= t->t2) return lv_color_hex(COLOR_LIGHTGREEN);
    else if (val <= t->t3) return lv_color_hex(COLOR_YELLOW);
    else if (val <= t->t4) return lv_color_hex(COLOR_ORANGE);
    else                   return lv_color_hex(COLOR_RED);
}
