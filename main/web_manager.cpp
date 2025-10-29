#include "web_manager.hpp"

static const char* TAG = "WebManager";

namespace WebManager {

    static httpd_handle_t server = nullptr;

    // --- Handler genérico para servir arquivos estáticos da PSRAM ---
    // Retorna ESP_OK se serviu, ESP_ERR_NOT_FOUND se não encontrou na PSRAM.
    static esp_err_t serve_static_file_handler(httpd_req_t* req) {
        std::string uri = req->uri;
        // Remove a barra inicial se presente (ex: "/css/style.css" -> "css/style.css")
        if (!uri.empty() && uri.front() == '/') uri.erase(0, 1);

        const Page* page = StorageManager::getPage(uri.c_str());
        if (!page) {
            ESP_LOGW(TAG, "Arquivo estático não encontrado na PSRAM: %s", uri.c_str());
            return ESP_ERR_NOT_FOUND; // Indica que não encontrou, para o chamador decidir o que fazer (404 ou fallback)
        }

        httpd_resp_set_type(req, page->mime.c_str());
        httpd_resp_send(req, (const char*)page->data, page->size);
        ESP_LOGI(TAG, "Servido (PSRAM): %s (%zu bytes)", uri.c_str(), page->size);
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

    // --- Handlers para redirecionamento de captive portal ---
    // Redireciona requisições como /generate_204 para a raiz.
    static esp_err_t redirect_to_root_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "Redirecionando %s para /", req->uri);
        httpd_resp_set_status(req, "302 Found"); // Código de redirecionamento
        httpd_resp_set_hdr(req, "Location", "/"); // Nova localização
        httpd_resp_send(req, NULL, 0); // Envia resposta vazia
        return ESP_OK;
    }

    // --- Handler de fallback para 404 (Not Found) e redirecionamento final ---
    // Este handler é o ÚLTIMO a ser registrado e só é chamado se nenhuma outra URI corresponder.
    // Ele não tenta servir arquivos estáticos, apenas redireciona.
    static esp_err_t not_found_handler(httpd_req_t* req) {
        ESP_LOGW(TAG, "404 Not Found: %s. Redirecionando para /", req->uri);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // --- Placeholder Handlers para APIs e UPnP (implementação futura) ---
    // Estes handlers devem ser preenchidos com a lógica específica de cada API.
    // Por enquanto, apenas logam e enviam uma resposta simples.

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
        // TODO: Implementar lógica de encerramento/reset (ex: esp_restart())
        httpd_resp_sendstr(req, "Encerrar command received placeholder");
        return ESP_OK;
    }

    static esp_err_t api_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "POST /api");
        // TODO: Implementar lógica da API genérica
        httpd_resp_sendstr(req, "API endpoint hit placeholder");
        return ESP_OK;
    }

    static esp_err_t upnp_description_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "GET /description.xml");
        // TODO: Servir description.xml (pode estar na PSRAM ou ser gerado dinamicamente)
        httpd_resp_set_type(req, "text/xml");
        httpd_resp_sendstr(req, "<root><device><friendlyName>ESP32-S3 Central</friendlyName></device></root>");
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

    // --- Inicializa servidor HTTP ---
    static void startServer() {
        if (server) { ESP_LOGW(TAG, "Servidor já em execução"); return; }

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.lru_purge_enable = true;
        // ESSENCIAL: Habilita o matching de URIs com wildcard
        config.uri_match_fn = httpd_uri_match_wildcard;
        // AUMENTAR O NÚMERO DE SLOTS PARA HANDLERS
        config.max_uri_handlers = 30; // Suficiente para 26 handlers + margem

        if (httpd_start(&server, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao iniciar servidor HTTP");
            return;
        }

        // 1. Rotas estáticas principais (HTML)
        // O root_handler é especial para "/"
        httpd_uri_t uri_root      = { .uri="/",             .method=HTTP_GET, .handler=root_handler,            .user_ctx=nullptr };
        httpd_uri_t uri_index     = { .uri="/index.html",   .method=HTTP_GET, .handler=serve_static_file_handler, .user_ctx=nullptr };
        httpd_uri_t uri_agenda    = { .uri="/agenda.html",    .method=HTTP_GET, .handler=serve_static_file_handler, .user_ctx=nullptr };
        httpd_uri_t uri_automacao = { .uri="/automacao.html", .method=HTTP_GET, .handler=serve_static_file_handler, .user_ctx=nullptr };
        httpd_uri_t uri_atualizar = { .uri="/atualizar.html", .method=HTTP_GET, .handler=serve_static_file_handler, .user_ctx=nullptr };
        httpd_uri_t uri_central   = { .uri="/central.html",   .method=HTTP_GET, .handler=serve_static_file_handler, .user_ctx=nullptr };

        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &uri_index);
        httpd_register_uri_handler(server, &uri_agenda);
        httpd_register_uri_handler(server, &uri_automacao);
        httpd_register_uri_handler(server, &uri_atualizar);
        httpd_register_uri_handler(server, &uri_central);

        // 2. Rotas de redirecionamento para detecção de captive portal
        // ESTES SÃO OS HANDLERS QUE ENVIAM O 302 FOUND ESPERADO PELO CELULAR
        httpd_uri_t uri_gen204  = { .uri="/generate_204",      .method=HTTP_GET, .handler=redirect_to_root_handler, .user_ctx=nullptr };
        httpd_uri_t uri_hotspot = { .uri="/hotspot-detect.html", .method=HTTP_GET, .handler=redirect_to_root_handler, .user_ctx=nullptr };
        httpd_uri_t uri_ncsi    = { .uri="/ncsi.txt",          .method=HTTP_GET, .handler=redirect_to_root_handler, .user_ctx=nullptr };

        httpd_register_uri_handler(server, &uri_gen204);
        httpd_register_uri_handler(server, &uri_hotspot);
        httpd_register_uri_handler(server, &uri_ncsi);

        // 3. Rotas de API (POST/GET)
        httpd_uri_t uri_get_info  = { .uri="/GET/info",  .method=HTTP_POST, .handler=get_info_handler,  .user_ctx=nullptr };
        httpd_uri_t uri_set_info  = { .uri="/SET/info",  .method=HTTP_POST, .handler=set_info_handler,  .user_ctx=nullptr };
        httpd_uri_t uri_get_login = { .uri="/GET/login", .method=HTTP_POST, .handler=login_auth_handler, .user_ctx=nullptr };
        httpd_uri_t uri_get_isok  = { .uri="/GET/isok",  .method=HTTP_GET,  .handler=get_config_handler, .user_ctx=nullptr };
        httpd_uri_t uri_encerrar  = { .uri="/encerrar",  .method=HTTP_POST, .handler=encerrar_handler,  .user_ctx=nullptr };
        httpd_uri_t uri_api       = { .uri="/api",       .method=HTTP_POST, .handler=api_handler,       .user_ctx=nullptr };

        httpd_register_uri_handler(server, &uri_get_info);
        httpd_register_uri_handler(server, &uri_set_info);
        httpd_register_uri_handler(server, &uri_get_login);
        httpd_register_uri_handler(server, &uri_get_isok);
        httpd_register_uri_handler(server, &uri_encerrar);
        httpd_register_uri_handler(server, &uri_api);

        // 4. Rotas UPnP
        httpd_uri_t uri_desc          = { .uri="/description.xml",          .method=HTTP_GET, .handler=upnp_description_handler, .user_ctx=nullptr };
        httpd_uri_t uri_setup         = { .uri="/setup.xml",              .method=HTTP_GET, .handler=upnp_setup_handler,       .user_ctx=nullptr };
        httpd_uri_t uri_motion_control = { .uri="/upnp/control/motion1",   .method=(httpd_method_t)HTTP_ANY, .handler=upnp_sensor_response_handler, .user_ctx=nullptr };
        httpd_uri_t uri_basic_control  = { .uri="/upnp/control/basicevent1", .method=(httpd_method_t)HTTP_ANY, .handler=upnp_sensor_response_handler, .user_ctx=nullptr };
        httpd_uri_t uri_motion_event   = { .uri="/upnp/event/motion1",     .method=(httpd_method_t)HTTP_ANY, .handler=upnp_subscribe_handler,     .user_ctx=nullptr };
        httpd_uri_t uri_basic_event    = { .uri="/upnp/event/basicevent1", .method=(httpd_method_t)HTTP_ANY, .handler=upnp_subscribe_handler,     .user_ctx=nullptr };
        httpd_uri_t uri_event_service  = { .uri="/eventservice.xml",       .method=HTTP_GET, .handler=upnp_event_service_handler, .user_ctx=nullptr };

        httpd_register_uri_handler(server, &uri_desc);
        httpd_register_uri_handler(server, &uri_setup);
        httpd_register_uri_handler(server, &uri_motion_control);
        httpd_register_uri_handler(server, &uri_basic_control);
        httpd_register_uri_handler(server, &uri_motion_event);
        httpd_register_uri_handler(server, &uri_basic_event);
        httpd_register_uri_handler(server, &uri_event_service);

        // 5. Handlers para recursos estáticos (CSS, JS, IMG) usando wildcard de prefixo
        // Estes devem ser registrados ANTES do handler catch-all para 404.
        // O serve_static_file_handler já remove a barra inicial e busca no StorageManager.
        httpd_uri_t uri_css_prefix = { .uri="/css/*", .method=HTTP_GET, .handler=serve_static_file_handler, .user_ctx=nullptr };
        httpd_uri_t uri_js_prefix  = { .uri="/js/*",  .method=HTTP_GET, .handler=serve_static_file_handler, .user_ctx=nullptr };
        httpd_uri_t uri_img_prefix = { .uri="/img/*", .method=HTTP_GET, .handler=serve_static_file_handler, .user_ctx=nullptr };

        httpd_register_uri_handler(server, &uri_css_prefix);
        httpd_register_uri_handler(server, &uri_js_prefix);
        httpd_register_uri_handler(server, &uri_img_prefix);

        // 6. Handler catch-all para 404 final (deve ser o ÚLTIMO a ser registrado)
        // Este handler só será chamado se nenhuma das URIs acima corresponder.
        httpd_uri_t uri_catch_all = { .uri="*", .method=(httpd_method_t)HTTP_ANY, .handler=not_found_handler, .user_ctx=nullptr };
        httpd_register_uri_handler(server, &uri_catch_all);

        ESP_LOGI(TAG, "HTTP server ativo (porta %d)", config.server_port);
    }

    // --- Inicialização integrada com EventBus ---
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