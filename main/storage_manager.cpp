#include "storage_manager.hpp"
#include "storage.hpp"

static const char* TAG = "StorageManager";
namespace StorageManager {
    static QueueHandle_t s_storage_queue = nullptr;
    static SemaphoreHandle_t s_flash_mutex = nullptr;
    static std::unordered_map<std::string, Page*> pageMap;
    static std::unordered_map<std::string, Device*> deviceMap;
    void registerPage(const char* uri, Page* p) {
        pageMap[uri] = p;
        ESP_LOGI(TAG, "Página registrada: %s (%zu bytes, %s)", uri, p->size, p->mime.c_str());
    }
    const Page* getPage(const char* uri) {
        auto it = pageMap.find(uri);
        return (it != pageMap.end()) ? it->second : nullptr;
    }
    void registerDevice(const std::string& id, Device* device) {
        deviceMap[id] = device;
        ESP_LOGI(TAG, "Dispositivo registrado: ID=%s, Nome=%s, Tipo=%d, Status=%d", id.c_str(), device->name.c_str(), device->type, device->status);
    }
    const Device* getDevice(const std::string& id) {
        auto it = deviceMap.find(id);
        return (it != deviceMap.end()) ? it->second : nullptr;
    }
    size_t getDeviceCount() {
        return deviceMap.size();
    }
    std::vector<std::string> getDeviceIds() {
        std::vector<std::string> ids;
        ids.reserve(deviceMap.size());
        for (const auto& pair : deviceMap) {
            ids.push_back(pair.first);
        }
        return ids;
    }
    void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_IFOK) {
            ESP_LOGI(TAG, "Network IFOK recebido → checar SSID/password armazenados...");
            if(!GlobalConfigData::isBlankOrEmpty(GlobalConfigData::cfg->ssid)){
                EventBus::post(EventDomain::STORAGE, EventId::STO_SSIDOK);
            }
        }
    }
    static void storage_task(void* arg) {
        StorageRequest request;
        ESP_LOGI(TAG, "Storage Task iniciada.");
        while (true) {
            if (xQueueReceive(s_storage_queue, &request, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG,"Requisição recebida: Comando=%d, Tipo=%d, Tamanho=%zu",static_cast<int>(request.command),static_cast<int>(request.type),request.data_len);
                if (xSemaphoreTake(s_flash_mutex, portMAX_DELAY) == pdTRUE) {
                    switch (request.command) {
                        case StorageCommand::SAVE: {
                            ESP_LOGI(TAG, "Processando SAVE para Tipo=%d", static_cast<int>(request.type));
                            switch (request.type) {
                                case StorageStructType::CONFIG_DATA:{
                                    ESP_LOGI(TAG, "Salvando CONFIG_DATA");
                                    CentralInfo received;
                                    memset(&received,0,sizeof(GlobalConfig));
                                    memcpy(&received, request.data_ptr, sizeof(CentralInfo));
                                    strncpy(GlobalConfigData::cfg->central_name, received.central_name, sizeof(GlobalConfigData::cfg->central_name) - 1);
                                    GlobalConfigData::cfg->central_name[sizeof(GlobalConfigData::cfg->central_name) - 1] = '\0';
                                    strncpy(GlobalConfigData::cfg->token_id, received.token_id, sizeof(GlobalConfigData::cfg->token_id) - 1);
                                    GlobalConfigData::cfg->token_id[sizeof(GlobalConfigData::cfg->token_id) - 1] = '\0';
                                    strncpy(GlobalConfigData::cfg->token_password, received.token_password, sizeof(GlobalConfigData::cfg->token_password) - 1);
                                    GlobalConfigData::cfg->token_password[sizeof(GlobalConfigData::cfg->token_password) - 1] = '\0';
                                    strncpy(GlobalConfigData::cfg->token_flag, received.token_flag, sizeof(GlobalConfigData::cfg->token_flag) - 1);
                                    GlobalConfigData::cfg->token_flag[sizeof(GlobalConfigData::cfg->token_flag) - 1] = '\0';
                                    esp_err_t ret=Storage::saveGlobalConfigFile();
                                    if (ret!=ESP_OK){ESP_LOGE(TAG, "Erro ao salvar CONFIG_DATA");}
                                    else{ESP_LOGI(TAG, "CONFIG_DATA Salvo");}
                                    if (request.response_event_id != EventId::NONE) {EventBus::post(EventDomain::STORAGE, request.response_event_id, nullptr, 0);}
                                    break;
                                }
                                case StorageStructType::CREDENTIAL_DATA:{
                                    ESP_LOGI(TAG, "Salvando CONFIG_DATA");
                                    // Atualaizar variável e salvar na flash
                                    if (request.response_event_id != EventId::NONE) {EventBus::post(EventDomain::STORAGE, request.response_event_id, nullptr, 0);}
                                    break;
                                }
                                case StorageStructType::SENSOR_DATA:{
                                    ESP_LOGI(TAG, "Salvando SENSOR_DATA");
                                    // Ainda não sei o que fazer
                                    break;
                                }
                                case StorageStructType::DEVICE_DATA:{
                                    ESP_LOGI(TAG, "Salvando DEVICE_DATA");
                                    // Ainda não sei o que fazer
                                    break;
                                }
                                case StorageStructType::AUTOMA_DATA:{
                                    ESP_LOGI(TAG, "Salvando AUTOMA_DATA");
                                    // Ainda não sei o que fazer
                                    break;
                                }
                                case StorageStructType::SCHEDULE_DATA:{
                                    ESP_LOGI(TAG, "Salvando SCHEDULE_DATA");
                                    // Ainda não sei o que fazer
                                    break;
                                }
                            }
                            break;
                        }
                        case StorageCommand::READ: {
                            ESP_LOGI(TAG, "Processando READ para Tipo=%d", static_cast<int>(request.type));
                            switch (request.type) {
                                case StorageStructType::CONFIG_DATA:
                                    ESP_LOGI(TAG, "Lendo CONFIG_DATA");
                                    if(request.response_event_id!=EventId::NONE){EventBus::post(EventDomain::STORAGE,request.response_event_id,nullptr,0);}
                                    break;
                                default:
                                    ESP_LOGW(TAG, "Tipo de estrutura READ não tratado: %d", static_cast<int>(request.type));
                                    break;
                            }
                            break;
                        }
                        case StorageCommand::DELETE: {
                            ESP_LOGI(TAG, "Processando DELETE para Tipo=%d", static_cast<int>(request.type));
                            if (request.response_event_id != EventId::NONE) {EventBus::post(EventDomain::STORAGE, request.response_event_id, nullptr, 0);}
                            break;
                        }
                    }
                    xSemaphoreGive(s_flash_mutex);
                } else {
                    ESP_LOGE(TAG, "Falha ao adquirir mutex");
                }
                if (request.data_ptr) {
                    heap_caps_free(request.data_ptr);
                    request.data_ptr = nullptr;
                }
            }
        }
    }
    esp_err_t enqueueRequest(StorageCommand cmd, StorageStructType type, const void* data_to_copy, size_t data_len, int client_fd, EventId response_event_id) {
        void* data_buffer = nullptr;
        if (data_to_copy && data_len > 0) {
            data_buffer = heap_caps_malloc(data_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (data_buffer == nullptr) {
                ESP_LOGE(TAG, "Falha ao alocar %zu bytes na PSRAM para requisição!", data_len);
                return ESP_ERR_NO_MEM;
            }
            memcpy(data_buffer, data_to_copy, data_len);
        }
        StorageRequest request={.command=cmd,.type=type,.data_ptr=data_buffer,.data_len=data_len,.client_fd=client_fd,.response_event_id=response_event_id};
        if (xQueueSend(s_storage_queue, &request, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Falha ao enviar requisição para a fila de armazenamento!");
            if (data_buffer) {heap_caps_free(data_buffer);}
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "Requisição enfileirada: Comando=%d, Tipo=%d", static_cast<int>(cmd), static_cast<int>(type));
        return ESP_OK;
    }
    esp_err_t init() {
        ESP_LOGI(TAG, "Inicializando Storage Manager...");
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        s_storage_queue = xQueueCreate(10, sizeof(StorageRequest));
        if(s_storage_queue == nullptr){ESP_LOGE(TAG,"Falha ao criar fila de armazenamento!");return ESP_FAIL;}
        ESP_LOGI(TAG, "Fila de armazenamento criada.");
        s_flash_mutex = xSemaphoreCreateMutex();
        if(s_flash_mutex == nullptr){ESP_LOGE(TAG,"Falha ao criar mutex da flash!");vQueueDelete(s_storage_queue);return ESP_FAIL;}
        ESP_LOGI(TAG, "Mutex da flash criado.");
        ESP_LOGI(TAG, "Montando Storage físico (LittleFS)...");
        esp_err_t ret = Storage::init();
        if(ret != ESP_OK){ESP_LOGE(TAG,"Falha ao inicializar Storage físico");return ret;}
        BaseType_t xReturned = xTaskCreate(storage_task,"storage_task",4096,nullptr,5,nullptr );
        if (xReturned != pdPASS) {
            ESP_LOGE(TAG, "Falha ao criar task de armazenamento!");
            vQueueDelete(s_storage_queue);
            vSemaphoreDelete(s_flash_mutex);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Task de armazenamento criada.");
        EventBus::post(EventDomain::READY, EventId::STO_READY);
        ESP_LOGI(TAG, "→ STO_READY publicado");
        return ESP_OK;
    }
}