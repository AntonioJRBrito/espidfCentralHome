#include "rtc_manager.hpp"

static const char* TAG = "RtcManager";
namespace RtcManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Rtc...");
        EventBus::post(EventDomain::RTC, EventId::RTC_READY);
        ESP_LOGI(TAG, "→ RTC_READY publicado");
        return ESP_OK;
    }
}