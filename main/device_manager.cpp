#include "device_manager.hpp"

static const char* TAG = "DeviceManager";
namespace DeviceManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Device...");
        EventBus::post(EventDomain::READY, EventId::DEV_READY);
        ESP_LOGI(TAG, "→ DEV_READY publicado");
        return ESP_OK;
    }
}