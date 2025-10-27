#include "socket_manager.hpp"

static const char* TAG = "SocketManager";
namespace SocketManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Socket...");
        EventBus::post(EventDomain::SOCKET, EventId::SOC_READY);
        ESP_LOGI(TAG, "→ SOC_READY publicado");
        return ESP_OK;
    }
}