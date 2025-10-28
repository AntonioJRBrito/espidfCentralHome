#pragma once
#include "esp_err.h"
#include <string>
#include <unordered_map>
#include "esp_log.h"
#include "event_bus.hpp"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_heap_caps.h"
#include "event_bus.hpp"
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
namespace StorageManager {
    struct Page {
        uint8_t* data;
        size_t size;
        std::string mime;
    };
    esp_err_t init();
    const Page* get(const char* uri);
}