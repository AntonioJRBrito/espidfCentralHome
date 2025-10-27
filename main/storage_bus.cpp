#include "storage_bus.hpp"

static const char* TAG = "STORAGE_BUS";
config* StorageBus::configCentral = nullptr;

esp_err_t StorageBus::init(){
    
    return ESP_OK;
}