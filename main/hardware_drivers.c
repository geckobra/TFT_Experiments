#include "hardware_drivers.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "esp_log.h"

#define AppTag "HardwareDrivers"
#define DEBOUNCE_SAMPLES 3

esp_lcd_panel_handle_t panel_handle = NULL;
adc_oneshot_unit_handle_t adc_handle = NULL;

// Global display handle - captured on first flush, used by DMA done callback
// to safely signal LVGL only after the SPI/DMA transfer is fully complete
lv_display_t* g_display = NULL;

static keypad_state_t state = {.last_state = BUTTON_NONE, .current_state = BUTTON_NONE};

static bool debounced_states[KEYPAD_BUTTONS] = {false};
static bool raw_states[KEYPAD_BUTTONS]         = {false};
static bool last_button_states[KEYPAD_BUTTONS] = {false};

static uint8_t debounce_counters[KEYPAD_BUTTONS] = {0};

uint32_t button_pressed_times[KEYPAD_BUTTONS] = {0};

void init_adc(void) {
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

//called by the SPI driver when the DMA transfer is fully complete.
//only at this point is it safe to tell LVGL the buffer is free to reuse.
//calling lv_display_flush_ready() before this causes tearing because LVGL
//may start writing new pixel data into the buffer while DMA is still reading it.
static bool on_trans_done(esp_lcd_panel_io_handle_t io,  esp_lcd_panel_io_event_data_t *edata, void *ctx) {
    if (g_display) lv_display_flush_ready(g_display);
    return false;
}

void driver_init(void) {
    spi_bus_config_t spi_cfg = {
        .miso_io_num = GPIO_NUM_19, //VSPIQ
        .mosi_io_num = GPIO_NUM_23, //VSPID
        .sclk_io_num = GPIO_NUM_18, //VSPICLK
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (SCREEN_WIDTH * SCREEN_HEIGHT / 10) * sizeof(lv_color_t),
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &spi_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t panel_spi_cfg = {
        .dc_gpio_num = GPIO_NUM_32,
        .cs_gpio_num = GPIO_NUM_22, //VSPICSO
        .pclk_hz = 10 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .trans_queue_depth = 30,
        .spi_mode = 0,
        //register the DMA-done callback so lv_display_flush_ready() is only
        //called after the transfer completes, preventing buffer reuse mid-DMA
        .on_color_trans_done = on_trans_done,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &panel_spi_cfg, &io_handle));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_4,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_cfg, &panel_handle));
}

void hardware_drivers_init(void) {
    init_adc();
    driver_init();

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));

    ESP_LOGI(AppTag, "Configured display panel and ADC successfully!");
}

void flush_callback(lv_display_t* display, const lv_area_t* area, uint8_t* pixel_buffer) {
    g_display = display;

    esp_lcd_panel_draw_bitmap(panel_handle,
        area->x1, area->y1,
        area->x2 + 1, area->y2 + 1,
        pixel_buffer);
}

void update_keypad_state(void* pvParameters) {
    //TODO: Fix Button Boucing Issue

    int raw_adc_value = 0;
    for (;;){
        for (int i = 0; i < KEYPAD_BUTTONS; i++) {
            raw_states[i] = false;
        }

        esp_err_t err = adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw_adc_value);
        //ESP_LOGI("ADC_DEBUG", "Raw value: %d", raw_adc_value); // <-- Add this temporarily
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        
        if (raw_adc_value > 3500) {
            raw_states[BUTTON_NONE] = true;
        } else if (raw_adc_value >= 490 && raw_adc_value < 580) {
            raw_states[BUTTON_UP] = true;
        } else if (raw_adc_value >= 1180 && raw_adc_value < 1290) {
            raw_states[BUTTON_DOWN] = true;
        } else if (raw_adc_value >= 0 && raw_adc_value < 100) {
            raw_states[BUTTON_LEFT] = true;
        } else if (raw_adc_value >= 1880 && raw_adc_value < 1990) {
            raw_states[BUTTON_RIGHT] = true;
        } else if (raw_adc_value >= 2700 && raw_adc_value < 2940) {
            raw_states[BUTTON_SIDE] = true;
        }
    
        for (int i = 0; i < KEYPAD_BUTTONS; i++) {
            if (raw_states[i] == debounced_states[i]) {
                //signal is stable - reset the counter, nothing to commit
                debounce_counters[i] = 0;
            } else {
                debounce_counters[i]++;
                if (debounce_counters[i] >= DEBOUNCE_SAMPLES) {
                    // Signal has been consistently different for long enough:
                    // snapshot last state, commit the new one, reset counter
                    last_button_states[i] = debounced_states[i];
                    debounced_states[i]   = raw_states[i];
                    debounce_counters[i]  = 0;
                }
            }
        }

        for (int i = 0; i < KEYPAD_BUTTONS; i++){
            if (debounced_states[i]){
                button_pressed_times[i] += 50; // ms per sample tick
            } else {
                button_pressed_times[i] = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

uint32_t pressed_button_time(keypad_buttons_t button){
    if (button >= KEYPAD_BUTTONS) return 0;
    return button_pressed_times[button] / 1000;
}

bool is_button_pressed(keypad_buttons_t button) {
    if (button >= KEYPAD_BUTTONS) return false;
    return (debounced_states[button] == true && last_button_states[button] == false);
}

bool is_button_released(keypad_buttons_t button) {
    if (button >= KEYPAD_BUTTONS) return false;
    return (last_button_states[button] == true && debounced_states[button] == false);
}