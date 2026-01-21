#include "automation_manager.hpp"

static const char* TAG = "AutomationManager";
namespace AutomationManager {
    // variáveis da agenda
    static Schedule schedule_now;
    static esp_timer_handle_t schedule_timer = nullptr;
    // Disparos dos devices
    static void executeAction(const char* device_id,uint8_t action,uint8_t inform){
        std::string device = device_id;
        const Device* device_ptr = StorageManager::getDevice(device);
        if(device_ptr){
            uint8_t new_action=action;
            if(device_ptr->type==1&&inform==0){new_action=1-action;}
            if(device_ptr->type>1&&inform==0){return;}
            if(device=="1"||device=="2"||device=="3"){
                ESP_LOGI(TAG,"Execute modo %u vai colocar %s em %u",inform,device_id,new_action);
                DeviceDTO device_dto;
                memcpy(&device_dto,device_ptr,sizeof(DeviceDTO));
                device_dto.status=new_action;
                RequestSave requester;
                requester.requester = std::stoi(device);
                requester.request_int = std::stoi(device);
                requester.resquest_type=RequestTypes::REQUEST_INT;
                StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::DEVICE_DATA,&device_dto,sizeof(DeviceDTO),requester,EventId::STO_DEVICESAVED);
            }else{
                ESP_LOGI(TAG,"Execute modo %u vai colocar %s em %u",inform,device_id,new_action);
                std::string payload = "ACT:"+std::to_string(new_action);
                PublishBrokerData* pub_data = (PublishBrokerData*)malloc(sizeof(PublishBrokerData));
                strcpy(pub_data->device_id,device_id);
                strcpy(pub_data->payload,payload.c_str());
                EventBus::post(EventDomain::BROKER,EventId::BRK_PUBLISHREQUEST,pub_data,sizeof(PublishBrokerData));
            }
        }
    }
    // Automation
    static void automation_task(void* pvParameters) {
        AutomationTaskParams* params = (AutomationTaskParams*)pvParameters;
        ESP_LOGI(TAG,"sizeof(SensorEventData)=%zu, offset inform=%zu", sizeof(AutomationTaskParams), offsetof(AutomationTaskParams, inform));
        if(params->inform!=0&&params->inform!=1){ESP_LOGW(TAG,"Inform inválido:%u",params->inform);free(params);vTaskDelete(NULL);return;}
        std::string sensor_id_str(params->sensor_id);
        const std::vector<DeviceAction>* actions = StorageManager::getAutomationBySensor(sensor_id_str);
        if(!actions){ESP_LOGW(TAG,"Nenhuma automação encontrada para sensor:%s inform:%u",params->sensor_id,params->inform);free(params);vTaskDelete(NULL);return;}
        for (const auto& action : *actions) {executeAction(action.device_id,action.action,params->inform);}
        free(params);
        vTaskDelete(NULL);
    }
    // Schedule
    static void scheduleTimerCallback(void* arg) {
        ESP_LOGI(TAG, "Timer de agendamento disparou → executando devices");
        for(const auto& [device_id, action]:schedule_now.devices_to_trigger){executeAction(device_id.c_str(),action,2);}
        schedule_now.devices_to_trigger.clear();
        ESP_LOGI(TAG, "Solicitando próximo agendamento");
        EventBus::post(EventDomain::NETWORK, EventId::NET_RTCSCHREQUEST);
    }
    static uint32_t evaluateSchedule(CurrentTime* ct) {
        if(!ct||!StorageManager::schedule_json_psram){ESP_LOGW(TAG,"Agenda não carregada ou ct nulo");return 0;}
        int cur_dow = ct->dayOfWeek;
        int cur_min = ct->hour * 60 + ct->minute;
        int cur_sec = ct->second;
        uint32_t best = UINT32_MAX;
        int best_dow = -1;
        uint16_t best_mod = 0;
        schedule_now.devices_to_trigger.clear();
        cJSON *root = cJSON_Parse(StorageManager::schedule_json_psram);
        if(!root){ESP_LOGE(TAG,"JSON da agenda inválido");return 0;}
        cJSON *entry = nullptr;
        cJSON_ArrayForEach(entry, root){
            if (!cJSON_IsObject(entry)) continue;
            cJSON *dow_obj = cJSON_GetObjectItem(entry, "dow");
            cJSON *mod_obj = cJSON_GetObjectItem(entry, "mod");
            if (!dow_obj || !mod_obj) continue;
            uint8_t dow_mask = (uint8_t)dow_obj->valueint;
            uint16_t mod = (uint16_t)mod_obj->valueint;
            for (int dow = 0; dow < 7; dow++) {
                if (!(dow_mask & (1 << dow))) continue;
                int days_ahead = (dow - cur_dow + 7) % 7;
                if (days_ahead == 0 && mod <= cur_min) {days_ahead = 7;}
                uint32_t delta_sec = days_ahead * 86400 + (mod - cur_min) * 60 - cur_sec;
                if (delta_sec < 1) delta_sec = 1;
                if (delta_sec < best) {best = delta_sec;best_dow = dow;best_mod = mod;}
            }
        }
        cJSON_ArrayForEach(entry,root){
            if (!cJSON_IsObject(entry)) continue;
            cJSON *dow_obj = cJSON_GetObjectItem(entry, "dow");
            cJSON *mod_obj = cJSON_GetObjectItem(entry, "mod");
            cJSON *id_obj = cJSON_GetObjectItem(entry, "id");
            cJSON *action_obj = cJSON_GetObjectItem(entry, "action");
            if (!dow_obj || !mod_obj || !id_obj || !action_obj) continue;
            uint8_t dow_mask = (uint8_t)dow_obj->valueint;
            uint16_t mod = (uint16_t)mod_obj->valueint;
            const char *device_id = id_obj->valuestring;
            int8_t action = (int8_t)action_obj->valueint;
            if ((dow_mask & (1 << best_dow)) && mod == best_mod) {
                schedule_now.devices_to_trigger[device_id] = action;
                ESP_LOGI(TAG, "Device agendado: %s, action=%d", device_id, action);
            }
        }
        cJSON_Delete(root);
        if (best == UINT32_MAX) {ESP_LOGW(TAG,"Nenhum evento agendado");return 0;}
        ESP_LOGI(TAG, "Próximo evento em %u segundos, %zu devices", best, schedule_now.devices_to_trigger.size());
        return best;
    }
    static void checkSchedule_task(void* pvParameters) {
        CurrentTime* ct = (CurrentTime*)pvParameters;
        ESP_LOGI(TAG,"Task checkSchedule iniciada: %02d:%02d", ct->hour, ct->minute);
        uint32_t seconds_until_next = evaluateSchedule(ct);
        if(seconds_until_next>0){
            if(schedule_timer){esp_timer_stop(schedule_timer);esp_timer_delete(schedule_timer);schedule_timer=nullptr;}
            esp_timer_create_args_t timer_args={.callback=scheduleTimerCallback,.arg=nullptr,.dispatch_method=ESP_TIMER_TASK,.name="schedule_timer"};
            esp_timer_create(&timer_args, &schedule_timer);
            esp_timer_start_once(schedule_timer, seconds_until_next * 1000000);
            ESP_LOGI(TAG,"Timer agendado para %u segundos", seconds_until_next);
        }else{ESP_LOGW(TAG,"Nenhum agendamento válido encontrado");}
        free(ct);
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
    // Evento Schedule
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void* event_data) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_RTCSYNCED) {
            ESP_LOGI(TAG, "RTC sincronizado → solicitando hora atual");
            EventBus::post(EventDomain::NETWORK, EventId::NET_RTCSCHREQUEST);
        }
        if (evt == EventId::NET_RTCSCHSUPLY) {
            if (event_data) {
                CheckScheduleParams* params = (CheckScheduleParams*)malloc(sizeof(CheckScheduleParams));
                memcpy(&params->ct, event_data, sizeof(CurrentTime));
                xTaskCreatePinnedToCore(checkSchedule_task,"checkSchedule_task",4096,params,5,NULL,1);
            }
        }
    }
    // Init
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Automation...");
        EventBus::post(EventDomain::READY, EventId::AUT_READY);
        EventBus::regHandler(EventDomain::AUTOMATION,&onEventAutomationBus,nullptr);
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        ESP_LOGI(TAG, "→ AUT_READY publicado");
        return ESP_OK;
    }
}