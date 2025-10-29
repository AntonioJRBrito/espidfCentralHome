#include "web_manager.hpp"

static const char* TAG = "WebManager";

namespace WebManager {
    static httpd_handle_t server = nullptr;
    // --- Handler genérico para servir arquivos estáticos da PSRAM ---
    static esp_err_t serve_static_file_handler(httpd_req_t* req) {
        std::string uri = req->uri;
        if (!uri.empty() && uri.front() == '/') uri.erase(0, 1);
        const Page* page = StorageManager::getPage(uri.c_str());
        if (!page) {
            ESP_LOGW(TAG, "Arquivo estático não encontrado na PSRAM: %s", uri.c_str());
            httpd_resp_send_404(req);
            return ESP_ERR_NOT_FOUND;
        }
        httpd_resp_set_type(req, page->mime.c_str());
        httpd_resp_send(req, (const char*)page->data, page->size);
        ESP_LOGI(TAG, "Servido (PSRAM): %s (%zu bytes)", uri.c_str(), page->size);
        return ESP_OK;
    }
    // --- Handler para redirecionamento de captive portal ---
    static esp_err_t redirect_to_root_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "Redirecionando %s para /", req->uri);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    // --- Handlers para configuração e controle da central ---
    static esp_err_t get_info_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "GET /GET/info (POST)");
        // TODO: Implementar lógica para obter informações (ex: de GlobalConfigData)
        httpd_resp_sendstr(req, "GET info data placeholder");
        return ESP_OK;
    }
    static esp_err_t set_info_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "POST /SET/info");
        // TODO: Implementar lógica para definir informações (ler corpo da requisição)
        httpd_resp_sendstr(req, "SET info data placeholder");
        return ESP_OK;
    }
    static esp_err_t login_auth_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "POST /GET/login");
        // TODO: Implementar lógica de autenticação
        httpd_resp_sendstr(req, "Login attempt placeholder");
        return ESP_OK;
    }
    static esp_err_t get_config_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "GET /GET/isok");
        // TODO: Implementar lógica para obter configuração
        httpd_resp_sendstr(req, "Config data placeholder");
        return ESP_OK;
    }
    static esp_err_t encerrar_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "POST /encerrar");
        // TODO: Implementar lógica de encerramento a conexão (beacon)
        httpd_resp_sendstr(req, "Encerrar command received placeholder");
        return ESP_OK;
    }
    // --- Handlers para HA ---
    static esp_err_t upnp_description_handler(httpd_req_t* req) {
        std::string uri = req->uri;
        if (!uri.empty() && uri.front() == '/') uri.erase(0, 1);
        const Page* page = StorageManager::getPage(uri.c_str());
        if (!page) {
            ESP_LOGW(TAG, "Arquivo não encontrado: %s", uri.c_str());
            httpd_resp_send_404(req);
            return ESP_ERR_NOT_FOUND;
        }
        std::string content((char*)page->data, page->size);
        content=GlobalConfigData::replacePlaceholders(content,"$IP$",GlobalConfigData::cfg->ip);
        content=GlobalConfigData::replacePlaceholders(content,"$ID$",GlobalConfigData::cfg->id);
        httpd_resp_set_type(req, page->mime.c_str());
        httpd_resp_send(req, content.c_str(), content.length());
        ESP_LOGI(TAG,"%s", content.c_str());
        return ESP_OK;
    }
    static esp_err_t api_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "POST /api");
        // TODO: Implementar lógica da API genérica
        httpd_resp_sendstr(req, "API endpoint hit placeholder");
        return ESP_OK;
    }

    static esp_err_t upnp_setup_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "GET /setup.xml");
        // TODO: Servir setup.xml
        httpd_resp_set_type(req, "text/xml");
        httpd_resp_sendstr(req, "<root><setup>Setup info</setup></root>");
        return ESP_OK;
    }

    static esp_err_t upnp_sensor_response_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "UPnP Control/Event: %s", req->uri);
        // TODO: Lógica para responder a comandos UPnP
        httpd_resp_sendstr(req, "UPnP control response placeholder");
        return ESP_OK;
    }

    static esp_err_t upnp_subscribe_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "UPnP Subscribe: %s", req->uri);
        // TODO: Lógica para gerenciar subscrições UPnP
        httpd_resp_sendstr(req, "UPnP subscribe response placeholder");
        return ESP_OK;
    }

    static esp_err_t upnp_event_service_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "GET /eventservice.xml");
        // TODO: Servir eventservice.xml
        httpd_resp_set_type(req, "text/xml");
        httpd_resp_sendstr(req, "<root><eventService>Event service info</eventService></root>");
        return ESP_OK;
    }
    static esp_err_t not_found_handler(httpd_req_t* req) {
        ESP_LOGW(TAG, "404 Not Found: %s. Redirecionando para /", req->uri);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    // --- Handler para a raiz "/" (serve index.html) ---
    static esp_err_t root_handler(httpd_req_t* req) {
        const Page* page = StorageManager::getPage("index.html");
        if (!page) {
            ESP_LOGW(TAG, "index.html ausente na PSRAM para /");
            httpd_resp_send_404(req);
            return ESP_OK;
        }
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, (const char*)page->data, page->size);
        ESP_LOGI(TAG, "Servido: / (index.html) (%zu bytes)", page->size);
        return ESP_OK;
    }
    static void registerUriHandler(const char* description,http_method method,esp_err_t (*handler)(httpd_req_t *r)){
        httpd_uri_t uri_buf;
        uri_buf.uri=description;
        uri_buf.method=method;
        uri_buf.handler=handler;
        uri_buf.user_ctx=nullptr;
        httpd_register_uri_handler(server, &uri_buf);
    }
    // --- Inicializa servidor HTTP ---
    static void startServer() {
        if (server) { ESP_LOGW(TAG, "Servidor já em execução"); return; }
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.lru_purge_enable = true;
        config.uri_match_fn = httpd_uri_match_wildcard;
        config.max_uri_handlers = 30;
        if (httpd_start(&server, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao iniciar servidor HTTP");
            return;
        }
        // 1. Rotas estáticas principais (HTML/CSS/JS/IMG)
        registerUriHandler("/",HTTP_GET,root_handler);
        registerUriHandler("/index.html",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/agenda.html",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/automacao.html",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/atualizar.html",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/central.html",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/css/*",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/js/*",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/img/*",HTTP_GET,serve_static_file_handler);
        // 2. Captive Portal
        registerUriHandler("/generate_204",HTTP_GET,redirect_to_root_handler);
        registerUriHandler("/hotspot-detect.html",HTTP_GET,redirect_to_root_handler);
        registerUriHandler("/ncsi.txt",HTTP_GET,redirect_to_root_handler);
        // 3. Rotas de configuração e controle da central
        registerUriHandler("/GET/info",HTTP_POST,get_info_handler);
        registerUriHandler("/SET/info",HTTP_POST,set_info_handler);
        registerUriHandler("/GET/login",HTTP_POST,login_auth_handler);
        registerUriHandler("/GET/isok",HTTP_GET,get_config_handler);
        registerUriHandler("/encerrar",HTTP_POST,encerrar_handler);
        // 4. Rotas UPnP
        registerUriHandler("/description.xml",HTTP_GET,upnp_description_handler);
        registerUriHandler("/api",HTTP_POST,api_handler);

        httpd_uri_t uri_setup         = { .uri="/setup.xml",              .method=HTTP_GET, .handler=upnp_setup_handler,       .user_ctx=nullptr };
        httpd_uri_t uri_motion_control = { .uri="/upnp/control/motion1",   .method=(httpd_method_t)HTTP_ANY, .handler=upnp_sensor_response_handler, .user_ctx=nullptr };
        httpd_uri_t uri_basic_control  = { .uri="/upnp/control/basicevent1", .method=(httpd_method_t)HTTP_ANY, .handler=upnp_sensor_response_handler, .user_ctx=nullptr };
        httpd_uri_t uri_motion_event   = { .uri="/upnp/event/motion1",     .method=(httpd_method_t)HTTP_ANY, .handler=upnp_subscribe_handler,     .user_ctx=nullptr };
        httpd_uri_t uri_basic_event    = { .uri="/upnp/event/basicevent1", .method=(httpd_method_t)HTTP_ANY, .handler=upnp_subscribe_handler,     .user_ctx=nullptr };
        httpd_uri_t uri_event_service  = { .uri="/eventservice.xml",       .method=HTTP_GET, .handler=upnp_event_service_handler, .user_ctx=nullptr };

        httpd_register_uri_handler(server, &uri_setup);
        httpd_register_uri_handler(server, &uri_motion_control);
        httpd_register_uri_handler(server, &uri_basic_control);
        httpd_register_uri_handler(server, &uri_motion_event);
        httpd_register_uri_handler(server, &uri_basic_event);
        httpd_register_uri_handler(server, &uri_event_service);
        // 6. Handler catch-all para 404
        registerUriHandler("*",(httpd_method_t)HTTP_ANY,not_found_handler);
        ESP_LOGI(TAG, "HTTP server ativo (porta %d)", config.server_port);
    }
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*) {
        if (static_cast<EventId>(id)==EventId::NET_IFOK) {
            ESP_LOGI(TAG, "NET_IFOK → iniciando servidor HTTP");
            startServer();
            EventBus::unregHandler(EventDomain::NETWORK, &onNetworkEvent);
        }
    }
    esp_err_t init() {
        ESP_LOGI(TAG, "Inicializando WebManager...");
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        EventBus::post(EventDomain::WEB, EventId::WEB_READY);
        ESP_LOGI(TAG, "→ WEB_READY publicado; aguardando NET_IFOK");
        return ESP_OK;
    }
}