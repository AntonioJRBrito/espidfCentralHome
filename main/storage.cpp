#include "storage.hpp"

// static const char* TAG = "STORAGE";

namespace Storage{
    config* configCentral = nullptr;
    esp_err_t init(){
        return ESP_OK;
    }
}
