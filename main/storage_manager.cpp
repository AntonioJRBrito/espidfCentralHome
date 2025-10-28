#include "storage_manager.hpp"
#include "storage.hpp"

static const char* TAG = "StorageManager";

namespace StorageManager {
    static std::unordered_map<std::string, Page> pageMap;
    void registerPage(const char* uri, const Page& p) {
        pageMap[uri] = p;
        ESP_LOGI(TAG, "Página registrada: %s (%zu bytes, %s)", uri, p.size, p.mime.c_str());
    }
    const Page* getPage(const char* uri) {
        auto it = pageMap.find(uri);
        return (it != pageMap.end()) ? &it->second : nullptr;
    }
    void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_IFOK) {
            ESP_LOGI(TAG, "Network IFOK recebido → checar SSID/password armazenados...");
            // Aqui entrará a recuperação de SSID/senha se existirem
            // Storage::getWifiCredentials(...)
        }
    }
    void onStorageEvent(void*, esp_event_base_t, int32_t id, void*) {
        EventId evt = static_cast<EventId>(id);
        switch (evt) {
            case EventId::STO_QUERY:
                ESP_LOGI(TAG, "Recebido STO_QUERY");
                break;
            case EventId::STO_UPDATE_REQ:
                ESP_LOGI(TAG, "Recebido STO_UPDATE_REQ");
                break;
            default:
                break;
        }
    }
    esp_err_t init() {
        ESP_LOGI(TAG, "Inicializando Storage Manager...");
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        EventBus::regHandler(EventDomain::STORAGE, &onStorageEvent, nullptr);
        ESP_LOGI(TAG, "Montando Storage físico (LittleFS)...");
        esp_err_t ret = Storage::init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao inicializar Storage físico");
            return ret;
        }
        EventBus::post(EventDomain::STORAGE, EventId::STO_READY);
        ESP_LOGI(TAG, "→ STO_READY publicado");
        return ESP_OK;
    }
}

/// acessar cia: const Page* p = StorageManager::getPage("/index.html");