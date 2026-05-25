#include <stdio.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_random.h"
#include <string.h>
#include <ds1302.h>

#define AppTag "Display"

//TFT screen dimensions
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define LOGO_WIDTH 65
#define LOGO_HEIGHT 43

//handles that are used throughout the program
adc_oneshot_unit_handle_t adc_handle = NULL;
esp_lcd_panel_handle_t panel_handle = NULL;
ds1302_t rtc_device = {};

void flush_callback(lv_display_t*, const lv_area_t*, uint8_t*);
uint32_t my_millis(void);

struct tm current_time;
char time_text[16]; //time string to be displayed in the TFT screen

void get_compiler_time(struct tm *t) {
    //this function is only used once when first loading the program with an unsynced RTC
    //it gets the time of compilation and sets that as the time in the RTC

    char s_mon[4];
    int day, year, hour, minute, second;
    const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

    sscanf(__DATE__, "%3s %d %d", s_mon, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

    char *p = strstr(months, s_mon);
    int month = p ? (p - months) / 3 : 0;

    minute += 1;
    second += 30;

    //compile time for this program in my computer is about 1 minute and 30 seconds, add that offset to get 
    //a time as close to real as possible
    if (second >= 60) {
        second -= 60;
        minute++;
    }
    if (minute >= 60) {
        minute -= 60;
        hour++;
    }

    //midnight rollover in case the code is compiled at midnight
    if (hour >= 24) {
        hour = 0;
        day++; 
    }
    
    t->tm_year = year - 1900;
    t->tm_mon  = month; 
    t->tm_mday = day;
    t->tm_hour = hour;
    t->tm_min  = minute;
    t->tm_sec  = second;
    t->tm_isdst = -1;
}

void init_adc(void){
    //initialize the ADC to read external keypad
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &chan_cfg));
}

void driver_init(void){
    //this function initialized SPI drivers, LCD I/O and the ILI9341 panel driver
    spi_bus_config_t spi_cfg = {
        .miso_io_num = GPIO_NUM_13,
        .mosi_io_num = GPIO_NUM_11,
        .sclk_io_num = GPIO_NUM_12,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (SCREEN_WIDTH*SCREEN_HEIGHT) * sizeof(lv_color_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t panel_spi_cfg = {};
    panel_spi_cfg.dc_gpio_num = GPIO_NUM_2;
    panel_spi_cfg.cs_gpio_num = GPIO_NUM_10;
    panel_spi_cfg.pclk_hz = 12*1000*1000;
    panel_spi_cfg.lcd_cmd_bits = 8;       
    panel_spi_cfg.lcd_param_bits = 8;    
    panel_spi_cfg.trans_queue_depth = 10;
    panel_spi_cfg.spi_mode = 0;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &panel_spi_cfg, &io_handle));

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = GPIO_NUM_4;
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_cfg.bits_per_pixel = 16;
    panel_cfg.data_endian = LCD_RGB_DATA_ENDIAN_BIG;

    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_cfg, &panel_handle));
}

esp_err_t set_hardware_rtc_time(struct tm *new_time_config) {
    //sets the time passed as argument as the time of the RTC IC
    esp_err_t err;

    err = ds1302_set_write_protect(&rtc_device, false);
    if (err != ESP_OK) {
        ESP_LOGE(AppTag, "Failed to unlock RTC write protection!");
        return err;
    }

    err = ds1302_set_time(&rtc_device, new_time_config);
    if (err != ESP_OK) {
        ESP_LOGE(AppTag, "Failed to write new data to RTC registers!");
        ds1302_set_write_protect(&rtc_device, true); 
        return err;
    }

    err = ds1302_set_write_protect(&rtc_device, true);
    if (err != ESP_OK) {
        ESP_LOGE(AppTag, "Failed to re-lock RTC write protection!");
        return err;
    }

    ESP_LOGI(AppTag, "Hardware RTC successfully updated and locked!");
    return ESP_OK;
}

