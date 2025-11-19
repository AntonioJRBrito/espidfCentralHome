#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>
#include <vector>
#include "esp_err.h"
#include "esp_log.h"
#include "event_bus.hpp"
#include "storage_manager.hpp"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "bootloader_random.h"
#include "esp_random.h"

namespace WebManager {
    esp_err_t init();
}
