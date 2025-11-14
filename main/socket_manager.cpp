#include "socket_manager.hpp"

static const char* TAG = "SocketManager";

namespace SocketManager {
    // Lista de file descriptors dos clientes WebSocket conectados
    static std::vector<int> ws_clients;
    static std::mutex clients_mutex;
    static httpd_handle_t ws_server = nullptr;
    static bool ws_registered = false;
    static httpd_uri_t ws_uri;
    static void info(int fd){
        cJSON* root = cJSON_CreateObject();
        if(!root){ESP_LOGE(TAG, "Falha ao criar objeto cJSON");return;}
        cJSON_AddStringToObject(root, "cNome", GlobalConfigData::cfg->central_name);
        cJSON_AddStringToObject(root, "isIA", "TRUEIA");
        cJSON_AddStringToObject(root, "userToken", GlobalConfigData::cfg->token_id);
        cJSON_AddStringToObject(root, "passToken", GlobalConfigData::cfg->token_password);
        cJSON_AddStringToObject(root, "useUserToken", GlobalConfigData::cfg->token_flag);
        cJSON_AddStringToObject(root, "token", GlobalConfigData::cfg->id);
        for (int i = 1; i <= 3; ++i) {
            std::string device_id_str = std::to_string(i);
            const Device* dev = StorageManager::getDevice(device_id_str);
            if (dev) {
                cJSON_AddNumberToObject(root, ("dTipo" + device_id_str).c_str(), dev->type);
                cJSON_AddStringToObject(root, ("dNome" + device_id_str).c_str(), dev->name.c_str());
                cJSON_AddNumberToObject(root, ("dTempo" + device_id_str).c_str(), dev->time);
            } else {
                ESP_LOGW(TAG, "Dispositivo interno %d não encontrado.", i);
                cJSON_AddStringToObject(root, ("dTipo" + device_id_str).c_str(), "0");
                cJSON_AddStringToObject(root, ("dNome" + device_id_str).c_str(), "");
                cJSON_AddStringToObject(root, ("dTempo" + device_id_str).c_str(), "0");
            }
        }
        if(GlobalConfigData::cfg->wifi_cache.is_sta_connected){
            cJSON_AddStringToObject(root, "conexao", "con");
        } else {
            cJSON_AddStringToObject(root, "conexao", "dis");
        }
        const char* ssid_value = (strlen(GlobalConfigData::cfg->ssid) == 0) ? "" : GlobalConfigData::cfg->ssid;
        cJSON_AddStringToObject(root, "ssid", ssid_value);
        const char* json_string = cJSON_PrintUnformatted(root);
        if(!json_string){ESP_LOGE(TAG, "Falha ao serializar objeto cJSON");cJSON_Delete(root);return;}
        std::string full_message = "listInfo" + std::string(json_string);
        esp_err_t ret = sendToClient(fd, full_message.c_str());
        if(ret != ESP_OK){ESP_LOGE(TAG,"Falha ao servir listInfo");}
        else{ESP_LOGI(TAG, "Servido listInfo");}
        cJSON_Delete(root);
        free((void*)json_string);
    }
    // Adiciona cliente à lista
    static void addClient(int fd) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = std::find(ws_clients.begin(), ws_clients.end(), fd);
        if(it==ws_clients.end()){ws_clients.push_back(fd);ESP_LOGI(TAG,"WS conectado (fd=%d). Total: %zu",fd,ws_clients.size());}
    }
    // Remove cliente da lista
    static void removeClient(int fd) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = std::find(ws_clients.begin(), ws_clients.end(), fd);
        if(it!=ws_clients.end()){ws_clients.erase(it);ESP_LOGI(TAG,"WS desconectado (fd=%d). Total: %zu",fd,ws_clients.size());}
    }
    // Handler do WebSocket
    static esp_err_t ws_handler(httpd_req_t* req) {
        if(req->method==HTTP_GET){ESP_LOGI(TAG,"Handshake WebSocket recebido");return ESP_OK;}
        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;
        esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
        if(ret!=ESP_OK){ESP_LOGE(TAG,"Erro de tamanho do frame: %s",esp_err_to_name(ret));return ret;}
        ESP_LOGI(TAG,"Frame recebido: len=%d, type=%d", ws_pkt.len, ws_pkt.type);
        if(ws_pkt.len==0){return ESP_OK;}
        uint8_t* buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if(!buf){ESP_LOGE(TAG,"Falha ao alocar memória para payload");return ESP_ERR_NO_MEM;}
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if(ret!=ESP_OK){ESP_LOGE(TAG,"Erro ao receber payload: %s",esp_err_to_name(ret));free(buf);return ret;}
        buf[ws_pkt.len] = '\0';
        std::string message((char*)buf);
        ESP_LOGI(TAG, "Mensagem WebSocket recebida: %s", message.c_str());
        int fd = httpd_req_to_sockfd(req);
        addClient(fd);
        if (message == "NET") {
            ESP_LOGI(TAG, "Comando NET recebido, solicitando lista WiFi para fd=%d", fd);
            EventBus::post(EventDomain::NETWORK, EventId::NET_LISTQRY, &fd, sizeof(int));
        }
        else if (message == "INFO") {
            ESP_LOGI(TAG, "Comando INFO recebido para fd=%d", fd);
            info(fd);
        }
        else {ESP_LOGW(TAG, "Comando desconhecido: %s", message.c_str());}
        free(buf);
        return ret;
    }
    // Envia mensagem para um cliente específico
    esp_err_t sendToClient(int fd, const char* message) {
        if (!ws_server || !message) {return ESP_ERR_INVALID_ARG;}
        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;
        ws_pkt.payload = (uint8_t*)message;
        ws_pkt.len = strlen(message);
        esp_err_t ret = httpd_ws_send_frame_async(ws_server, fd, &ws_pkt);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Falha ao enviar para fd=%d: %s", fd, esp_err_to_name(ret));
            removeClient(fd);
        }
        return ret;
    }
    // Broadcast para todos os clientes
    esp_err_t broadcast(const char* message) {
        if (!message) {return ESP_ERR_INVALID_ARG;}
        std::lock_guard<std::mutex> lock(clients_mutex);
        ESP_LOGI(TAG, "Broadcast para %zu clientes: %s", ws_clients.size(), message);
        std::vector<int> clients_copy = ws_clients;
        for (int fd : clients_copy) {sendToClient(fd, message);}
        return ESP_OK;
    }
    // Retorna número de clientes conectados
    size_t getClientCount() {
        std::lock_guard<std::mutex> lock(clients_mutex);
        return ws_clients.size();
    }
    //Handler de eventos do EventDomain::NETWORK
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void* data) {
        EventId evt = static_cast<EventId>(id);
        int client_fd = -1;
        if(data){memcpy(&client_fd,data,sizeof(int));ESP_LOGD(TAG,"onNetworkEvent: fd=%d, evt=%d",client_fd,(int)evt);}
        if (evt == EventId::NET_LISTOK) {
            ESP_LOGI(TAG, "NET_LISTOK recebido para fd=%d", client_fd);
            const char* html_content = GlobalConfigData::cfg->wifi_cache.networks_html_ptr;
            size_t content_len = GlobalConfigData::cfg->wifi_cache.networks_html_len;
            if (content_len > 0) {
                esp_err_t ret = sendToClient(client_fd, html_content);
                if (ret == ESP_OK) {ESP_LOGI(TAG, "Lista WiFi enviada para fd=%d (%zu bytes)", client_fd, content_len);}
                else {ESP_LOGE(TAG, "Falha ao enviar lista para fd=%d: %s", client_fd, esp_err_to_name(ret));}
            } else {
                ESP_LOGW(TAG, "Cache WiFi vazio para fd=%d", client_fd);
                const char* error_msg = "listNet<option value=''>Erro: cache vazio</option>";
                sendToClient(client_fd, error_msg);
            }
        }
        else {ESP_LOGD(TAG, "Evento de rede ignorado: %d", (int)evt);}
    }
    // Inicia o WebSocket (chamado após o HTTP server estar rodando)
    esp_err_t start(httpd_handle_t server) {
        if (!server) {ESP_LOGE(TAG, "HTTP server inválido");return ESP_FAIL;}
        if (ws_registered) {ESP_LOGW(TAG, "WebSocket já registrado, ignorando...");return ESP_OK;}
        ws_server = server;
        ESP_LOGI(TAG, "Registrando WebSocket em /ws...");
        ws_uri.uri = "/ws";
        ws_uri.method = HTTP_GET;
        ws_uri.handler = ws_handler;
        ws_uri.user_ctx = nullptr;
        ws_uri.is_websocket = true;
        ws_uri.handle_ws_control_frames = false;
        ws_uri.supported_subprotocol = nullptr;
        esp_err_t ret = httpd_register_uri_handler(server, &ws_uri);
        if (ret == ESP_OK) {
            ws_registered = true;
            ESP_LOGI(TAG, "✓ WebSocket registrado com sucesso");
            EventBus::post(EventDomain::SOCKET, EventId::SOC_STARTED);
            ESP_LOGI(TAG, "→ SOC_STARTED publicado");
        } else {ESP_LOGE(TAG, "Falha ao registrar WebSocket handler");}
        return ret;
    }
    // Para o WebSocket
    esp_err_t stop() {
        std::lock_guard<std::mutex> lock(clients_mutex);
        ws_clients.clear();
        ws_server = nullptr;
        ws_registered = false;
        ESP_LOGI(TAG, "WebSocket parado");
        return ESP_OK;
    }
    static void onWebEvent(void*, esp_event_base_t, int32_t id, void* data) {
        ESP_LOGI(TAG, "→ onWebEvent chamado: id=%d", (int)id);  // ← NOVO LOG
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::WEB_STARTED) {
            ESP_LOGI(TAG, "WEB_STARTED recebido, iniciando WebSocket...");
            httpd_handle_t* server_ptr = (httpd_handle_t*)data;
            if (server_ptr && *server_ptr) {start(*server_ptr);}
            else {ESP_LOGE(TAG, "HTTP server inválido no evento WEB_STARTED");}
        }
    }
    // Inicialização
    esp_err_t init() {
        ESP_LOGI(TAG, "Inicializando Socket Manager...");
        EventBus::regHandler(EventDomain::WEB, &onWebEvent, nullptr);
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        EventBus::post(EventDomain::READY, EventId::SOC_READY);
        ESP_LOGI(TAG, "→ SOC_READY publicado");
        return ESP_OK;
    }
}
