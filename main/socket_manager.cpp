#include "socket_manager.hpp"

static const char* TAG = "SocketManager";

namespace SocketManager {
    // Lista de file descriptors dos clientes WebSocket conectados
    static std::vector<WebSocketClient> ws_clients;
    static std::mutex clients_mutex;
    static httpd_handle_t ws_server = nullptr;
    static bool ws_registered = false;
    static httpd_uri_t ws_uri;
    static uint32_t lastAID;
    static void info(int fd){
        cJSON* root = cJSON_CreateObject();
        if(!root){ESP_LOGE(TAG, "Falha ao criar objeto cJSON");return;}
        cJSON_AddStringToObject(root, "cNome", StorageManager::cfg->central_name);
        cJSON_AddStringToObject(root, "isIA", "TRUEIA");
        cJSON_AddStringToObject(root, "userToken", StorageManager::cfg->token_id);
        cJSON_AddStringToObject(root, "passToken", StorageManager::cfg->token_password);
        cJSON_AddStringToObject(root, "useUserToken", StorageManager::cfg->token_flag);
        cJSON_AddStringToObject(root, "token", StorageManager::id_cfg->id);
        for (int i = 1; i <= 3; ++i) {
            std::string device_id_str = std::to_string(i);
            const Device* dev = StorageManager::getDevice(device_id_str);
            if (dev) {
                cJSON_AddNumberToObject(root, ("dTipo" + device_id_str).c_str(), dev->type);
                cJSON_AddStringToObject(root, ("dNome" + device_id_str).c_str(), dev->name);
                cJSON_AddNumberToObject(root, ("dTempo" + device_id_str).c_str(), dev->time);
            } else {
                ESP_LOGW(TAG, "Dispositivo interno %d não encontrado.", i);
                cJSON_AddStringToObject(root, ("dTipo" + device_id_str).c_str(), "0");
                cJSON_AddStringToObject(root, ("dNome" + device_id_str).c_str(), "");
                cJSON_AddStringToObject(root, ("dTempo" + device_id_str).c_str(), "0");
            }
        }
        if(StorageManager::scanCache->is_sta_connected){
            cJSON_AddStringToObject(root, "conexao", "con");
        } else {
            cJSON_AddStringToObject(root, "conexao", "dis");
        }
        const char* ssid_value = (strlen(StorageManager::cd_cfg->ssid) == 0) ? "" : StorageManager::cd_cfg->ssid;
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
        if(it==ws_clients.end()){ws_clients.push_back(WebSocketClient(fd,lastAID));ESP_LOGI(TAG,"Conectado fd=%d AID=%d.",fd,lastAID);}
    }
    // Remove cliente da lista
    static void removeClient(int fd) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = std::find(ws_clients.begin(), ws_clients.end(), fd);
        if(it!=ws_clients.end()){
            WebSocketClient& client = *it;
            esp_wifi_deauth_sta(client.aid);
            EventBus::post(EventDomain::NETWORK, EventId::NET_SUSPCLIENT,&client.aid,sizeof(client.aid));
            ws_clients.erase(it);
            ESP_LOGI(TAG,"WS desconectado (fd=%d). Total: %zu",fd,ws_clients.size());
        }
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
            std::string response_msg = "fd" + std::to_string(fd);
            sendToClient(fd,response_msg.c_str());
        }
        else if (message == "INFO") {
            ESP_LOGI(TAG, "Comando INFO recebido para fd=%d", fd);
            info(fd);
        }
        else if (strncmp(message.c_str(),"CONFIG",6) == 0) {
            const char* jsonString = message.c_str() + 6;
            ESP_LOGI(TAG, "Comando CONFIG recebido para fd=%d", fd);
            cJSON* root = cJSON_Parse(jsonString);
            if (root == nullptr) {ESP_LOGE(TAG, "Falha ao fazer parse do JSON CONFIG"); ret = ESP_FAIL;}
            else {
                for (int i = 1; i <= 3; ++i) {
                    // std::string device_id_str = std::to_string(i);
                    // const char* device_id_c_str = device_id_str.c_str();
                    // const Device* const_device_ptr = StorageManager::getDevice(device_id_str);
                    DeviceDTO device_dto;
                    snprintf(device_dto.id, sizeof(device_dto.id), "%d", i);
                    // strncpy(device_dto.id, device_id_c_str, sizeof(device_dto.id) - 1);
                    // device_dto.id[sizeof(device_dto.id) - 1] = '\0';
                    // if (const_device_ptr) {
                    //     memcpy(&device_dto, const_device_ptr, sizeof(DeviceDTO));
                    //     ESP_LOGI(TAG, "Device com ID '%s' encontrado na memória. Copiando para DTO local.", device_id_c_str);
                    // } else {
                    //     ESP_LOGW(TAG, "Device com ID '%s' não encontrado na memória. Criando um novo DTO local.", device_id_c_str);
                    //     strncpy(device_dto.id, device_id_c_str, sizeof(device_dto.id) - 1);
                    //     device_dto.id[sizeof(device_dto.id) - 1] = '\0';
                    // }
                    std::string dNomeKey = "dNome" + std::to_string(i);
                    std::string dTipoKey = "dTipo" + std::to_string(i);
                    std::string dTempoKey = "dTempo" + std::to_string(i);
                    cJSON* dNome = cJSON_GetObjectItem(root, dNomeKey.c_str());
                    cJSON* dTipo = cJSON_GetObjectItem(root, dTipoKey.c_str());
                    cJSON* dTempo = cJSON_GetObjectItem(root, dTempoKey.c_str());
                    if (dNome && dNome->valuestring) {
                        strncpy(device_dto.name, dNome->valuestring, sizeof(device_dto.name) - 1);
                        device_dto.name[sizeof(device_dto.name) - 1] = '\0';
                    }
                    if (dTipo && dTipo->valuestring) {
                        device_dto.type = std::stoi(dTipo->valuestring);
                    }
                    if (dTempo && dTempo->valuestring) {
                        device_dto.time = std::stoi(dTempo->valuestring);
                    }
                    device_dto.status = 0;
                    RequestSave requester;
                    requester.requester=fd;
                    requester.request_int=i;
                    requester.resquest_type=RequestTypes::REQUEST_INT;
                    esp_err_t device_ret = StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::DEVICE_DATA,&device_dto,sizeof(DeviceDTO),requester,EventId::STO_DEVICESAVED);
                    if (device_ret != ESP_OK) {ESP_LOGE(TAG, "Falha ao enfileirar requisição para Device %d", i);}
                    else {ESP_LOGI(TAG, "Requisição para Device %d enfileirada com sucesso", i);}
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                GlobalConfigDTO config_dto;
                cJSON* cn = cJSON_GetObjectItem(root, "cNome");
                cJSON* tf = cJSON_GetObjectItem(root, "userChoice");
                cJSON* ti = cJSON_GetObjectItem(root, "userTk");
                cJSON* tp = cJSON_GetObjectItem(root, "inpPassTk");
                if (cn) strncpy(config_dto.central_name,cn->valuestring,sizeof(config_dto.central_name)-1);
                if (tf) strncpy(config_dto.token_flag,tf->valuestring,sizeof(config_dto.token_flag)-1);
                if (ti) strncpy(config_dto.token_id,ti->valuestring,sizeof(config_dto.token_id)-1);
                if (tp) strncpy(config_dto.token_password,tp->valuestring,sizeof(config_dto.token_password)-1);
                config_dto.central_name[sizeof(config_dto.central_name)-1]='\0';
                config_dto.token_id[sizeof(config_dto.token_id)-1]='\0';
                config_dto.token_password[sizeof(config_dto.token_password)-1]='\0';
                config_dto.token_flag[sizeof(config_dto.token_flag)-1]='\0';
                RequestSave requester;
                requester.requester=fd;
                requester.resquest_type=RequestTypes::REQUEST_NONE;
                esp_err_t ret = StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::CONFIG_DATA,&config_dto,sizeof(GlobalConfigDTO),requester,EventId::STO_CONFIGSAVED);
                if (ret != ESP_OK) {ESP_LOGE(TAG, "Falha ao enfileirar requisição de CONFIG_DATA");}
                else {ESP_LOGI(TAG, "Requisição CONFIG_DATA enfileirada com sucesso");}
                cJSON_Delete(root);
            }
        }
        else if (strncmp(message.c_str(),"CREDENTIAL",10) == 0) {
            const char* jsonString = message.c_str()+10;
            ESP_LOGI(TAG, "Comando CREDENTIAL recebido para fd=%d", fd);
            cJSON* root = cJSON_Parse(jsonString);
            if (root == nullptr) {ESP_LOGE(TAG, "Falha ao fazer parse do JSON CREDENTIAL. JSON recebido: %s", jsonString); ret = ESP_FAIL;}
            else {
                cJSON* ssid = cJSON_GetObjectItem(root,"nomewifi");
                cJSON* pass = cJSON_GetObjectItem(root,"inpSenha");
                testSSID test;
                test.fd = fd;
                strncpy(test.ssid,ssid->valuestring,sizeof(test.ssid));
                strncpy(test.pass,pass->valuestring,sizeof(test.pass));
                EventBus::post(EventDomain::NETWORK,EventId::NET_TEST,&test,sizeof(test));
                cJSON_Delete(root);
            }
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
    //Handler de eventos do EventDomain::NETWORK
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void* data) {
        EventId evt = static_cast<EventId>(id);
        int data_rec = -1;
        if(data){memcpy(&data_rec,data,sizeof(data));ESP_LOGD(TAG,"onNetworkEvent: fd=%d, evt=%d",data_rec,(int)evt);}
        if (evt == EventId::NET_LISTOK) {
            ESP_LOGI(TAG, "NET_LISTOK recebido para fd=%d", data_rec);
            const char* html_content = StorageManager::scanCache->networks_html_ptr;
            size_t content_len = StorageManager::scanCache->networks_html_len;
            if (content_len > 0) {
                esp_err_t ret = sendToClient(data_rec, html_content);
                if (ret == ESP_OK) {ESP_LOGI(TAG, "Lista WiFi enviada para fd=%d (%zu bytes)", data_rec, content_len);}
                else {ESP_LOGE(TAG, "Falha ao enviar lista para fd=%d: %s", data_rec, esp_err_to_name(ret));}
            } else {
                ESP_LOGW(TAG, "Cache WiFi vazio para fd=%d", data_rec);
                const char* error_msg = "listNet<option value=''>Erro: cache vazio</option>";
                sendToClient(data_rec, error_msg);
            }
        }
        else if (evt == EventId::NET_TESTFAILREVERT) {
            ESP_LOGI(TAG, "NET_TESTFAILREVERT recebido para fd=%d", data_rec);
            const char* message = "errorRevert";
            sendToClient(data_rec,message);
        }
        else if (evt == EventId::NET_TESTFAILTRY) {
            ESP_LOGI(TAG, "NET_LISTOK recebido para fd=%d", data_rec);
            const char* message = "errorTry";
            sendToClient(data_rec,message);
        }
        else if (evt == EventId::NET_APCLICONNECTED) {
            ESP_LOGI(TAG, "Ultima conexão AID=%d", data_rec);
            lastAID = (uint32_t)data_rec;
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
            // EventBus::post(EventDomain::SOCKET, EventId::SOC_STARTED);
            // ESP_LOGI(TAG, "→ SOC_STARTED publicado");
        } else {ESP_LOGE(TAG, "Falha ao registrar WebSocket handler");}
        return ret;
    }
    // Parar o WebSocket
    esp_err_t stop() {
        std::lock_guard<std::mutex> lock(clients_mutex);
        ws_clients.clear();
        ws_server = nullptr;
        ws_registered = false;
        ESP_LOGI(TAG, "WebSocket parado");
        return ESP_OK;
    }
    static void onWebEvent(void*, esp_event_base_t, int32_t id, void* data) {
        ESP_LOGI(TAG, "→ onWebEvent chamado: id=%d", (int)id);
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::WEB_STARTED) {
            ESP_LOGI(TAG, "WEB_STARTED recebido, iniciando WebSocket...");
            httpd_handle_t* server_ptr = (httpd_handle_t*)data;
            if (server_ptr && *server_ptr) {start(*server_ptr);}
            else {ESP_LOGE(TAG, "HTTP server inválido no evento WEB_STARTED");}
        }
        if (evt == EventId::WEB_STOPCLIENT) {
            ESP_LOGI(TAG, "WEB_STOPCLIENT recebido, removendo cliente...");
            int client_fd = -1;
            if(data){memcpy(&client_fd,data,sizeof(int));}
            removeClient(client_fd);
        }
    }
    static void onStorageEvent(void*, esp_event_base_t, int32_t id, void* data) {
        ESP_LOGI(TAG, "→ onStorageEvent chamado: id=%d", (int)id);
        int client_fd = -1;
        EventId evt = static_cast<EventId>(id);
        if(data){memcpy(&client_fd,data,sizeof(int));}
        ESP_LOGI(TAG,"STO_EVENT: fd=%d, evt=%d",client_fd,(int)evt);
        esp_err_t ret = ESP_OK;
        const char* response;
        if (evt == EventId::STO_CONFIGSAVED) {response = "configOk";}
        else if (evt == EventId::STO_CREDENTIALSAVED) {response = "credentialOk";}
        else {return;}
        ret = sendToClient(client_fd,response);
        if (ret != ESP_OK) {ESP_LOGI(TAG,"onStorageEvent RETURN=%s",esp_err_to_name(ret));}
    }
    // Inicialização
    esp_err_t init() {
        ESP_LOGI(TAG, "Inicializando Socket Manager...");
        EventBus::regHandler(EventDomain::WEB, &onWebEvent, nullptr);
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        EventBus::regHandler(EventDomain::STORAGE, &onStorageEvent, nullptr);
        EventBus::post(EventDomain::READY, EventId::SOC_READY);
        ESP_LOGI(TAG, "→ SOC_READY publicado");
        return ESP_OK;
    }
}
