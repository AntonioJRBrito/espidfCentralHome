#include "ota_manager.hpp"

static const char* TAG = "OtaManager";
namespace OtaManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Ota...");
        EventBus::post(EventDomain::OTA, EventId::OTA_READY);
        ESP_LOGI(TAG, "→ OTA_READY publicado");
        return ESP_OK;
    }
}