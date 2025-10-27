#include "net_manager.hpp"

static const char* TAG = "NetManager";

namespace NetManager
{
    static esp_netif_t* netif_ap = nullptr;
    static void onEventBus(void*,esp_event_base_t base,int32_t id,void*)
    {
        EventId evt = static_cast<EventId>(id);
        switch (evt)
        {
            case EventId::READY_ALL:
                ESP_LOGI(TAG, "[EventBus] Recebido READY_ALL - NetManager permanece ativo");
                break;
            case EventId::STO_READY:
                ESP_LOGI(TAG, "[EventBus] Storage pronto - futura leitura de credenciais");
                break;
            default:
                ESP_LOGI(TAG, "[EventBus] Evento não tratado: base=%s id=%ld",base ? (const char*)base : "null", id);
                break;
        }
    }
    static void onWifiEvent(void*, esp_event_base_t base, int32_t id, void*)
    {
        if (base == WIFI_EVENT)
        {
            switch (id)
            {
                case WIFI_EVENT_AP_START:
                    ESP_LOGI(TAG, "AP iniciado");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_APCONNECTED);
                    break;
                case WIFI_EVENT_AP_STOP:
                    ESP_LOGW(TAG, "AP parado");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_APDISCONNECTED);
                    break;
                case WIFI_EVENT_STA_START:
                    ESP_LOGI(TAG, "STA iniciou");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_STASTARTED);
                    break;
                case WIFI_EVENT_STA_CONNECTED:
                    ESP_LOGI(TAG, "STA conectada");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_STACONNECTED);
                    break;
                case WIFI_EVENT_STA_DISCONNECTED:
                    ESP_LOGW(TAG, "STA desconectada");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_STADISCONNECTED);
                    break;
                default:
                    ESP_LOGI(TAG, "WIFI_EVENT não tratado (id=%ld)", id);
                    break;
            }
        }
        else if (base == IP_EVENT)
        {
            switch (id)
            {
                case IP_EVENT_AP_STAIPASSIGNED:
                    ESP_LOGI(TAG, "Cliente conectado ao AP");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_STAGOTIP);
                    break;
                default:
                    break;
            }
        }
    }

    static esp_err_t startAP()
    {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
            ret == ESP_ERR_NVS_NEW_VERSION_FOUND ||
            ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS inválido, apagando...");
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        netif_ap = esp_netif_create_default_wifi_ap();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr));
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char ssid[18];
        sprintf(ssid,"CTR_%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        wifi_config_t ap_cfg{};
        strncpy((char*)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid));
        ap_cfg.ap.ssid_len = strlen(ssid);
        ap_cfg.ap.channel = 1;
        ap_cfg.ap.max_connection = 4;
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        ESP_LOGI(TAG, "Iniciando AP SSID=%s", ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());
        return ESP_OK;
    }

    esp_err_t init()
    {
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr);
        esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr);
        EventBus::regHandler(EventDomain::NETWORK, &onEventBus, nullptr);
        if (startAP() != ESP_OK){
            ESP_LOGE(TAG, "Falha ao criar AP");
            return ESP_FAIL;
        }else{
            vTaskDelay(pdMS_TO_TICKS(300));
            EventBus::post(EventDomain::NETWORK, EventId::NET_READY);
            ESP_LOGI(TAG, "→ NET_READY publicado");
        }
        ESP_LOGI(TAG, "NetManager inicializado (AP ativo, NET_READY enviado)");
        return ESP_OK;
    }
}