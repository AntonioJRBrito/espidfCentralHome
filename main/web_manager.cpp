#include "web_manager.hpp"

static const char* TAG = "WebManager";
namespace WebManager {
    static httpd_handle_t server = nullptr;
    static esp_err_t handler_default(httpd_req_t* req) {
        const char* uri = req->uri;
        if (strcmp(uri, "/") == 0) uri = "/index.html";
        const Page* page = StorageManager::getPage(uri);
        if (!page){ESP_LOGW(TAG,"404: %s",uri);httpd_resp_send_404(req);return ESP_OK;}
        httpd_resp_set_type(req, page->mime.c_str());
        httpd_resp_send(req, (const char*)page->data, page->size);
        ESP_LOGI(TAG, "Servindo: %s (%zu bytes)", uri, page->size);
        return ESP_OK;
    }
    static void startServer() {
        if (server) {ESP_LOGW(TAG, "Servidor HTTP já ativo");return;}
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.ctrl_port = 32768;
        config.stack_size = 8192;
        config.lru_purge_enable = true;
        if (httpd_start(&server, &config) == ESP_OK) {
            httpd_uri_t uri_all {
                .uri      = "*",
                .method   = HTTP_GET,
                .handler  = handler_default,
                .user_ctx = nullptr
            };
            httpd_register_uri_handler(server, &uri_all);
            ESP_LOGI(TAG, "HTTP Server iniciado — escutando em 0.0.0.0:%d", config.server_port);
        } else {
            ESP_LOGE(TAG, "Falha ao iniciar HTTP Server");
        }
    }
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_IFOK) {
            ESP_LOGI(TAG, "NET_IFOK → Iniciando servidor HTTP (modo AP/STA, 0.0.0.0)");
            startServer();
        }
    }
    static void onStorageEvent(void*, esp_event_base_t, int32_t id, void*) {
        EventId evt = static_cast<EventId>(id);
        ESP_LOGI(TAG, "Evento de STORAGE recebido (%d)", static_cast<int>(evt));
        // Futuro: servir dados dinâmicos via API (REST/JSON)
    }
    esp_err_t init() {
        ESP_LOGI(TAG, "Inicializando WebManager...");
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        EventBus::regHandler(EventDomain::STORAGE, &onStorageEvent, nullptr);
        EventBus::post(EventDomain::WEB, EventId::WEB_READY);
        ESP_LOGI(TAG, "→ WEB_READY publicado, aguardando NET_IFOK");
        return ESP_OK;
    }
}