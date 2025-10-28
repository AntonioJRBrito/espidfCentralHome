#pragma once
#include "storage_manager.hpp"
#include "global_config.hpp"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include <string>
#include "esp_heap_caps.h"
#include <stdio.h>

namespace Storage {
    esp_err_t init();
    static void loadFileToPsram(const char* path, const char* key, const char* mime);
}