#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "esp_err.h"
#include "cJSON.h"
#include <map>
#include "esp_timer.h"
#include "storage_manager.hpp"

namespace AutomationManager
{
    struct Schedule {
        std::map<std::string, uint8_t> devices_to_trigger;
    };
    struct CheckScheduleParams {
        CurrentTime ct;
    };
    static void executeAction(const char* device_id,uint8_t action,uint8_t inform);
    static void automation_task(void* pvParameters);
    static void scheduleTimerCallback(void* arg);
    static void checkSchedule_task(void* pvParameters);
    static void onEventAutomationBus(void*,esp_event_base_t base,int32_t id,void* data);
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void* event_data);
    esp_err_t init();
}