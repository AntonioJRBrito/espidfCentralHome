#include "storage_manager.hpp"

static const char* TAG = "StorageManager";
namespace StorageManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Storage...");
        EventBus::post(EventDomain::STORAGE, EventId::STO_READY);
        ESP_LOGI(TAG, "→ STO_READY publicado");
        return ESP_OK;
    }
}