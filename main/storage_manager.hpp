#pragma once
#include "esp_err.h"
#include "esp_log.h"
#include "event_bus.hpp"
#include "storage_bus.hpp"

namespace StorageManager {
    esp_err_t init();
}