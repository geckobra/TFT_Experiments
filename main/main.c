#include "hardware_drivers.h"
#include "time_manager.h"
#include "ui_screens.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AppTag "Main"

static void display_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_INVALIDATE_AREA) {
        lv_area_t * area = lv_event_get_param(e);
        
        // Force horizontal coordinates to adhere to even/odd boundaries (even pixel width)
        area->x1 = area->x1 & (~0x1);
        area->x2 = area->x2 | 0x1;
    }
}

uint32_t my_millis(void) {
    return pdTICKS_TO_MS(xTaskGetTickCount());
}

void app_main(void) {
    hardware_drivers_init();
    lv_init();
    lv_tick_set_cb(my_millis);
    time_manager_init();

    lv_display_t* display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565_SWAPPED);

    lv_display_add_event_cb(display, display_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    const int buf_size = (SCREEN_WIDTH * SCREEN_HEIGHT / 10) * sizeof(lv_color_t);
    uint16_t *buf = heap_caps_aligned_alloc(4, buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf == NULL) {
        ESP_LOGE(AppTag, "Could not allocate display buffer memory context!");
        return;
    }
    lv_display_set_buffers(display, buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_callback);

    ui_clock_screen_init();

    bool displayed_dock = false;
    lv_obj_t* cont_row = lv_obj_create(lv_screen_active());
    lv_obj_set_size(cont_row, 300, 60);
    lv_obj_align(cont_row, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_flex_flow(cont_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(cont_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(cont_row, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t* button_1 = lv_button_create(cont_row);
    lv_obj_set_size(button_1, 30, 30);
    lv_obj_t* button_2 = lv_button_create(cont_row);
    lv_obj_set_size(button_2, 30, 30);
    lv_obj_add_flag(cont_row, LV_OBJ_FLAG_HIDDEN);
    
    uint32_t last_clock_update = 0;
    xTaskCreate(update_keypad_state, "Keypad", 2048, NULL, 1, NULL);
    while (true) {
        uint32_t current_time = my_millis();
        if (current_time - last_clock_update >= 500) {
            last_clock_update = current_time;
            ui_clock_screen_update();
        }

        if (is_button_pressed(BUTTON_SIDE)){
            ESP_LOGI(AppTag, "Side button detected");
            if (displayed_dock == true){
                ESP_LOGI(AppTag, "Short press release detected: Hiding Dock");
                lv_obj_add_flag(cont_row, LV_OBJ_FLAG_HIDDEN);
                displayed_dock = false;
            }
        }

        if (pressed_button_time(BUTTON_SIDE) >= 2 && !displayed_dock){
            ESP_LOGI(AppTag, "Long press detected: Revealing Dock");
            lv_obj_remove_flag(cont_row, LV_OBJ_FLAG_HIDDEN);
            displayed_dock = true;
        }

        if (is_button_released(BUTTON_SIDE)){
            ESP_LOGI(AppTag, "Side button released!!");
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}