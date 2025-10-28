#include "storage_manager.hpp"

static const char* TAG = "StorageManager";
static std::unordered_map<std::string, StorageManager::Page> pageMap;

using namespace StorageManager;

static std::string mimeType(const std::string& path) {
    if (path.find(".html") != std::string::npos) return "text/html";
    if (path.find(".css")  != std::string::npos) return "text/css";
    if (path.find(".js")   != std::string::npos) return "application/javascript";
    if (path.find("logomarca") != std::string::npos) return "image/png";
    return "text/plain";
}

esp_err_t StorageManager::init() {
    ESP_LOGI(TAG, "Inicializando LittleFS...");
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));

    const char* files[] = {
        "/littlefs/index.html", "/littlefs/central.html", "/littlefs/agenda.html",
        "/littlefs/automacao.html", "/littlefs/atualizar.html",
        "/littlefs/css/bootstrap.min.css", "/littlefs/css/igra.css",
        "/littlefs/js/messages.js", "/littlefs/js/icons.js",
        "/littlefs/image/logomarca"
    };
    ESP_LOGI(TAG, "Carregando páginas p/ PSRAM...");

    for (auto f : files) {
        FILE* fp = fopen(f, "rb");
        if (!fp) { ESP_LOGW(TAG, "Arquivo %s não encontrado", f); continue; }

        fseek(fp, 0, SEEK_END);
        size_t sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (!buf) { ESP_LOGE(TAG, "PSRAM insuficiente p/ %s", f); fclose(fp); continue; }

        fread(buf, 1, sz, fp);
        fclose(fp);

        std::string key = std::string(f).erase(0, 7);  // tira "/spiffs"
        pageMap[key] = { buf, sz, mimeType(f) };
        ESP_LOGI(TAG, "→ %s (%u bytes)", key.c_str(), (unsigned)sz);
    }

    EventBus::post(EventDomain::STORAGE, EventId::STO_READY);
    ESP_LOGI(TAG, "→ STO_READY publicado");
    return ESP_OK;
}

const Page* StorageManager::get(const char* uri) {
    auto it = pageMap.find(uri);
    if (it != pageMap.end()) return &it->second;
    return nullptr;
}