#include "storage_manager.hpp"
#include "storage.hpp"

static const char* TAG = "StorageManager";

namespace StorageManager {
    // Declaração dos ponteiros globais que serão alocados na PSRAM
    GlobalConfig* cfg = nullptr;
    IDConfig* id_cfg = nullptr;
    CredentialConfig* cd_cfg = nullptr;
    WifiScanCache* scanCache = nullptr;
    char* schedule_json_psram = nullptr;
    // Variáveis estáticas do módulo
    static QueueHandle_t s_storage_queue = nullptr;
    static SemaphoreHandle_t s_flash_mutex = nullptr;
    static std::unordered_map<std::string, Page*> pageMap;
    static std::unordered_map<std::string, Device*>* deviceMap = nullptr;
    static std::unordered_map<std::string, Sensor*>* sensorMap = nullptr;
    std::unordered_map<std::string, Automation*>* automationMap = nullptr;
    // --- Funções de Utilidade ---
    bool isBlankOrEmpty(const char* str) {
        if (str == nullptr || str[0] == '\0') {return true;}
        for (size_t i = 0; str[i] != '\0'; ++i) {if (!isspace(static_cast<unsigned char>(str[i]))) {return false;}}
        return true;
    }
    bool isWifiCacheValid() {
        if (!scanCache || !scanCache->networks_html_ptr) {return false;}
        if (scanCache->networks_html_len == 0 || scanCache->networks_html_ptr[0] == '\0') {return false;}
        time_t now = time(nullptr);
        time_t elapsed = now - scanCache->last_scan;
        if (elapsed > 300) {ESP_LOGD(TAG, "Cache WiFi expirado (%ld segundos)", elapsed);return false;}
        return true;
    }
    void invalidateWifiCache() {
        if (!scanCache || !scanCache->networks_html_ptr) return;
        scanCache->networks_html_ptr[0] = '\0';
        scanCache->networks_html_len = 0;
        scanCache->last_scan = 0;
        ESP_LOGI(TAG, "Cache WiFi invalidado (conteúdo limpo).");
    }
    std::string replacePlaceholders(const std::string& content, const std::string& search, const std::string& replace) {
        std::string result = content;
        size_t pos = 0;
        while((pos=result.find(search,pos))!=std::string::npos){result.replace(pos,search.length(),replace);pos+=replace.length();}
        return result;
    }
    std::vector<std::string> splitString(const std::string& s, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }
    bool isPassValid(const std::string& password){
        if(strcmp(StorageManager::cfg->token_flag,"0")==0){return (password==StorageManager::cd_cfg->password);}
        else if(strcmp(StorageManager::cfg->token_flag,"1")==0){return (password==StorageManager::cfg->token_password);}
        return false;
    }
    // --- Gerenciamento de Páginas Web ---
    void registerPage(const char* uri, Page* p) {
        pageMap[uri] = p;
        ESP_LOGI(TAG, "Página registrada: %s (%zu bytes, %s)", uri, p->size, p->mime.c_str());
    }
    const Page* getPage(const char* uri) {
        auto it = pageMap.find(uri);
        return (it != pageMap.end()) ? it->second : nullptr;
    }
    // --- Gerenciamento de Dispositivos ---
    void registerDevice(Device* device) {
        auto it = deviceMap->find(std::string(device->id));
        if (it != deviceMap->end()) {
            ESP_LOGW(TAG, "Dispositivo '%s' já existe. Liberando memória antiga.",std::string(device->id));
            heap_caps_free(it->second);
        }
        (*deviceMap)[std::string(device->id)] = device;
        ESP_LOGI(TAG, "Disp regist:(ID=%s)(Nome=%s)(Tipo=%d)(Status=%d)(Tempo=%d)(x_int=%d)(x_str=%s)",device->id,device->name,device->type,device->status,device->time,device->x_int,device->x_str);
    }
    // Função pública para obter dispositivo
    const Device* getDevice(const std::string& id) {
        auto it = deviceMap->find(id);
        return (it != deviceMap->end()) ? it->second : nullptr;
    }
    // Função interna para obter dispositivo
    static Device* getMutableDeviceInternal(const std::string& id) {
        auto it = deviceMap->find(id);
        return (it != deviceMap->end()) ? it->second : nullptr;
    }
    size_t getDeviceCount() {
        return deviceMap->size();
    }
    std::vector<std::string> getDeviceIds() {
        std::vector<std::string> ids;
        if (!deviceMap) return ids;
        ids.reserve(deviceMap->size());
        for (const auto& pair : *deviceMap) {ids.push_back(pair.first);}
        return ids;
    }
    std::string buildJSONDevices(){
        ESP_LOGI(TAG, "Construindo lista de dispositivos");
        cJSON *root = cJSON_CreateArray();
        std::vector<std::string> ids = StorageManager::getDeviceIds();
        for (const auto &id : ids) {
            const Device* device = StorageManager::getDevice(id);
            if (!device) continue;
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj, "id", device->id);
            cJSON_AddStringToObject(obj, "name", device->name);
            cJSON_AddNumberToObject(obj, "type", device->type);
            cJSON_AddNumberToObject(obj, "status", device->status);
            cJSON_AddNumberToObject(obj, "time", device->time);
            cJSON_AddItemToArray(root, obj);
        }
        char *jsonStr = cJSON_PrintUnformatted(root);
        std::string result = "";
        result += jsonStr;
        cJSON_Delete(root);
        free(jsonStr);
        return result;
    }
    std::string buildJSONDevice(const void* data){
        if(!data){return "";}
        RequestSave requester;
        std::string requestId;
        memcpy(&requester,data,sizeof(RequestSave));
        if(requester.resquest_type==RequestTypes::REQUEST_INT){requestId = std::to_string(requester.request_int);}
        if(requester.resquest_type==RequestTypes::REQUEST_CHAR){requestId = requester.request_char;}
        std::string result = "";
        const Device* device = StorageManager::getDevice(requestId);
        if (device){
            cJSON *root = cJSON_CreateArray();
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj,"id",device->id);
            cJSON_AddStringToObject(obj,"name",device->name);
            cJSON_AddNumberToObject(obj,"type",device->type);
            cJSON_AddNumberToObject(obj,"status",device->status);
            cJSON_AddNumberToObject(obj,"time",device->time);
            cJSON_AddItemToArray(root,obj);
            char *jsonStr = cJSON_PrintUnformatted(root);
            result += jsonStr;
            cJSON_Delete(root);
            free(jsonStr);
        }
        return result;
    }
    void actDeviceByPage(std::string message){
        std::vector<std::string> parts = StorageManager::splitString(message,':');
        if(parts.size()==4){
            const Device* device_ptr = StorageManager::getDevice(parts[1]);
            if (device_ptr) {
                DeviceDTO device_dto;
                std::string actDev;
                memcpy(&device_dto, device_ptr, sizeof(DeviceDTO));
                switch (device_dto.type){
                    case 1:device_dto.status=1-device_dto.status;actDev=std::to_string(device_dto.status);break;
                    case 2:case 3:device_dto.status=1;actDev="1";break;
                    case 4:actDev=parts[2];break;
                }
                RequestSave requester;
                if(parts[0]=="INT"){
                    requester.requester = std::stoi(parts[1]);
                    requester.request_int = std::stoi(parts[1]);
                    requester.resquest_type=RequestTypes::REQUEST_INT;
                    StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::DEVICE_DATA,&device_dto,sizeof(DeviceDTO),requester,EventId::STO_DEVICESAVED);
                }else{
                    std::string payload = (parts[0] == "CMD") ? "ACT:" : "BRG:";
                    payload += actDev;
                    PublishBrokerData* pub_data = (PublishBrokerData*)malloc(sizeof(PublishBrokerData));
                    strcpy(pub_data->device_id,parts[1].c_str());
                    strcpy(pub_data->payload,payload.c_str());
                    EventBus::post(EventDomain::BROKER, EventId::BRK_PUBLISHREQUEST, pub_data, sizeof(PublishBrokerData));
                }
            }
        }
    }
    // --- Gerenciamento de Sensores ---
    void registerSensor(Sensor* sensor) {
        auto it = sensorMap->find(std::string(sensor->id));
        if (it != sensorMap->end()) {
            ESP_LOGW(TAG, "Sensor '%s' já existe. Liberando memória antiga.",std::string(sensor->id));
            heap_caps_free(it->second);
        }
        (*sensorMap)[std::string(sensor->id)] = sensor;
        ESP_LOGI(TAG, "Sensor registrado:(ID=%s)(Nome=%s)(Tipo=%d)(tempo=%d)(x_int=%d)(x_str=%s)",sensor->id,sensor->name,sensor->type,sensor->time,sensor->x_int,sensor->x_str);
    }
    // Função pública para obter sensor
    const Sensor* getSensor(const std::string& id) {
        auto it = sensorMap->find(id);
        return (it != sensorMap->end()) ? it->second : nullptr;
    }
    // Função interna para obter sensor
    static Sensor* getMutableSensorInternal(const std::string& id) {
        auto it = sensorMap->find(id);
        return (it != sensorMap->end()) ? it->second : nullptr;
    }
    size_t getSensorCount() {
        return sensorMap->size();
    }
    std::vector<std::string> getSensorIds() {
        std::vector<std::string> ids;
        if (!sensorMap) return ids;
        ids.reserve(sensorMap->size());
        for (const auto& pair : *sensorMap) {ids.push_back(pair.first);}
        return ids;
    }
    // --- Funções de Automação ---
    const std::vector<DeviceAction>* getAutomationBySensor(const std::string& sensor_id) {
        auto it = automationMap->find(sensor_id);
        return (it != automationMap->end()) ? it->second->actions : nullptr;
    }
    // --- Handlers de Eventos ---
    void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_IFOK) {
            ESP_LOGI(TAG, "Network IFOK recebido → checar SSID/password armazenados...");
            if (!isBlankOrEmpty(cd_cfg->ssid)) {
                EventBus::post(EventDomain::STORAGE, EventId::STO_SSIDOK);
            }
        }
    }
    // --- Enfileiramento de Requisições ---
    esp_err_t enqueueRequest(StorageCommand cmd, StorageStructType type, const void* data_to_copy, size_t data_len, RequestSave requester, EventId response_event_id) {
        void* data_buffer = nullptr;
        if (data_to_copy && data_len > 0) {
            data_buffer = heap_caps_malloc(data_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if(data_buffer==nullptr){ESP_LOGE(TAG,"Falha ao alocar %zu bytes na PSRAM para requisição!",data_len);return ESP_ERR_NO_MEM;}
            memcpy(data_buffer, data_to_copy, data_len);
        }
        StorageRequest request;
        request.command = cmd;
        request.type = type;
        request.data_ptr = data_buffer;
        request.data_len = data_len;
        request.requester = requester;
        request.response_event_id = response_event_id;
        if (xQueueSend(s_storage_queue, &request, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Falha ao enviar requisição para a fila de armazenamento!");
            if (data_buffer) {heap_caps_free(data_buffer);}
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "Requisição enfileirada: Comando=%d, Tipo=%d", static_cast<int>(cmd), static_cast<int>(type));
        return ESP_OK;
    }
    // --- Fila de Armazenamento (storage_task) ---
    static void storage_task(void* arg) {
        StorageRequest request;
        ESP_LOGI(TAG, "Storage Task iniciada.");
        while (true) {
            if (xQueueReceive(s_storage_queue, &request, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG,"Requisição:Cmd=%d,Tp=%d,Ln=%zu",static_cast<int>(request.command),static_cast<int>(request.type),request.data_len);
                esp_err_t err = ESP_OK;
                if (xSemaphoreTake(s_flash_mutex, portMAX_DELAY) == pdTRUE) {
                    switch (request.command) {
                        case StorageCommand::SAVE: {
                            ESP_LOGI(TAG, "Processando SAVE para Tipo=%d", static_cast<int>(request.type));
                            switch (request.type) {
                                case StorageStructType::CONFIG_DATA: 
                                {
                                    if (request.data_ptr && request.data_len == sizeof(GlobalConfigDTO)) {
                                        memcpy(cfg, request.data_ptr, sizeof(GlobalConfigDTO));
                                        err=Storage::saveGlobalConfigFile(cfg);
                                        ESP_LOGI(TAG, "GlobalConfig salvo na flash. Status: %s", esp_err_to_name(err));
                                        if(request.response_event_id!=EventId::NONE){
                                            EventBus::post(EventDomain::STORAGE,request.response_event_id,&request.requester,sizeof(request.requester));
                                        }
                                    }else{ESP_LOGE(TAG, "SAVE CONFIG_DATA: Dados inválidos ou tamanho incorreto.");}
                                    break;
                                }
                                case StorageStructType::CREDENTIAL_DATA:
                                {
                                    if (request.data_ptr && request.data_len == sizeof(CredentialConfigDTO)) {
                                        memcpy(cd_cfg, request.data_ptr, sizeof(CredentialConfigDTO));
                                        Storage::saveCredentialConfigFile(cd_cfg);
                                        ESP_LOGI(TAG, "CredentialConfig salvo na flash. Status: %s", esp_err_to_name(err));
                                        if(request.response_event_id!=EventId::NONE){
                                            EventBus::post(EventDomain::STORAGE,request.response_event_id,&request.requester,sizeof(request.requester));
                                        }
                                    }else{ESP_LOGE(TAG, "SAVE CONFIG_DATA: Dados inválidos ou tamanho incorreto.");}
                                    break;
                                }
                                case StorageStructType::DEVICE_DATA: {
                                    if (request.data_ptr && request.data_len == sizeof(DeviceDTO)) {
                                        DeviceDTO* device_dto = static_cast<DeviceDTO*>(request.data_ptr);
                                        std::string device_id_str(device_dto->id);
                                        Device* existing_device = getMutableDeviceInternal(device_id_str);
                                        if (existing_device) {
                                            memcpy(existing_device, device_dto, sizeof(DeviceDTO));
                                            Storage::saveDeviceFile(existing_device);
                                            if (request.response_event_id != EventId::NONE){
                                                EventBus::post(EventDomain::STORAGE,request.response_event_id,&request.requester,sizeof(request.requester));
                                            }
                                            ESP_LOGI(TAG, "Dispositivo '%s' atualizado na PSRAM e salvo na flash. Status: %s", device_id_str.c_str(), esp_err_to_name(err));
                                        } else {
                                            ESP_LOGI(TAG,"SAVE DEVICE_DATA: Disp '%s' não encontrado na PSRAM. Alocando e registrando novo dispositivo.",device_id_str.c_str());
                                            Device* new_device_ptr = (Device*)heap_caps_malloc(sizeof(Device), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                                            if (!new_device_ptr) {
                                                ESP_LOGE(TAG, "SAVE DEVICE_DATA: Falha ao alocar memória para novo dispositivo '%s' na PSRAM.", device_id_str.c_str());
                                            } else {
                                                memcpy(new_device_ptr, device_dto, sizeof(DeviceDTO));
                                                registerDevice(new_device_ptr); 
                                                Storage::saveDeviceFile(new_device_ptr);
                                                if (request.response_event_id != EventId::NONE) {
                                                    EventBus::post(EventDomain::STORAGE,request.response_event_id,&request.requester,sizeof(request.requester));
                                                }
                                                ESP_LOGI(TAG, "Novo disp '%s' alocado na PSRAM, registrado e salvo na flash. Status: %s", device_id_str.c_str(), esp_err_to_name(err));
                                            }
                                        }
                                    }else{ESP_LOGE(TAG, "SAVE DEVICE_DATA: Dados inválidos ou tamanho incorreto.");}
                                    break;
                                }
                                case StorageStructType::SENSOR_DATA: 
                                {
                                    if (request.data_ptr && request.data_len == sizeof(SensorDTO)) {
                                        SensorDTO* sensor_dto = static_cast<SensorDTO*>(request.data_ptr);
                                        std::string sensor_id_str(sensor_dto->id);
                                        Sensor* existing_sensor = getMutableSensorInternal(sensor_id_str);
                                        if (existing_sensor) {
                                            memcpy(existing_sensor, sensor_dto, sizeof(SensorDTO));
                                            Storage::saveSensorFile(existing_sensor);
                                            if (request.response_event_id != EventId::NONE) {
                                                EventBus::post(EventDomain::STORAGE,request.response_event_id,&request.requester,sizeof(request.requester));
                                            }
                                            ESP_LOGI(TAG, "Sensor '%s' atualizado na PSRAM e salvo na flash. Status: %s", sensor_id_str.c_str(), esp_err_to_name(err));
                                        } else {
                                            ESP_LOGI(TAG, "SAVE SENSOR_DATA: Sensor '%s' não encontrado na PSRAM. Alocando e registrando novo sensor.", sensor_id_str.c_str());
                                            Sensor* new_sensor_ptr = (Sensor*)heap_caps_malloc(sizeof(Sensor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                                            if (!new_sensor_ptr) {
                                                ESP_LOGE(TAG, "SAVE SENSOR_DATA: Falha ao alocar memória para novo sensor '%s' na PSRAM.", sensor_id_str.c_str());
                                            } else {
                                                memcpy(new_sensor_ptr, sensor_dto, sizeof(SensorDTO));
                                                registerSensor(new_sensor_ptr); 
                                                Storage::saveSensorFile(new_sensor_ptr);
                                                if (request.response_event_id != EventId::NONE) {
                                                    EventBus::post(EventDomain::STORAGE,request.response_event_id,&request.requester,sizeof(request.requester));
                                                }
                                                ESP_LOGI(TAG,"Novo sensor '%s' alocado na PSRAM, registrado e salvo na flash. Status: %s",sensor_id_str.c_str(),esp_err_to_name(err));
                                            }
                                        }
                                    }else{ESP_LOGE(TAG, "SAVE DEVICE_DATA: Dados inválidos ou tamanho incorreto.");}
                                    break;
                                }
                                case StorageStructType::AUTOMA_DATA: {
                                    ESP_LOGI(TAG, "Salvando AUTOMA_DATA");
                                    if(request.data_ptr && request.data_len > 0){
                                        char* json_str = (char*)malloc(request.data_len + 1);
                                        if (!json_str) {ESP_LOGE(TAG, "Falha ao alocar buffer para JSON");break;}
                                        memcpy(json_str, request.data_ptr, request.data_len);
                                        json_str[request.data_len] = '\0';
                                        Storage::saveAutomation(json_str);
                                        free(json_str);
                                    }else{ESP_LOGE(TAG, "AUTOMA_DATA: dados inválidos");}
                                    break;
                                }
                                case StorageStructType::SCHEDULE_DATA: {
                                    ESP_LOGI(TAG, "Salvando SCHEDULE_DATA");
                                    if(request.data_ptr && request.data_len > 0){
                                        char* json_str = (char*)malloc(request.data_len + 1);
                                        if (!json_str) {ESP_LOGE(TAG, "Falha ao alocar buffer para JSON");break;}
                                        memcpy(json_str, request.data_ptr, request.data_len);
                                        json_str[request.data_len] = '\0';
                                        Storage::saveSchedule(json_str);
                                        free(json_str);
                                        ESP_LOGI(TAG, "Agenda atualizada → reiniciando ciclo de agendamento");
                                        EventBus::post(EventDomain::NETWORK, EventId::NET_RTCSCHREQUEST);
                                    }else{ESP_LOGE(TAG, "SCHEDULE_DATA: dados inválidos");}
                                    break;
                                }
                            }
                            break;
                        }
                        case StorageCommand::DELETE: {
                            ESP_LOGI(TAG, "Processando DELETE para Tipo=%d (TODO)", static_cast<int>(request.type));
                            // TODO: Implementar lógica de DELETE
                            err = ESP_ERR_NOT_SUPPORTED;
                            break;
                        }
                    }
                    xSemaphoreGive(s_flash_mutex);
                } else {
                    ESP_LOGE(TAG, "Falha ao adquirir mutex da flash! Requisição não processada.");
                    // esp_err_t mutex_err = ESP_ERR_TIMEOUT; Não vou fazer nada.
                }
                if (request.data_ptr) {
                    heap_caps_free(request.data_ptr);
                    request.data_ptr = nullptr;
                }
            }
        }
    }
    // --- Inicialização do Módulo ---
    esp_err_t init() {
        ESP_LOGI(TAG, "Inicializando Storage Manager...");
        // 1. Alocação e inicialização dos objetos globais na PSRAM
        cfg = (GlobalConfig*)heap_caps_calloc(1, sizeof(GlobalConfig), MALLOC_CAP_SPIRAM);
        if (!cfg) { ESP_LOGE(TAG, "Falha ao alocar GlobalConfig na PSRAM"); return ESP_ERR_NO_MEM; }
        ESP_LOGI(TAG, "GlobalConfig alocado na PSRAM.");
        id_cfg = (IDConfig*)heap_caps_calloc(1, sizeof(IDConfig), MALLOC_CAP_SPIRAM);
        if (!id_cfg) { heap_caps_free(cfg); ESP_LOGE(TAG, "Falha ao alocar IDConfig na PSRAM"); return ESP_ERR_NO_MEM; }
        ESP_LOGI(TAG, "IDConfig alocado na PSRAM.");
        cd_cfg = (CredentialConfig*)heap_caps_calloc(1, sizeof(CredentialConfig), MALLOC_CAP_SPIRAM);
        if (!cd_cfg) { heap_caps_free(cfg); heap_caps_free(id_cfg); ESP_LOGE(TAG, "Falha ao alocar CredentialConfig na PSRAM"); return ESP_ERR_NO_MEM; }
        ESP_LOGI(TAG, "CredentialConfig alocado na PSRAM.");
        scanCache = (WifiScanCache*)heap_caps_calloc(1, sizeof(WifiScanCache), MALLOC_CAP_SPIRAM);
        if (!scanCache) { heap_caps_free(cfg); heap_caps_free(id_cfg); heap_caps_free(cd_cfg); ESP_LOGE(TAG, "Falha ao alocar WifiScanCache na PSRAM"); return ESP_ERR_NO_MEM; }
        ESP_LOGI(TAG, "WifiScanCache alocado na PSRAM.");
        deviceMap = new std::unordered_map<std::string, Device*>();
        sensorMap = new std::unordered_map<std::string, Sensor*>();
        automationMap = new std::unordered_map<std::string, Automation*>();
        scanCache->networks_html_ptr = (char*)heap_caps_malloc(MAX_HTML_OPTIONS_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
        if (!scanCache->networks_html_ptr) {
            ESP_LOGE(TAG, "Falha ao alocar %d bytes para cache WiFi HTML na PSRAM", MAX_HTML_OPTIONS_BUFFER_SIZE);
            heap_caps_free(cfg); heap_caps_free(id_cfg); heap_caps_free(scanCache); scanCache = nullptr;
            return ESP_ERR_NO_MEM;
        }
        scanCache->networks_html_ptr[0] = '\0';
        ESP_LOGI(TAG, "Buffer HTML para WifiScanCache alocado na PSRAM.");
        // 3. Inicialização de IDConfig com MAC e ID do dispositivo (não persistido)
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(id_cfg->mac, sizeof(id_cfg->mac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        snprintf(id_cfg->id, sizeof(id_cfg->id), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG, "IDConfig inicializado com MAC: %s, ID: %s", id_cfg->mac, id_cfg->id);
        // 4. Montagem do sistema de arquivos (LittleFS)
        ESP_LOGI(TAG, "Montando Storage físico (LittleFS)...");
        esp_err_t ret = Storage::init();
        if (ret != ESP_OK) { ESP_LOGE(TAG, "Falha ao inicializar Storage físico"); return ret; }
        ESP_LOGI(TAG, "Storage físico inicializado.");
        // 5. Criação da fila de requisições
        s_storage_queue = xQueueCreate(10, sizeof(StorageRequest));
        if (s_storage_queue == nullptr) { ESP_LOGE(TAG, "Falha ao criar fila de armazenamento!"); return ESP_FAIL; }
        ESP_LOGI(TAG, "Fila de armazenamento criada.");
        // 6. Criação do mutex para acesso à flash
        s_flash_mutex = xSemaphoreCreateMutex();
        if (s_flash_mutex == nullptr) { ESP_LOGE(TAG, "Falha ao criar mutex da flash!"); vQueueDelete(s_storage_queue); return ESP_FAIL; }
        ESP_LOGI(TAG, "Mutex da flash criado.");
        // 7. Criação da task de armazenamento
        BaseType_t xReturned = xTaskCreate(storage_task, "storage_task", 4096, nullptr, 5, nullptr);
        if (xReturned != pdPASS) {
            ESP_LOGE(TAG, "Falha ao criar task de storage!");
            vQueueDelete(s_storage_queue);
            vSemaphoreDelete(s_flash_mutex);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Task de storage criada.");
        // 8. Registro do handler de eventos de rede
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        ESP_LOGI(TAG, "Handler de eventos de rede registrado.");
        // 9. Posta evento de ready
        EventBus::post(EventDomain::READY, EventId::STO_READY);
        ESP_LOGI(TAG, "→ STO_READY publicado");
        return ESP_OK;
    }
}
