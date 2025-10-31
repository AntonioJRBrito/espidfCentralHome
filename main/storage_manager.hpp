#pragma once
#include "esp_err.h"
#include "esp_log.h"
#include "event_bus.hpp"
#include <unordered_map>
#include <string>
#include <vector>
#include <new>

struct Page {
    void* data;
    size_t size;
    std::string mime;
};
struct Device {
    std::string id;
    std::string name;
    uint8_t type;
    uint16_t time;
    uint8_t status;
    std::string x_str;
    uint8_t x_int;
};
namespace StorageManager {
    esp_err_t init();
    // page
    void registerPage(const char* uri, const Page* page);
    const Page* getPage(const char* uri);
    // device
    void registerDevice(const std::string& id, Device* device);
    const Device* getDevice(const std::string& id);
    size_t getDeviceCount();
    std::vector<std::string> getDeviceIds();
    // handlers
    void onNetworkEvent(void*, esp_event_base_t, int32_t, void*);
    void onStorageEvent(void*, esp_event_base_t, int32_t, void*);
}