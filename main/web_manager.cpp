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
        const Page* page = StorageManager::getPage("description.xml");
        if(!page){ESP_LOGW(TAG, "description.xml não encontrado");return ESP_ERR_NOT_FOUND;}
        std::string content((char*)page->data, page->size);
        content=GlobalConfigData::replacePlaceholders(content,"$IP$",GlobalConfigData::cfg->ip);
        content=GlobalConfigData::replacePlaceholders(content,"$ID$",GlobalConfigData::cfg->id);
        httpd_resp_set_type(req, page->mime.c_str());
        httpd_resp_send(req, content.c_str(), content.length());
        ESP_LOGW(TAG,"%s", content.c_str());
        return ESP_OK;
    }
    static esp_err_t api_handler(httpd_req_t* req) {
        const Page* page = StorageManager::getPage("apiget.json");
        if(!page){ESP_LOGW(TAG, "apiget.json não encontrado");return ESP_ERR_NOT_FOUND;}
        std::string content((char*)page->data, page->size);
        content=GlobalConfigData::replacePlaceholders(content,"$ID$",GlobalConfigData::cfg->id);
        httpd_resp_set_type(req, page->mime.c_str());
        httpd_resp_send(req, content.c_str(), content.length());
        ESP_LOGW(TAG,"%s", content.c_str());
        return ESP_OK;
    }
    static esp_err_t lights_handler_get(httpd_req_t* req) {
        std::string uri = req->uri;
        ESP_LOGI(TAG, "GET: %s", uri.c_str());
        size_t pos = uri.find("/lights");
        if(pos == std::string::npos){return ESP_ERR_NOT_FOUND;}
        std::string id = uri.substr(pos + 8);
        if (id.empty()) {
            const Page* page = StorageManager::getPage("lights_all.json");
            if (!page) {ESP_LOGE(TAG, "lights_all.json não encontrado");return ESP_FAIL;}
            std::string content((char*)page->data, page->size);
            content = GlobalConfigData::replacePlaceholders(content,"$MAC$",GlobalConfigData::cfg->mac);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, content.c_str(), content.length());
            ESP_LOGI(TAG, "Servido: lights_all.json (%zu bytes)", content.length());
            return ESP_OK;
        }
        int device_id = atoi(id.c_str());
        if (device_id < 1 || device_id > 3) {ESP_LOGW(TAG, "ID inválido: %d", device_id);return ESP_ERR_INVALID_ARG;}
        const Page* page = StorageManager::getPage("api/light_detail.json");
        if (!page) {ESP_LOGE(TAG, "light_detail.json não encontrado");return ESP_FAIL;}
        std::string content((char*)page->data, page->size);
        content = GlobalConfigData::replacePlaceholders(content,"$DEVICE",std::to_string(device_id));
        // content = GlobalConfigData::replacePlaceholders(content,"$DEVICENAME$",???);
        content = GlobalConfigData::replacePlaceholders(content,"$MAC",GlobalConfigData::cfg->mac);
        
        // Substitui status
        bool device_status = false; // TODO: buscar status real
        content = GlobalConfigData::replacePlaceholders(content, "$STATUS$", device_status ? "true" : "false");
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, content.c_str(), content.length());
        
        ESP_LOGI(TAG, "Servido: light_detail.json para ID=%d (%zu bytes)", device_id, content.length());
        return ESP_OK;
    }
    // --- Handler redirecionador para "/" ---
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
        if(!page){ESP_LOGW(TAG,"index.html não encontrado");httpd_resp_send_404(req);return ESP_OK;}
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, (const char*)page->data, page->size);
        ESP_LOGI(TAG, "Servido: / (index.html) (%zu bytes)", page->size);
        return ESP_OK;
    }
    // --- Registrador de handlers ---
    static void registerUriHandler(const char* description,http_method method,esp_err_t (*handler)(httpd_req_t *r)){
        ESP_LOGI(TAG, "→ Registrando URI: '%s' (método=%d)", description, method);  // ← LOG ADICIONADO
        httpd_uri_t uri_buf;
        uri_buf.uri = description;
        uri_buf.method = method;
        uri_buf.handler = handler;
        uri_buf.user_ctx = nullptr;
        esp_err_t ret = httpd_register_uri_handler(server, &uri_buf);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✓ URI '%s' registrada com sucesso", description);
        } else {
            ESP_LOGE(TAG, "✗ Falha ao registrar URI '%s': %s", description, esp_err_to_name(ret));
        }
    }
    // --- Inicializa servidor HTTP ---
    static void startServer() {
        if (server) { ESP_LOGW(TAG, "Servidor já em execução"); return; }
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.lru_purge_enable = true;
        config.uri_match_fn = httpd_uri_match_wildcard;
        config.max_uri_handlers = 30;
        if(httpd_start(&server,&config)!=ESP_OK){ESP_LOGE(TAG,"Falha ao iniciar servidor HTTP");return;}
        else{ESP_LOGI(TAG, "Servidor HTTP executando");}
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
        // 4. Rotas ha
        registerUriHandler("/description.xml",HTTP_GET,upnp_description_handler);
        registerUriHandler("/api",(httpd_method_t)HTTP_ANY,api_handler);

        //   putgetSTR="/api/"+ID+"/lights";

        // 6. Handler catch-all para 404
        ESP_LOGI(TAG, "HTTP server ativo (porta %d)", config.server_port);
    }
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*) {
        if (static_cast<EventId>(id)==EventId::NET_IFOK) {
            ESP_LOGI(TAG, "NET_IFOK → iniciando servidor HTTP");
            startServer();
            EventBus::unregHandler(EventDomain::NETWORK, &onNetworkEvent);
            EventBus::post(EventDomain::WEB,EventId::WEB_STARTED,&server,sizeof(httpd_handle_t));
            ESP_LOGI(TAG, "→ WEB_STARTED publicado");
        }
    }
    static void onSocketEvent(void*, esp_event_base_t, int32_t id, void*) {
        if (static_cast<EventId>(id)==EventId::SOC_STARTED) {
            ESP_LOGI(TAG, "SOC_STARTED → iniciando '*'");
            httpd_uri_t uri_buf;
            uri_buf.uri = "*";
            uri_buf.method = (httpd_method_t)HTTP_ANY;
            uri_buf.handler = redirect_to_root_handler;
            esp_err_t ret = httpd_register_uri_handler(server, &uri_buf);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✓ URI '*' registrada com sucesso");
            } else {
                ESP_LOGE(TAG, "✗ Falha ao registrar URI '*': %s", esp_err_to_name(ret));
            }
            ESP_LOGI(TAG, "→ '*' publicado");
        }
    }
    esp_err_t init() {
        ESP_LOGI(TAG, "Inicializando WebManager...");
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        EventBus::regHandler(EventDomain::SOCKET, &onSocketEvent, nullptr);
        EventBus::post(EventDomain::READY, EventId::WEB_READY);
        ESP_LOGI(TAG, "→ WEB_READY publicado; aguardando NET_IFOK");
        return ESP_OK;
    }
}