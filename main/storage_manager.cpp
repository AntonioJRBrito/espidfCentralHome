#include "storage_manager.hpp"
#include "storage.hpp"

static const char* TAG = "StorageManager";
namespace StorageManager {
    static std::unordered_map<std::string, Page*> pageMap;
    static std::unordered_map<std::string, Device*> deviceMap;
    void registerPage(const char* uri, Page* p) {
        pageMap[uri] = p;
        ESP_LOGI(TAG, "Página registrada: %s (%zu bytes, %s)", uri, p->size, p->mime.c_str());
    }
    const Page* getPage(const char* uri) {
        auto it = pageMap.find(uri);
        return (it != pageMap.end()) ? it->second : nullptr;
    }
    void registerDevice(const std::string& id, Device* device) {
        deviceMap[id] = device;
        ESP_LOGI(TAG, "Dispositivo registrado: ID=%s, Nome=%s, Tipo=%d, Status=%d", id.c_str(), device->name.c_str(), device->type, device->status);
    }
    const Device* getDevice(const std::string& id) {
        auto it = deviceMap.find(id);
        return (it != deviceMap.end()) ? it->second : nullptr;
    }
    size_t getDeviceCount() {
        return deviceMap.size();
    }
    std::vector<std::string> getDeviceIds() {
        std::vector<std::string> ids;
        ids.reserve(deviceMap.size());
        for (const auto& pair : deviceMap) {
            ids.push_back(pair.first);
        }
        return ids;
    }
    void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_IFOK) {
            ESP_LOGI(TAG, "Network IFOK recebido → checar SSID/password armazenados...");
            if(!GlobalConfigData::isBlankOrEmpty(GlobalConfigData::cfg->ssid)){
                EventBus::post(EventDomain::STORAGE, EventId::STO_SSIDOK);
            }
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
        EventBus::post(EventDomain::READY, EventId::STO_READY);
        ESP_LOGI(TAG, "→ STO_READY publicado");
        return ESP_OK;
    }
}