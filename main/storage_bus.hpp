#pragma once
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
struct config{
    char APssid[17];
};
namespace StorageBus {
    extern config* configCentral;
    esp_err_t init();
}