
#include "device_manager.hpp"

static const char* TAG = "DeviceManager";

namespace DeviceManager {
    static const gpio_num_t OUTPUT_DEV[4] = {GPIO_NUM_41,GPIO_NUM_42,GPIO_NUM_40,GPIO_NUM_39};
    static const touch_pad_t BUTTON_DEV[4] = {TOUCH_PAD_NUM4,TOUCH_PAD_NUM5,TOUCH_PAD_NUM6,TOUCH_PAD_NUM7};
    static esp_timer_handle_t timers[4] = {nullptr,nullptr,nullptr,nullptr};
    static uint32_t last_press_time[4] = {0,0,0,0};
    static const uint32_t DEBOUNCE_MS = 200;
    static touch_button_handle_t button_handle[4];
    static void timer_callback(void* arg){
        int dev_id = *(int*)arg;
        free(arg);
        ESP_LOGI(TAG, "Timer concluído para device %d", dev_id);
        const Device* device_ptr = StorageManager::getDevice(std::to_string(dev_id));
        if(device_ptr){
            DeviceDTO device_dto;
            memcpy(&device_dto, device_ptr, sizeof(DeviceDTO));
            device_dto.status=0;
            RequestSave requester;
            requester.requester=dev_id;
            requester.request_int=dev_id;
            requester.resquest_type=RequestTypes::REQUEST_INT;
            StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::DEVICE_DATA,&device_dto,sizeof(DeviceDTO),requester,EventId::STO_DEVICESAVED);
        }
    }
    void turnOn(uint8_t dev_id, uint32_t timeout_ms){
        if (dev_id <= 0) return;
        gpio_set_level(OUTPUT_DEV[dev_id], 0);
        ESP_LOGI(TAG, "Device %d ligado", dev_id);
        if (timeout_ms > 0) {
            int *dev_id_ptr = (int*) malloc(sizeof(int));
            *dev_id_ptr = dev_id;
            esp_timer_create_args_t timer_args={.callback=timer_callback,.arg=dev_id_ptr,.dispatch_method=ESP_TIMER_TASK,.name="device_timer",.skip_unhandled_events=true};
            esp_timer_create(&timer_args, &timers[dev_id]);
            esp_timer_start_once(timers[dev_id], timeout_ms * 1000);
            ESP_LOGI(TAG, "Timer de %lums iniciado para device %d", timeout_ms, dev_id);
        }
    }
    void turnOff(uint8_t dev_id){
        if (dev_id <= 0) return;
        gpio_set_level(OUTPUT_DEV[dev_id], 1);
        if (timers[dev_id]) {
            esp_timer_stop(timers[dev_id]);
            esp_timer_delete(timers[dev_id]);
            timers[dev_id] = nullptr;
        }
        ESP_LOGI(TAG, "Device %d desligado", dev_id);
    }
    static void init_gpios(){
        for (int i = 0; i < 4; ++i) {
            gpio_reset_pin(OUTPUT_DEV[i]);
            gpio_set_direction(OUTPUT_DEV[i], GPIO_MODE_OUTPUT);
            const Device* device_ptr = StorageManager::getDevice(std::to_string(i));
            if (device_ptr) {
                DeviceDTO device_dto;
                memcpy(&device_dto, device_ptr, sizeof(DeviceDTO));
                if(device_dto.status==0){gpio_set_level(OUTPUT_DEV[i],1);}else{gpio_set_level(OUTPUT_DEV[i],0);}
            }
        }
        ESP_LOGI(TAG, "GPIOs de saída inicializadas");
    }
    static void init_touch(){
        touch_pad_init();
        for (int i = 0; i < 4; ++i){touch_pad_config(BUTTON_DEV[i]);}
        touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
        touch_pad_fsm_start();
        touch_elem_global_config_t global_config = TOUCH_ELEM_GLOBAL_DEFAULT_CONFIG();
        ESP_ERROR_CHECK(touch_element_install(&global_config));
        ESP_LOGI(TAG, "Touch Element instalado");
        touch_button_global_config_t button_global_config = TOUCH_BUTTON_GLOBAL_DEFAULT_CONFIG();
        ESP_ERROR_CHECK(touch_button_install(&button_global_config));
        ESP_LOGI(TAG, "Touch Button instalado");
        for (int i = 0; i < 4; ++i) {
            touch_button_config_t button_config={.channel_num=BUTTON_DEV[i],.channel_sens=0.5F};
            ESP_ERROR_CHECK(touch_button_create(&button_config,&button_handle[i]));
            ESP_ERROR_CHECK(touch_button_subscribe_event(button_handle[i],TOUCH_ELEM_EVENT_ON_PRESS,(void*)i));
            ESP_ERROR_CHECK(touch_button_set_dispatch_method(button_handle[i],TOUCH_ELEM_DISP_CALLBACK));
            ESP_ERROR_CHECK(touch_button_set_callback(button_handle[i],touch_event_cb));
        }
        ESP_ERROR_CHECK(touch_element_start());
        ESP_LOGI(TAG, "Touch Element inicializado");
    }
    static void touch_event_cb(touch_button_handle_t handle, touch_button_message_t *msg, void *arg){
        ESP_LOGI(TAG, "DEBUG - Callback acionado: evento=%d", msg->event);
        int dev=(int)arg;
        if (msg->event == TOUCH_BUTTON_EVT_ON_PRESS){
            uint32_t current_time = esp_timer_get_time() / 1000;
            if(current_time-last_press_time[dev]<DEBOUNCE_MS) {return;}
            int64_t now_ms = esp_timer_get_time() / 1000;
            if((now_ms-last_press_time[dev])>=(int64_t)DEBOUNCE_MS){
                last_press_time[dev]=now_ms;
                if(dev==0){handlerService();}else{handlerDev(dev);}
                ESP_LOGI(TAG, "Touch detected - device %d", dev);
            }
        }
    }
    static void handlerDev(uint8_t dev_id){
        const Device* device_ptr = StorageManager::getDevice(std::to_string(dev_id));
        if (device_ptr) {
            DeviceDTO device_dto;
            memcpy(&device_dto, device_ptr, sizeof(DeviceDTO));
            if(device_dto.type==1){device_dto.status=1-device_dto.status;}else{device_dto.status=1;}
            ESP_LOGI(TAG, "Send to request dev[%d]",dev_id);
            RequestSave requester;
            requester.requester=dev_id;
            requester.request_int=dev_id;
            requester.resquest_type=RequestTypes::REQUEST_INT;
            StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::DEVICE_DATA,&device_dto,sizeof(DeviceDTO),requester,EventId::STO_DEVICESAVED);
        }       
    }
    static void handlerService(){
        // aqui vou tratar a questão do display
    }
    static void onStorageEvent(void*, esp_event_base_t, int32_t id, void* event_data){
        if (static_cast<EventId>(id)==EventId::STO_DEVICESAVED) {
            RequestSave requester;
            memcpy(&requester, event_data, sizeof(RequestSave));
            if(requester.resquest_type!=RequestTypes::REQUEST_INT){return;}
            if((requester.request_int<1)|(requester.request_int>3)){return;}
            int dev_id = requester.request_int;
            ESP_LOGI(TAG, "Device on event dev[%d]",dev_id);
            const Device* device_ptr = StorageManager::getDevice(std::to_string(dev_id));
            if (device_ptr) {
                DeviceDTO device_dto;
                memcpy(&device_dto, device_ptr,sizeof(DeviceDTO));
                ESP_LOGI(TAG, "Device to change dev[%d] st[%d]",dev_id,device_dto.status);
                if(device_dto.status==0){turnOff(dev_id);}
                else{
                    switch (device_dto.type)
                    {
                    case 1:
                        turnOn(dev_id,0);
                        break;
                    case 2:
                        turnOn(dev_id,10);
                        break;
                    case 3:
                        turnOn(dev_id,device_dto.time*1000);
                        break;
                    default:
                        break;
                    }
                    ESP_LOGI(TAG, "Device on event dev[%d] st[%d] tp[%d] tm[%d]",dev_id,device_dto.status,device_dto.type,device_dto.time);
                }
            }
        }
    }
    static void onReadyEvent(void*, esp_event_base_t, int32_t id, void*) {
        if (static_cast<EventId>(id)==EventId::READY_ALL) {
            EventBus::unregHandler(EventDomain::READY, &onReadyEvent);
            ESP_LOGI(TAG, "READY_ALL → iniciando Device");
            init_gpios();
            init_touch();
            ESP_LOGI(TAG, "→ DEV_STARTED publicado");
        }
    }
    esp_err_t init(){
        ESP_LOGI(TAG, "Pré-inicializando Device Manager...");
        EventBus::regHandler(EventDomain::READY, &onReadyEvent, nullptr);
        EventBus::regHandler(EventDomain::STORAGE, &onStorageEvent, nullptr);
        EventBus::post(EventDomain::READY, EventId::DEV_READY);
        ESP_LOGI(TAG, "Aguardando storage estar ready.");
        return ESP_OK;
    }
}