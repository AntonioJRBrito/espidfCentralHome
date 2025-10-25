#include "storage_bus.hpp"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"

static const char* TAG = "STORAGE_BUS";
config* StorageBus::configCentral = nullptr;

esp_err_t StorageBus::init(){
    
    return ESP_OK;
}