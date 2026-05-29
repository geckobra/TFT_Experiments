#pragma once

#include <time.h>
#include <ds1302.h>

extern ds1302_t rtc_device;
extern struct tm current_time;
extern char time_text[16];

void time_manager_init(void);