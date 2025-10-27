#include "web_manager.hpp"

static const char* TAG = "WebManager";
namespace WebManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Web...");
        EventBus::post(EventDomain::WEB, EventId::WEB_READY);
        ESP_LOGI(TAG, "→ WEB_READY publicado");
        return ESP_OK;
    }
}