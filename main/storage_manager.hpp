#pragma once
#include "esp_err.h"
#include "esp_log.h"
#include "event_bus.hpp"
#include <unordered_map>
#include <string>

struct Page {
    void* data;
    size_t size;
    std::string mime;
};
namespace StorageManager {
    esp_err_t init();
    void registerPage(const char* uri, const Page& page);
    const Page* getPage(const char* uri);
    void onNetworkEvent(void*, esp_event_base_t, int32_t, void*);
    void onStorageEvent(void*, esp_event_base_t, int32_t, void*);
}