void read_rtc(void *pvParameters){
    //task that reads the RTC each second and updates the time string
    bool is_rtc_running = false;

    for (;;){
        esp_err_t err = ds1302_is_running(&rtc_device, &is_rtc_running);

        if (err == ESP_OK && is_rtc_running){
            if (ds1302_get_time(&rtc_device, &current_time) == ESP_OK) {
                snprintf(time_text, sizeof(time_text), "%02d:%02d", current_time.tm_hour, current_time.tm_min);

                ESP_LOGI(AppTag, "Hardware Time: %04d-%02d-%02dT%02d:%02d:%02d", 
                         current_time.tm_year + 1900, 
                         current_time.tm_mon + 1, 
                         current_time.tm_mday, 
                         current_time.tm_hour, 
                         current_time.tm_min, 
                         current_time.tm_sec);
            }
        } else {
            ESP_LOGW(AppTag, "Waiting for DS1302 crystal stabilization data...");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void){
    //initialize ADC and drivers
    init_adc();
    driver_init();

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false)); 
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));

    ESP_LOGI(AppTag, "Configured display panel successfully!");

    lv_init();
    lv_tick_set_cb(my_millis);

    //create screen and set color format
    lv_display_t* display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565_SWAPPED);

    //create a buffer as big as the screen to load full images that fill the background
    const int buf_size = (SCREEN_WIDTH*SCREEN_HEIGHT) * sizeof(lv_color_t);
    uint16_t *buf = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    if (buf == NULL){
        ESP_LOGE(AppTag, "Could not allocate display buffer!");
        return;
    }
    
    lv_display_set_buffers(display, buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_callback);
    
    //set base container to clear visibility leaks
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), LV_PART_MAIN);

    //declare the font and the background image
    LV_FONT_DECLARE(poketch_numeral_64);
    LV_IMAGE_DECLARE(poketch_background);

    //render background image
    lv_obj_t* background_image = lv_image_create(lv_screen_active());
    lv_image_set_src(background_image, &poketch_background);
    //lv_obj_set_size(background_image, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(background_image, LV_ALIGN_CENTER, 0, 0);

    //create time display label
    lv_obj_t* hour_label = lv_label_create(lv_screen_active());
    lv_label_set_text(hour_label, "00:00");
    lv_obj_set_style_text_color(hour_label, lv_color_hex(0x385030), LV_PART_MAIN);
    lv_obj_set_style_text_align(hour_label, LV_TEXT_ALIGN_CENTER, 0);

    //remove style from the label so there is no tearing and clipping around the time
    lv_obj_set_style_bg_opa(hour_label, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hour_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_clip_corner(hour_label, false, LV_PART_MAIN);
    lv_obj_set_style_border_width(hour_label, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(hour_label, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hour_label, 0, LV_PART_MAIN);
    
    lv_obj_align(hour_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(hour_label, &poketch_numeral_64, LV_STATE_DEFAULT);

    //set RTC configuration by setting the pins
    rtc_device.ce_pin = GPIO_NUM_5;
    rtc_device.ch = false;
    rtc_device.io_pin = GPIO_NUM_6;
    rtc_device.sclk_pin = GPIO_NUM_7;

    ESP_ERROR_CHECK(ds1302_init(&rtc_device));

    //set the pins connected to the RTC as PULL-UP
    gpio_set_direction(GPIO_NUM_6, GPIO_MODE_INPUT_OUTPUT);
    gpio_pullup_en(GPIO_NUM_6);
    gpio_pullup_en(GPIO_NUM_7);

    //if the rtc is not running, force the compile time, otherwhise, don't do nothing to the rtc
    bool rtc_running = false;
    ds1302_is_running(&rtc_device, &rtc_running);

    if (!rtc_running){
        ESP_LOGW(AppTag, "RTC halted or clean boot detected. Starting oscillator...");
        ESP_ERROR_CHECK(ds1302_start(&rtc_device, true));
        
        struct tm config_time;
        memset(&config_time, 0, sizeof(struct tm));
        get_compiler_time(&config_time);
        
        set_hardware_rtc_time(&config_time);
    }

    //create task that reads the RTC
    xTaskCreate(read_rtc, "RTC_Task", 3072, NULL, 1, NULL);

    //this dummy label is used to only update the screen when time changes so as to avoid screen flickering
    char last_printed_time[16] = "";
    
    while(true){
        if (strcmp(last_printed_time, time_text) != 0) {
            lv_label_set_text(hour_label, time_text);
            strcpy(last_printed_time, time_text);
        }
        
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

uint32_t my_millis(void){
    return pdTICKS_TO_MS(xTaskGetTickCount());
}

void flush_callback(lv_display_t* display, const lv_area_t* area, uint8_t* pixel_buffer){
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2+1, area->y2+1, pixel_buffer);
    lv_display_flush_ready(display);
}