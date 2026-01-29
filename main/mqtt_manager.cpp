#include "mqtt_manager.hpp"

static const char* TAG = "MqttManager";
namespace MqttManager {
    static esp_mqtt_client_config_t mqtt_cfg = {
        .broker={.address={.uri="mqtt://server.ia.srv.br:1883",},},
        .credentials={.username="igramosquitto",.authentication={.password="mosquittopass",},},
    };
    static esp_mqtt_client_handle_t mqtt_client = nullptr;
    static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
        esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);
        switch (event->event_id) {
            case MQTT_EVENT_CONNECTED:
                ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
                EventBus::post(EventDomain::MQTT, EventId::MQT_CONNECTED);
                break;
            case MQTT_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
                EventBus::post(EventDomain::MQTT, EventId::MQT_DISCONNECTED);
                break;
            case MQTT_EVENT_ERROR:
                ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
                EventBus::post(EventDomain::MQTT, EventId::MQT_ERROR);
                break;
            case MQTT_EVENT_DATA:
            {
                if(event->data_len >= 4){
                    if(strncmp(event->data, "PAS:", 4) == 0) {
                        ESP_LOGI(TAG, "Comando MQTT PAS recebido");
                        char temp[256];
                        int len = (event->data_len < sizeof(temp) - 1) ? event->data_len : sizeof(temp) - 1;
                        strncpy(temp, event->data, len);
                        temp[len] = '\0';
                        std::string mqt_mess = temp;
                        std::vector<std::string> parts = StorageManager::splitString(mqt_mess, ':');
                        if (parts.size() < 4) {ESP_LOGW(TAG, "Formato PAS inválido: recebido %d partes", parts.size());publish("PAS:0");return;}
                        ESP_LOGD(TAG, "Token: '%s', Senha: '%s'", parts[1].c_str(), parts[2].c_str());
                        if(StorageManager::isPassValid(parts[2])){publish("PAS:1");ESP_LOGI(TAG,"Login bem-sucedido");}
                        else{publish("PAS:0");ESP_LOGW(TAG,"Login falhou: senha incorreta");}
                    }else if(strncmp(event->data, "QRY:", 4) == 0) {
                        ESP_LOGI(TAG, "Comando MQTT QRY recebido");
                        std::string result = "QRY:" + StorageManager::buildJSONDevices();
                        publish(result.c_str());
                    }else if((strncmp(event->data,"INT:",4)==0)||(strncmp(event->data,"CMD:",4)==0)||(strncmp(event->data,"BRG:",4)==0)){
                        char temp[50];
                        int len = (event->data_len < sizeof(temp) - 1) ? event->data_len : sizeof(temp) - 1;
                        strncpy(temp, event->data, len);
                        temp[len] = '\0';
                        std::string message = temp;
                        ESP_LOGI(TAG, "Comando INT/CMD/BRG: %s", temp);
                        StorageManager::actDeviceByPage(temp);
                    }else{
                        ESP_LOGI(TAG, "Comando desconhecido: %s", event->data);
                    }
                }
                break;
            }
            default:
                ESP_LOGD(TAG, "Evento MQTT não tratado: %d", event->event_id);
                break;
        }
    }
    esp_err_t connect() {
        if (mqtt_client != nullptr) {ESP_LOGW(TAG, "Cliente MQTT já existe");return ESP_ERR_INVALID_STATE;}
        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        if (mqtt_client == nullptr) {ESP_LOGE(TAG, "Falha ao inicializar cliente MQTT");return ESP_FAIL;}
        esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, nullptr);
        esp_err_t err = esp_mqtt_client_start(mqtt_client);
        if (err != ESP_OK) {ESP_LOGE(TAG, "Falha ao iniciar cliente MQTT: %s", esp_err_to_name(err));return err;}
        ESP_LOGI(TAG, "Cliente MQTT iniciado");
        return ESP_OK;
    }
    void disconnect() {
        if (mqtt_client == nullptr) {ESP_LOGW(TAG, "Cliente MQTT não inicializado");return;}
        esp_err_t err = esp_mqtt_client_stop(mqtt_client);
        if (err != ESP_OK) {ESP_LOGE(TAG, "Falha ao parar cliente MQTT: %s", esp_err_to_name(err));return;}
        err = esp_mqtt_client_destroy(mqtt_client);
        if (err != ESP_OK) {ESP_LOGE(TAG, "Falha ao destruir cliente MQTT: %s", esp_err_to_name(err));return;}
        mqtt_client = nullptr;
        ESP_LOGI(TAG, "Cliente MQTT desconectado e destruído");
    }
    esp_err_t publish(const char* data) {
        if (mqtt_client == nullptr) {ESP_LOGW(TAG, "Cliente MQTT não inicializado");return ESP_ERR_INVALID_STATE;}
        char topic[32];
        snprintf(topic, sizeof(topic), "SRV/%s", StorageManager::id_cfg->id);
        int msg_id = esp_mqtt_client_publish(mqtt_client,topic,data,0,0,0);
        if (msg_id < 0) {ESP_LOGE(TAG, "Falha ao publicar em %s", topic);return ESP_FAIL;}
        ESP_LOGI(TAG, "Publicado em %s (msg_id=%d)", topic, msg_id);
        return ESP_OK;
    }
    esp_err_t subscribe() {
        if (mqtt_client == nullptr) {ESP_LOGW(TAG, "Cliente MQTT não inicializado");return ESP_ERR_INVALID_STATE;}
        char topic[32];
        snprintf(topic, sizeof(topic), "CTR/%s", StorageManager::id_cfg->id);
        int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic, 0);
        if (msg_id < 0) {ESP_LOGE(TAG, "Falha ao subscrever em %s", topic);return ESP_FAIL;}
        ESP_LOGI(TAG, "Subscrito em %s (msg_id=%d)", topic, msg_id);
        return ESP_OK;
    }
    static void onEventNetworkBus(void* handler_args, esp_event_base_t base, int32_t id, void* data) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_STAGOTIP) {
            ESP_LOGI(TAG, "Rede pronta, iniciando conexão MQTT");
            connect();
            vTaskDelay(pdMS_TO_TICKS(3000));
            subscribe();
        }
    }
    static void onStorageEvent(void*, esp_event_base_t, int32_t id, void* data) {
        EventId evt = static_cast<EventId>(id);
        if (evt==EventId::STO_DEVICESAVED){
            std::string json = StorageManager::buildJSONDevice(data);
            if(!json.empty()){
                std::string result = "UPD:"+json;
                publish(result.c_str());
            }
        }
    }
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando MQTT...");
        EventBus::regHandler(EventDomain::NETWORK, &onEventNetworkBus, nullptr);
        EventBus::regHandler(EventDomain::STORAGE, &onStorageEvent, nullptr);
        EventBus::post(EventDomain::READY, EventId::MQT_READY);
        ESP_LOGI(TAG, "→ MQT_READY publicado");
        return ESP_OK;
    }
}