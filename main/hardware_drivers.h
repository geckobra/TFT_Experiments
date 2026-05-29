#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_adc/adc_oneshot.h"
#include "lvgl.h"

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

//HSPIQ -> MISO
//HSPID -> MOSI

//shared system handles
extern esp_lcd_panel_handle_t panel_handle;
extern adc_oneshot_unit_handle_t adc_handle;

#define KEYPAD_BUTTONS 5

typedef enum{
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,
    BUTTON_RIGHT,
    BUTTON_SIDE,
    BUTTON_NONE
} keypad_buttons_t;

//keep track of the pressed buttons and the time each has been pressed for
typedef struct{
    keypad_buttons_t last_state;
    keypad_buttons_t current_state;
} keypad_state_t;

void init_adc(void);
void driver_init(void);
void hardware_drivers_init(void);
void flush_callback(lv_display_t* display, const lv_area_t* area, uint8_t* pixel_buffer);

//keypad functions
void update_keypad_state(void*);
bool is_button_pressed(keypad_buttons_t);
bool is_button_released(keypad_buttons_t);
uint32_t pressed_button_time(keypad_buttons_t);