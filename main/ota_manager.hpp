#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "esp_err.h"
#include "storage_bus.hpp"

namespace OtaManager
{
    esp_err_t init();
}