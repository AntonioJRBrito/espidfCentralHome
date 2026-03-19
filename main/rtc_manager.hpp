#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "esp_err.h"
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "esp_log.h" 
#include <string>
#include "storage_manager.hpp"

namespace RtcManager
{
    esp_err_t init();
    void set_timezone(const char* timezone_str);
    std::string get_current_time_str();
    CurrentTime get_current_time_struct();
    void initialize_sntp();
    void set_manual_time(int year, int month, int day, int hour, int minute, int second);
    void set_timezone(const char* timezone_str);
}