#pragma once
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
namespace StorageBus {
    esp_err_t init();
    esp_err_t post();
    esp_err_t regHandler();
}