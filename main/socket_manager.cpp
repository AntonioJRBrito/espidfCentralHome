#include "socket_manager.hpp"

static const char* TAG = "SocketManager";

namespace SocketManager {
    // Lista de file descriptors dos clientes WebSocket conectados
    static std::vector<int> ws_clients;
    static std::mutex clients_mutex;
    static httpd_handle_t ws_server = nullptr;
    static bool ws_registered = false;
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
        // Se for GET, é o handshake inicial
        if(req->method==HTTP_GET){ESP_LOGI(TAG,"Handshake WebSocket recebido");return ESP_OK;}
        // Recebe frame do WebSocket
        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        // Define o tipo esperado
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;
        // Primeiro, obtém o tamanho do payload
        esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
        if(ret!=ESP_OK){ESP_LOGE(TAG,"Erro de tamanho do frame: %s",esp_err_to_name(ret));return ret;}
        ESP_LOGI(TAG,"Frame recebido: len=%d, type=%d", ws_pkt.len, ws_pkt.type);
        // Se for frame vazio, ignora
        if(ws_pkt.len==0){return ESP_OK;}
        // Aloca buffer para o payload
        uint8_t* buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if(!buf){ESP_LOGE(TAG,"Falha ao alocar memória para payload");return ESP_ERR_NO_MEM;}
        // Agora recebe o payload completo
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if(ret!=ESP_OK){ESP_LOGE(TAG,"Erro ao receber payload: %s",esp_err_to_name(ret));free(buf);return ret;}
        // Adiciona terminador nulo
        buf[ws_pkt.len] = '\0';
        // Processa mensagem recebida
        ESP_LOGI(TAG, "Mensagem WebSocket recebida: %s", (char*)buf);
        // Publica evento no EventBus
        // EventBus::post(EventDomain::SOCKET, EventId::SOC_MESSAGE, buf, ws_pkt.len + 1);
        
        // Echo: envia de volta para o cliente  - será??????
        httpd_ws_frame_t ws_resp;
        memset(&ws_resp, 0, sizeof(httpd_ws_frame_t));
        ws_resp.type = HTTPD_WS_TYPE_TEXT;
        ws_resp.payload = buf;
        ws_resp.len = ws_pkt.len;
        ret = httpd_ws_send_frame(req, &ws_resp);
        if(ret!=ESP_OK){ESP_LOGE(TAG,"Erro ao enviar resposta: %s",esp_err_to_name(ret));}
        free(buf);
        // Adiciona cliente à lista (se ainda não estiver)
        int fd = httpd_req_to_sockfd(req);
        addClient(fd);
        return ret;
    }
    
    // Envia mensagem para um cliente específico
    esp_err_t sendToClient(int fd, const char* message) {
        if (!ws_server || !message) {
            return ESP_ERR_INVALID_ARG;
        }
        
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
        if (!message) {
            return ESP_ERR_INVALID_ARG;
        }
        
        std::lock_guard<std::mutex> lock(clients_mutex);
        
        ESP_LOGI(TAG, "Broadcast para %zu clientes: %s", ws_clients.size(), message);
        
        // Cria cópia para evitar modificação durante iteração
        std::vector<int> clients_copy = ws_clients;
        
        for (int fd : clients_copy) {
            sendToClient(fd, message);
        }
        
        return ESP_OK;
    }
    
    // Retorna número de clientes conectados
    size_t getClientCount() {
        std::lock_guard<std::mutex> lock(clients_mutex);
        return ws_clients.size();
    }
    
    // Handler de eventos do EventBus
    static void onSocketEvent(void*, esp_event_base_t, int32_t id, void* data) {
        // EventId evt = static_cast<EventId>(id);
        
        // switch (evt) {
        //     case EventId::SOC_BROADCAST:
        //         // Broadcast solicitado via EventBus
        //         if (data) {
        //             broadcast((const char*)data);
        //         }
        //         break;
                
        //     case EventId::SOC_MESSAGE:
        //         // Mensagem recebida de cliente - já processada no handler
        //         ESP_LOGD(TAG, "Evento SOC_MESSAGE processado");
        //         break;
                
        //     default:
        //         break;
        // }
    }
    
    // Inicia o WebSocket (chamado após o HTTP server estar rodando)
    esp_err_t start(httpd_handle_t server) {
        if (!server) {ESP_LOGE(TAG, "HTTP server inválido");return ESP_FAIL;}
        if (ws_registered) {ESP_LOGW(TAG, "WebSocket já registrado, ignorando...");return ESP_OK;}
        ws_server = server;
        ESP_LOGI(TAG, "Registrando WebSocket em /ws...");
        httpd_uri_t ws_uri = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = ws_handler,
            .user_ctx = nullptr,
            .is_websocket = true,
            .handle_ws_control_frames = false,
            .supported_subprotocol = nullptr
        };
        esp_err_t ret = httpd_register_uri_handler(server, &ws_uri);
        if (ret == ESP_OK) {
            ws_registered = true;
            ESP_LOGI(TAG, "✓ WebSocket registrado com sucesso");
            EventBus::post(EventDomain::SOCKET, EventId::SOC_STARTED);
            ESP_LOGI(TAG, "→ SOC_STARTED publicado");
        } else {
            ESP_LOGE(TAG, "Falha ao registrar WebSocket handler");
        }
        
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
        // Registra handler de eventos
        EventBus::regHandler(EventDomain::SOCKET, &onSocketEvent, nullptr);
        EventBus::regHandler(EventDomain::WEB, &onWebEvent, nullptr);
        EventBus::post(EventDomain::READY, EventId::SOC_READY);
        ESP_LOGI(TAG, "→ SOC_READY publicado");
        return ESP_OK;
    }
}
