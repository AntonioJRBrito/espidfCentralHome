#include "web_manager.hpp"

static const char* TAG = "WebManager";

namespace WebManager {
    static httpd_handle_t server = nullptr;
    static httpd_uri_t uri_handlers[30];
    static int uri_count = 0;
    static int fd_onclosed = 0;
    static uint32_t gerarTokenNumerico() {return 10000000 + (esp_random() % 90000000);}
    static TimerHandle_t shutdown_timer = nullptr;
    // funções de beacon
    void init_beacon(TimerHandle_t xTimer) {
        ESP_LOGW(TAG, "Timer de encerramento expirou (inatividade)! Postando evento de desligamento.");
        EventBus::post(EventDomain::WEB, EventId::WEB_STOPCLIENT,&fd_onclosed,sizeof(int));
    }
    void startShutdownTimer() {
        if (shutdown_timer != nullptr) {
            if (xTimerReset(shutdown_timer, 0) != pdPASS) {ESP_LOGE(TAG, "Falha ao iniciar/resetar o timer de encerramento!");}
            else {ESP_LOGI(TAG, "Timer de encerramento iniciado/resetado.");}
        } else {
            ESP_LOGE(TAG, "Tentativa de iniciar timer de encerramento não inicializado!");
        }
    }
    void stopShutdownTimer() {
        if (shutdown_timer != nullptr) {
            if (xTimerStop(shutdown_timer, 0) != pdPASS) {ESP_LOGE(TAG, "Falha ao parar o timer de encerramento!");}
            else {ESP_LOGI(TAG, "Timer de encerramento parado.");}
        } else {
            ESP_LOGE(TAG, "Tentativa de parar timer de encerramento não inicializado!");
        }
    }
    // Função para decodificar strings URL-encoded
    std::string url_decode(const std::string& encoded_string) {
        std::string decoded_string;
        for (size_t i = 0; i < encoded_string.length(); ++i) {
            if (encoded_string[i] == '%') {
                if (i + 2 < encoded_string.length()) {
                    char hex_char1 = encoded_string[i+1];
                    char hex_char2 = encoded_string[i+2];
                    char decoded_char = 0;
                    if (hex_char1 >= '0' && hex_char1 <= '9') decoded_char = (hex_char1 - '0') << 4;
                    else if (hex_char1 >= 'a' && hex_char1 <= 'f') decoded_char = (hex_char1 - 'a' + 10) << 4;
                    else if (hex_char1 >= 'A' && hex_char1 <= 'F') decoded_char = (hex_char1 - 'A' + 10) << 4;
                    if (hex_char2 >= '0' && hex_char2 <= '9') decoded_char |= (hex_char2 - '0');
                    else if (hex_char2 >= 'a' && hex_char2 <= 'f') decoded_char |= (hex_char2 - 'a' + 10);
                    else if (hex_char2 >= 'A' && hex_char2 <= 'F') decoded_char |= (hex_char2 - 'A' + 10);
                    decoded_string += decoded_char;
                    i += 2;
                } else {
                    decoded_string += encoded_string[i];
                }
            } else if (encoded_string[i] == '+') {
                decoded_string += ' ';
            } else {
                decoded_string += encoded_string[i];
            }
        }
        return decoded_string;
    }
    // Função para extrair um valor de um corpo de requisição POST URL-encoded
    std::string extract_post_param(const std::string& body, const std::string& key) {
        size_t start_pos = 0;
        std::string search_key = key + "=";
        while ((start_pos = body.find(search_key, start_pos)) != std::string::npos) {
            if (start_pos == 0 || body[start_pos - 1] == '&') {
                size_t value_start = start_pos + search_key.length();
                size_t value_end = body.find('&', value_start);
                std::string encoded_value = body.substr(value_start, value_end - value_start);
                return url_decode(encoded_value);
            }
            start_pos += search_key.length();
        }
        return "";
    }
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
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, (const char*)page->data, page->size);
        ESP_LOGI(TAG, "Servido (PSRAM): %s (%zu bytes)", uri.c_str(), page->size);
        stopShutdownTimer();
        return ESP_OK;
    }
    static esp_err_t error_404_redirect_handler(httpd_req_t* req, httpd_err_code_t error) {
        ESP_LOGW(TAG, "Erro HTTP %d na URI: %s. Redirecionando para a raiz.", error, req->uri);
        stopShutdownTimer();
        return redirect_to_root_handler(req);
    }
    // --- Handler para redirecionamento de captive portal ---
    static esp_err_t redirect_to_root_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "Redirecionando %s para /", req->uri);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        stopShutdownTimer();
        return ESP_OK;
    }
    static esp_err_t login_auth_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "POST /GET/login (senha pura)");
        std::string response_str = "erro";
        size_t content_len = req->content_len;
        char* content_buf = nullptr;
        if (content_len == 0) {
            ESP_LOGW(TAG, "Corpo da requisição POST vazio para /GET/login.");
        } else {
            content_buf = (char*)malloc(content_len + 1);
            if (content_buf == nullptr) {
                ESP_LOGE(TAG, "Falha ao alocar memória para o corpo da requisição.");
            } else {
                int ret = httpd_req_recv(req, content_buf, content_len);
                if (ret <= 0) {
                    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                        httpd_resp_send_408(req);
                    }
                    ESP_LOGE(TAG, "Falha ao ler corpo da requisição POST: %s", esp_err_to_name(ret));
                } else {
                    content_buf[ret] = '\0';
                    std::string provided_password = content_buf;
                    ESP_LOGD(TAG, "Senha recebida: '%s'", provided_password.c_str());
                    if (StorageManager::cd_cfg && !StorageManager::isBlankOrEmpty(StorageManager::cd_cfg->password) &&
                        provided_password == StorageManager::cd_cfg->password) {
                        uint32_t token = gerarTokenNumerico();
                        response_str = "sucesso:" + std::to_string(token);
                        ESP_LOGI(TAG, "Login bem-sucedido. Token gerado: %u", token);
                    } else {
                        response_str = "erro";
                        ESP_LOGW(TAG, "Login falhou: senha incorreta ou não configurada.");
                    }
                }
            }
            free(content_buf);
        }
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, response_str.c_str(), response_str.length());
        // resetShutdownTimer();
        return ESP_OK;
    }
    static esp_err_t get_config_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "GET /GET/isok");
        // TODO: Implementar lógica para obter configuração
        httpd_resp_sendstr(req, "Config data placeholder");
        // resetShutdownTimer();
        return ESP_OK;
    }
    static esp_err_t encerrar_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "POST /encerrar");
        std::string uri_str(req->uri);
        size_t last_slash_pos = uri_str.rfind('/');
        if (last_slash_pos == std::string::npos || last_slash_pos == uri_str.length() - 1) {
            ESP_LOGE(TAG, "URI inválida para /encerrar/&lt;fd&gt;: %s", req->uri);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid URI format");
            return ESP_FAIL;
        }
        std::string fd_str = uri_str.substr(last_slash_pos + 1);
        fd_onclosed = 0;
        if(std::sscanf(fd_str.c_str(),"%d",&fd_onclosed)!=1){ESP_LOGW(TAG,"FD inválido na URI: '%s'. Usando FD 0.",fd_str.c_str());}
        startShutdownTimer();
        ESP_LOGI(TAG, "Encerrar command received, timer started para %d.",fd_onclosed);
        httpd_resp_sendstr(req, "Encerrar command received, shutdown timer started/reset.");
        return ESP_OK;
    }
    // --- Handlers para HA ---
    static esp_err_t upnp_description_handler(httpd_req_t* req) {
        const Page* page = StorageManager::getPage("description.xml");
        if(!page){ESP_LOGW(TAG, "description.xml não encontrado");return ESP_ERR_NOT_FOUND;}
        std::string content((char*)page->data, page->size);
        content=StorageManager::replacePlaceholders(content,"$IP$",StorageManager::id_cfg->ip);
        content=StorageManager::replacePlaceholders(content,"$ID$",StorageManager::id_cfg->id);
        httpd_resp_set_type(req, page->mime.c_str());
        httpd_resp_send(req, content.c_str(), content.length());
        ESP_LOGW(TAG,"%s", content.c_str());
        return ESP_OK;
    }
    static esp_err_t api_handler(httpd_req_t* req) {
        const Page* page = StorageManager::getPage("apiget.json");
        if(!page){ESP_LOGW(TAG, "apiget.json não encontrado");return ESP_ERR_NOT_FOUND;}
        std::string content((char*)page->data, page->size);
        content=StorageManager::replacePlaceholders(content,"$ID$",StorageManager::id_cfg->id);
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
            content = StorageManager::replacePlaceholders(content,"$MAC$",StorageManager::id_cfg->mac);
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
        content = StorageManager::replacePlaceholders(content,"$DEVICE",std::to_string(device_id));
        // content = GlobalConfigData::replacePlaceholders(content,"$DEVICENAME$",???);
        content = StorageManager::replacePlaceholders(content,"$MAC",StorageManager::id_cfg->mac);
        
        // Substitui status
        bool device_status = false; // TODO: buscar status real
        content = StorageManager::replacePlaceholders(content, "$STATUS$", device_status ? "true" : "false");
        
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
    static void registerUriHandler(const char* description, http_method method, esp_err_t (*handler)(httpd_req_t *r)) {
        if (uri_count >= 30) {
            ESP_LOGE(TAG, "Número máximo de URIs atingido");
            return;
        }
        httpd_uri_t* uri = &uri_handlers[uri_count++];  // pega o próximo slot estático
        uri->uri = description;
        uri->method = method;
        uri->handler = handler;
        uri->user_ctx = nullptr;
        esp_err_t ret = httpd_register_uri_handler(server, uri);
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
        registerUriHandler("/favicon.ico",HTTP_GET,serve_static_file_handler);
        // 2. Captive Portal
        registerUriHandler("/generate_204",HTTP_GET,redirect_to_root_handler);
        registerUriHandler("/hotspot-detect.html",HTTP_GET,redirect_to_root_handler);
        registerUriHandler("/ncsi.txt",HTTP_GET,redirect_to_root_handler);
        // 3. Rotas de configuração e controle da central
        registerUriHandler("/GET/login",HTTP_POST,login_auth_handler);
        registerUriHandler("/encerrar/*",HTTP_POST,encerrar_handler);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, error_404_redirect_handler);
        // 4. Rotas ha
        registerUriHandler("/description.xml",HTTP_GET,upnp_description_handler);
        registerUriHandler("/api",(httpd_method_t)HTTP_ANY,api_handler);

        ESP_LOGI(TAG, "✓ Handler 404 registrado para redirecionar para a raiz.");

        //   putgetSTR="/api/"+ID+"/lights";

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
    static void onWebEvent(void*, esp_event_base_t, int32_t id, void*) {
    }
    esp_err_t init() {
        ESP_LOGI(TAG, "Inicializando WebManager...");
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        // EventBus::regHandler(EventDomain::SOCKET, &onSocketEvent, nullptr);
        EventBus::regHandler(EventDomain::WEB, &onWebEvent, nullptr);
        shutdown_timer = xTimerCreate("ShutdownTimer",pdMS_TO_TICKS(5000),pdFALSE,(void*)0,init_beacon);
        if(shutdown_timer==nullptr){ESP_LOGE(TAG,"Falha ao criar o timer de encerramento!");return ESP_FAIL;}
        ESP_LOGI(TAG, "Timer de encerramento criado.");
        EventBus::post(EventDomain::READY, EventId::WEB_READY);
        ESP_LOGI(TAG, "→ WEB_READY publicado; aguardando NET_IFOK");
        return ESP_OK;
    }
}