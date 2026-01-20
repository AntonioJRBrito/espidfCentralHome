#include "automation_manager.hpp"

static const char* TAG = "AutomationManager";
namespace AutomationManager {
    // Task para tratar do Automation
    static void automation_task(void* pvParameters) {
        AutomationTaskParams* params = (AutomationTaskParams*)pvParameters;
        ESP_LOGI(TAG,"sizeof(SensorEventData)=%zu, offset inform=%zu", sizeof(AutomationTaskParams), offsetof(AutomationTaskParams, inform));
        if(params->inform!=0&&params->inform!=1){ESP_LOGW(TAG,"Inform inválido:%u",params->inform);free(params);vTaskDelete(NULL);return;}
        std::string sensor_id_str(params->sensor_id);
        const std::vector<DeviceAction>* actions = StorageManager::getAutomationBySensor(sensor_id_str);
        if(!actions){ESP_LOGW(TAG,"Nenhuma automação encontrada para sensor:%s inform:%u",params->sensor_id,params->inform);free(params);vTaskDelete(NULL);return;}
        for (const auto& action : *actions) {
            std::string device = action.device_id;
            const Device* device_ptr = StorageManager::getDevice(device);
            if(device_ptr){
                uint8_t new_action=action.action;
                if(device_ptr->type==1&&params->inform==0){new_action=1-action.action;}
                if(device_ptr->type>1&&params->inform==0){continue;
                }else if(device=="1"||device=="2"||device=="3"){
                    ESP_LOGI(TAG,"Automação vai colocar %s em %u",action.device_id,new_action);
                    DeviceDTO device_dto;
                    memcpy(&device_dto,device_ptr,sizeof(DeviceDTO));
                    device_dto.status=new_action;
                    RequestSave requester;
                    requester.requester = std::stoi(device);
                    requester.request_int = std::stoi(device);
                    requester.resquest_type=RequestTypes::REQUEST_INT;
                    StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::DEVICE_DATA,&device_dto,sizeof(DeviceDTO),requester,EventId::STO_DEVICESAVED);
                }else{
                    ESP_LOGI(TAG,"Automação vai colocar %s em %u",action.device_id,new_action);
                    std::string payload = "ACT:"+std::to_string(new_action);
                    PublishBrokerData* pub_data = (PublishBrokerData*)malloc(sizeof(PublishBrokerData));
                    strcpy(pub_data->device_id,action.device_id);
                    strcpy(pub_data->payload,payload.c_str());
                    EventBus::post(EventDomain::BROKER,EventId::BRK_PUBLISHREQUEST,pub_data,sizeof(PublishBrokerData));
                }
            }
        }
        free(params);
        vTaskDelete(NULL);
    }
    // Evento Automation  
    static void onEventAutomationBus(void*,esp_event_base_t base,int32_t id,void* data) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::AUT_DETECTSENSOR) {
            ESP_LOGI(TAG, "Evento AUT_DETECTSENSOR recebido. Criando tarefa do Automation.");
            AutomationTaskParams* params = (AutomationTaskParams*)malloc(sizeof(AutomationTaskParams));
            memcpy(params, data, sizeof(AutomationTaskParams));
            ESP_LOGI(TAG, "Handler recebido, sensor_id=%s inform=%u",params->sensor_id,params->inform);
            xTaskCreatePinnedToCore(automation_task,"automation_task",4096,params,5,NULL,1);
        }
    }
    // Init
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Automation...");
        EventBus::post(EventDomain::READY, EventId::AUT_READY);
        EventBus::regHandler(EventDomain::AUTOMATION,&onEventAutomationBus,nullptr);
        ESP_LOGI(TAG, "→ AUT_READY publicado");
        return ESP_OK;
    }
}