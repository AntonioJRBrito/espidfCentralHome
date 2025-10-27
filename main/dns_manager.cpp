#include "dns_manager.hpp"

static const char* TAG = "DnsManager";
namespace DnsManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando DNS...");
        EventBus::post(EventDomain::DNS, EventId::DNS_READY);
        ESP_LOGI(TAG, "→ DNS_READY publicado");
        return ESP_OK;
    }
}