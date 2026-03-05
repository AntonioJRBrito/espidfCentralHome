#include "web_manager.hpp"

static const char* TAG = "WebManager";

namespace WebManager {
    static httpd_handle_t server = nullptr;
    static httpd_uri_t uri_handlers[30];
    static int uri_count = 0;
    static int fd_onclosed = 0;
    static uint32_t current_token = 0;
    static uint32_t gerarTokenNumerico() {return 10000000 + (esp_random() % 90000000);}
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
                } else {decoded_string += encoded_string[i];}
            } else if (encoded_string[i] == '+') {decoded_string += ' ';
            } else {decoded_string += encoded_string[i];}
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
        if(uri.find("description.xml")!=std::string::npos){
            int sockfd = httpd_req_to_sockfd(req);
            if (sockfd >= 0) {
                char ipstr[INET6_ADDRSTRLEN] = {0};
                uint16_t port = 0;
                struct sockaddr_storage addr;
                socklen_t addr_len = sizeof(addr);
                if (getpeername(sockfd, (struct sockaddr*)&addr, &addr_len) == 0) {
                    if (addr.ss_family == AF_INET) {
                        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
                        inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
                        port = ntohs(s->sin_port);
                    } else if (addr.ss_family == AF_INET6) {
                        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&addr;
                        // verifica se é IPv4-mapeado ::ffff:a.b.c.d
                        const unsigned char *b = (const unsigned char *)&s6->sin6_addr;
                        bool is_v4_mapped = (b[0]==0 && b[1]==0 && b[2]==0 && b[3]==0 &&
                                            b[4]==0 && b[5]==0 && b[6]==0 && b[7]==0 &&
                                            b[8]==0 && b[9]==0 && b[10]==0xFF && b[11]==0xFF);
                        if (is_v4_mapped) {
                            // extrai os últimos 4 bytes como IPv4
                            struct in_addr v4addr;
                            memcpy(&v4addr.s_addr, &b[12], 4);
                            inet_ntop(AF_INET, &v4addr, ipstr, sizeof(ipstr));
                        } else {
                            inet_ntop(AF_INET6, &s6->sin6_addr, ipstr, sizeof(ipstr));
                        }
                        port = ntohs(s6->sin6_port);
                    } else {
                        strncpy(ipstr, "unknown", sizeof(ipstr)-1);
                    }

                    ESP_LOGW(TAG, "description.xml solicitado por %s:%d", ipstr, port);
                } else {
                    ESP_LOGE(TAG, "getpeername failed: errno=%d", errno);
                }
            } else {
                ESP_LOGE(TAG, "httpd_req_to_sockfd returned invalid fd: %d", sockfd);
            }

        }
        if (!uri.empty() && uri.front() == '/') uri.erase(0, 1);
        size_t query_pos = uri.find('?');
        if(query_pos!=std::string::npos){uri=uri.substr(0,query_pos);}
        ESP_LOGI(TAG,"uri:%s",uri.c_str());
        if(uri=="atualizar.html"||uri=="central.html"||uri=="automacao.html"||uri=="agenda.html"){
            char query_str[64] = {0};
            if (httpd_req_get_url_query_str(req, query_str, sizeof(query_str)) == ESP_OK) {
                ESP_LOGI(TAG, "Query string: %s", query_str);
                char token_value[16] = {0};
                if (httpd_query_key_value(query_str, "token", token_value, sizeof(token_value)) == ESP_OK) {
                    uint32_t received_token = strtoul(token_value, nullptr, 10);
                    ESP_LOGI(TAG, "Token recebido: %u, esperado: %u", received_token, current_token);
                    if(received_token != current_token) {ESP_LOGW(TAG, "Token inválido");uri = "index.html";}
                    current_token = 0;
                } else {ESP_LOGW(TAG, "Parâmetro 'token' não encontrado");uri = "index.html";}
            } else {ESP_LOGW(TAG, "Token ausente");uri = "index.html";}
        }
        const Page* page = StorageManager::getPage(uri.c_str());
        if(!page){ESP_LOGW(TAG,"Arquivo estático não encontrado na PSRAM:%s",uri.c_str());httpd_resp_send_404(req);return ESP_ERR_NOT_FOUND;}
        httpd_resp_set_type(req, page->mime.c_str());
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, (const char*)page->data, page->size);
        ESP_LOGI(TAG, "Servido (PSRAM): %s (%zu bytes)", uri.c_str(), page->size);
        return ESP_OK;
    }
    static esp_err_t error_404_redirect_handler(httpd_req_t* req, httpd_err_code_t error) {
        ESP_LOGW(TAG, "Erro HTTP %d na URI: %s. Redirecionando para a raiz.", error, req->uri);
        return redirect_to_root_handler(req);
    }
    // --- Handler para redirecionamento de captive portal ---
    static esp_err_t redirect_to_root_handler(httpd_req_t* req) {
        ESP_LOGI(TAG, "Redirecionando %s para /", req->uri);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
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
                    if(StorageManager::isPassValid(provided_password)){
                        current_token = gerarTokenNumerico();
                        response_str = "sucesso:" + std::to_string(current_token);
                        ESP_LOGI(TAG, "Login bem-sucedido. Token gerado: %u", current_token);
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
        ESP_LOGI(TAG, "Encerrar command received, timer started para %d.",fd_onclosed);
        httpd_resp_sendstr(req, "Encerrar command received, shutdown timer started/reset.");
        return ESP_OK;
    }
    // handler GETINFO
    static esp_err_t getinfo_handler(httpd_req_t *req){
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_OK;
    }
    // --- Handlers para HA ---
    static esp_err_t create_user_handler(httpd_req_t* req){
        std::string username = "user_" + std::string(StorageManager::id_cfg->id);
        std::string response = R"([{"success":{"username":")"+username+R"("}}])";
        httpd_resp_set_type(req,"application/json");
        httpd_resp_send(req,response.c_str(),response.length());
        ESP_LOGI(TAG,"Username gerado: %s",username.c_str());
        return ESP_OK;
    }
    static esp_err_t list_lights_handler(httpd_req_t* req){
        const Page* page = StorageManager::getPage("devices.json");
        if(!page){ESP_LOGW(TAG,"Arquivo devices.json não encontrado na PSRAM");httpd_resp_send_500(req);return ESP_FAIL;}
        std::string response((char*)page->data,page->size);
        for (int i = 1; i <= 3; i++) {
            std::string device_id_str = std::to_string(i);
            const Device* device_ptr = StorageManager::getDevice(device_id_str);
            if (device_ptr) {
                std::string placeholder = "$DEVICE" + device_id_str + "$";
                std::string device_name(device_ptr->name);
                response = StorageManager::replacePlaceholders(response, placeholder.c_str(), device_name.c_str());
            }
        }
        httpd_resp_set_type(req,"application/json");
        httpd_resp_send(req,response.c_str(),response.length());
        ESP_LOGI(TAG, "Lista de devices retornada (%zu bytes)", response.length());
        return ESP_OK;
    }
    static esp_err_t get_light_handler(httpd_req_t* req){
        std::string uri(req->uri);
        size_t last_slash = uri.rfind('/');
        if(last_slash == std::string::npos){httpd_resp_send_404(req);return ESP_OK;}
        std::string device_id_str = uri.substr(last_slash + 1);
        int device_id = std::stoi(device_id_str);
        if(device_id < 1 || device_id > 3) {httpd_resp_send_404(req);return ESP_OK;}
        const Page* page = StorageManager::getPage("device.json");
        if(!page){ESP_LOGW(TAG,"Arquivo device.json não encontrado na PSRAM");httpd_resp_send_500(req);return ESP_FAIL;}
        std::string response((char*)page->data,page->size);
        const Device* device_ptr = StorageManager::getDevice(device_id_str);
        if (!device_ptr) {httpd_resp_send_500(req);return ESP_FAIL;}
        response = StorageManager::replacePlaceholders(response,"$DEVICE$",device_id_str.c_str());
        response = StorageManager::replacePlaceholders(response,"$DEVICENAME$",device_ptr->name);
        response = StorageManager::replacePlaceholders(response,"$STATUS$",device_ptr->status ? "true" : "false");
        httpd_resp_set_type(req,"application/json");
        httpd_resp_send(req,response.c_str(),response.length());
        ESP_LOGI(TAG,"Estado do device %d (%s) retornado", device_id, device_ptr->name);
        return ESP_OK;
    }
    static esp_err_t set_light_handler(httpd_req_t* req){
        std::string uri(req->uri);
        if(uri.find("/state")==std::string::npos){httpd_resp_send_404(req);return ESP_OK;}
        size_t last_slash = uri.rfind('/');
        if(last_slash==std::string::npos||uri.substr(last_slash)!="/state"){httpd_resp_send_404(req);return ESP_OK;}
        size_t second_last_slash = uri.rfind('/', last_slash - 1);
        if(second_last_slash==std::string::npos){httpd_resp_send_404(req);return ESP_OK;}
        std::string device_id_str=uri.substr(second_last_slash + 1, last_slash - second_last_slash - 1);
        int device_id = std::stoi(device_id_str);
        if (device_id < 1 || device_id > 3) {httpd_resp_send_404(req);return ESP_OK;}
        char buf[256] = {0};
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) {httpd_resp_send_404(req);return ESP_FAIL;}
        cJSON* json = cJSON_Parse(buf);
        if (!json) {httpd_resp_send_404(req);return ESP_FAIL;}
        cJSON* on_item = cJSON_GetObjectItem(json, "on");
        if(!on_item||!cJSON_IsBool(on_item)){cJSON_Delete(json);httpd_resp_send_404(req);return ESP_FAIL;}
        bool new_state = on_item->type == cJSON_True;
        cJSON_Delete(json);
        const Device* device_ptr = StorageManager::getDevice(device_id_str);
        if (!device_ptr) {httpd_resp_send_500(req);return ESP_FAIL;}
        DeviceDTO device_dto;
        memcpy(&device_dto, device_ptr, sizeof(DeviceDTO));
        device_dto.status=new_state;
        RequestSave requester;
        requester.requester=device_id;
        requester.request_int=device_id;
        requester.resquest_type=RequestTypes::REQUEST_INT;
        StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::DEVICE_DATA,&device_dto,sizeof(DeviceDTO),requester,EventId::STO_DEVICESAVED);
        std::string response= R"([{"success": {"/lights/)"+device_id_str+R"(/state/on": )"+std::string(new_state ?"true":"false")+R"(}}])";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response.c_str(), response.length());
        ESP_LOGI(TAG, "Device %d alterada para %s",device_id,new_state ? "1" : "0");
        return ESP_OK;
    }
    // --- Handler redirecionador para "/" ---
    static esp_err_t not_found_handler(httpd_req_t* req){
        ESP_LOGW(TAG, "404 Not Found: %s. Redirecionando para /", req->uri);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    // --- Handler para a raiz "/" (serve index.html) ---
    static esp_err_t root_handler(httpd_req_t* req){
        const Page* page = StorageManager::getPage("index.html");
        if(!page){ESP_LOGW(TAG,"index.html não encontrado");httpd_resp_send_404(req);return ESP_OK;}
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, (const char*)page->data, page->size);
        ESP_LOGI(TAG, "Servido: / (index.html) (%zu bytes)", page->size);
        return ESP_OK;
    }
    // --- Handler do health da Central ---
    static esp_err_t health_get_handler(httpd_req_t *req)
    {
        const char *resp = "OK";
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        esp_err_t err = httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(TAG, "GET /health -> OK");
        return err;
    }
    // --- Registrador de handlers ---
    static void registerUriHandler(const char* description, http_method method, esp_err_t (*handler)(httpd_req_t *r)) {
        if(uri_count>=40){ESP_LOGE(TAG,"Número máximo de URIs atingido");return;}
        httpd_uri_t* uri = &uri_handlers[uri_count++];
        uri->uri = description;
        uri->method = method;
        uri->handler = handler;
        uri->user_ctx = nullptr;
        esp_err_t ret = httpd_register_uri_handler(server, uri);
        if (ret == ESP_OK) {ESP_LOGI(TAG, "✓ URI '%s' registrada com sucesso", description);}
        else {ESP_LOGE(TAG, "✗ Falha ao registrar URI '%s': %s", description, esp_err_to_name(ret));}
    }
    // --- Inicializa servidor HTTPS ---
    static void startServer() {
        if (server) { ESP_LOGW(TAG, "Servidor já em execução"); return; }
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.lru_purge_enable = true;
        config.uri_match_fn = httpd_uri_match_wildcard;
        config.max_uri_handlers = 40;
        config.max_open_sockets = 20;
        config.backlog_conn = 12;
        config.task_priority = tskIDLE_PRIORITY + 2;
        config.stack_size = 8192; 
        if(httpd_start(&server,&config)!=ESP_OK){ESP_LOGE(TAG,"Falha ao iniciar servidor HTTP");return;}
        else{ESP_LOGI(TAG, "Servidor HTTP executando");}
        // 1. Rotas estáticas principais (HTML/JSON/CSS/JS/IMG)
        registerUriHandler("/",HTTP_GET,root_handler);
        registerUriHandler("/teste.html",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/index.html",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/agenda.html",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/automacao.html",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/atualizar.html*",HTTP_GET,serve_static_file_handler);
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
        registerUriHandler("/health",HTTP_GET,health_get_handler);
        registerUriHandler("/encerrar/*",HTTP_POST,encerrar_handler);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, error_404_redirect_handler);
        // 4. GETINFO perdido
        registerUriHandler("/GETINFO",HTTP_GET,getinfo_handler);
        registerUriHandler("/GETINFO",HTTP_POST,getinfo_handler);
        // 5. Rotas ha
        registerUriHandler("/description.xml",HTTP_GET,serve_static_file_handler);
        registerUriHandler("/api",HTTP_POST,create_user_handler);
        std::string username = "user_" + std::string(StorageManager::id_cfg->id);
        std::string uri_lights = "/api/" + username + "/lights";
        std::string uri_light_id = "/api/" + username + "/lights/*";
        registerUriHandler(uri_lights.c_str(),HTTP_GET,list_lights_handler);
        registerUriHandler(uri_light_id.c_str(),HTTP_GET,get_light_handler);
        registerUriHandler(uri_light_id.c_str(),HTTP_PUT,set_light_handler);

        // ESP_LOGI(TAG, "HTTP server ativo (porta %d)", config.server_port);
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
        EventBus::regHandler(EventDomain::WEB, &onWebEvent, nullptr);
        ESP_LOGI(TAG, "Timer de encerramento criado.");
        EventBus::post(EventDomain::READY, EventId::WEB_READY);
        ESP_LOGI(TAG, "→ WEB_READY publicado; aguardando NET_IFOK");
        return ESP_OK;
    }
}