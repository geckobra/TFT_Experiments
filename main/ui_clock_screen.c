#include "ui_screens.h"
#include "time_manager.h"
#include "lvgl.h"
#include <string.h>

static lv_obj_t* hour_label = NULL;
static lv_obj_t* background_image = NULL;

void ui_clock_screen_init(void) {
    LV_FONT_DECLARE(poketch_numeral_64);
    LV_IMAGE_DECLARE(poketch_background);

    // Render background image layer
    background_image = lv_image_create(lv_screen_active());
    lv_image_set_src(background_image, &poketch_background);
    lv_obj_align(background_image, LV_ALIGN_CENTER, 0, 0);

    // Create time display label layer
    hour_label = lv_label_create(lv_screen_active());
    lv_label_set_text(hour_label, "00:00");
    lv_obj_set_style_text_color(hour_label, lv_color_hex(0x385030), LV_PART_MAIN);
    lv_obj_set_style_text_align(hour_label, LV_TEXT_ALIGN_CENTER, 0);

    //force transparency styles to cleanly bypass container box clipping artifacts
    lv_obj_set_style_bg_opa(hour_label, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hour_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_clip_corner(hour_label, false, LV_PART_MAIN);
    lv_obj_set_style_border_width(hour_label, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(hour_label, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hour_label, 0, LV_PART_MAIN);

    lv_obj_align(hour_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(hour_label, &poketch_numeral_64, LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), LV_PART_MAIN);
}

void ui_clock_screen_update(void) {
    static char last_printed_time[16] = "";
    
    // Only alter display components when a new minute layout rolls over
    if (strcmp(last_printed_time, time_text) != 0) {
        if (hour_label != NULL) {
            lv_label_set_text(hour_label, time_text);
        }
        strcpy(last_printed_time, time_text);
    }
}