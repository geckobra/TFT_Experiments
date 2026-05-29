#include "time_manager.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define AppTag "TimeManager"

ds1302_t rtc_device = {};
struct tm current_time;
char time_text[16] = "00:00";

static void get_compiler_time(struct tm *t) {
    char s_mon[4];
    int day, year, hour, minute, second;
    const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

    sscanf(__DATE__, "%3s %d %d", s_mon, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

    char *p = strstr(months, s_mon);
    int month = p ? (p - months) / 3 : 0;

    minute += 1;
    second += 30;

    if (second >= 60) {
        second -= 60;
        minute++;
    }
    if (minute >= 60) {
        minute -= 60;
        hour++;
    }
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

static esp_err_t set_hardware_rtc_time(struct tm *new_time_config) {
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

static void read_rtc_task(void *pvParameters) {
    bool is_rtc_running = false;

    for (;;) {
        esp_err_t err = ds1302_is_running(&rtc_device, &is_rtc_running);

        if (err == ESP_OK && is_rtc_running) {
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

void time_manager_init(void) {
    rtc_device.ce_pin = GPIO_NUM_5;
    rtc_device.ch = false;
    rtc_device.io_pin = GPIO_NUM_16;
    rtc_device.sclk_pin = GPIO_NUM_17;

    ESP_ERROR_CHECK(ds1302_init(&rtc_device));

    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_INPUT_OUTPUT);
    gpio_pullup_en(GPIO_NUM_16);
    gpio_pullup_en(GPIO_NUM_17);

    bool rtc_running = false;
    ds1302_is_running(&rtc_device, &rtc_running);

    if (!rtc_running) {
        ESP_LOGW(AppTag, "RTC halted or clean boot detected. Starting oscillator...");
        ESP_ERROR_CHECK(ds1302_start(&rtc_device, true));

        struct tm config_time;
        memset(&config_time, 0, sizeof(struct tm));
        get_compiler_time(&config_time);

        set_hardware_rtc_time(&config_time);
    }

    xTaskCreate(read_rtc_task, "RTC_Task", 3072, NULL, 1, NULL);
}