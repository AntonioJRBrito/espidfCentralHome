#include "automation_manager.hpp"

static const char* TAG = "AutomationManager";
namespace AutomationManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Automation...");
        EventBus::post(EventDomain::READY, EventId::AUT_READY);
        ESP_LOGI(TAG, "→ AUT_READY publicado");
        return ESP_OK;
    }
}