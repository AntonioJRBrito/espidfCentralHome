#include "broker_manager.hpp"

static const char* TAG = "BrokerManager";
namespace BrokerManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Broker...");
        EventBus::post(EventDomain::READY, EventId::BRK_READY);
        ESP_LOGI(TAG, "→ BRK_READY publicado");
        return ESP_OK;
    }
}