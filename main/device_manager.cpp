
#include "device_manager.hpp"

static const char* TAG = "DeviceManager";

namespace DeviceManager{
    // QRCode
    const int minVersion=1;
    const int maxVersion=40;
    const qrcodegen_Mask mask=qrcodegen_Mask_AUTO;
    const bool boostEcl=true;
    const qrcodegen_Ecc ecl=qrcodegen_Ecc_MEDIUM;
    // buzzer
    #define BUZZER_GPIO      GPIO_NUM_1
    #define BUZZER_ACTIVE    0
    #define BUZZER_INACTIVE  1
    #define BUZZER_MS        100
    static esp_timer_handle_t buzzer_timer=nullptr;
    // display
    #define OLED_I2C_PORT    I2C_NUM_0
    #define OLED_I2C_SDA     GPIO_NUM_17
    #define OLED_I2C_SCL     GPIO_NUM_18
    #define OLED_WIDTH       128
    #define OLED_HEIGHT      64
    #define OLED_ADDR        0x3C
    #define OLED_BUFFER_SIZE 1024
    #define NUM_SCREENS      4
    #define BATCH_MS         200
    static uint8_t* screen_buffers[NUM_SCREENS]={0};
    static uint8_t* screen_send_bufs[NUM_SCREENS]={0};
    static SemaphoreHandle_t buffer_mutex=NULL;
    static SemaphoreHandle_t send_mutex=NULL;
    static QueueHandle_t draw_queue=NULL;
    static StaticQueue_t *draw_queue_struct=NULL;
    static uint8_t *draw_queue_buffer=NULL;
    static size_t draw_queue_capacity=0;
    static const int DEV_XPOS[4]={0,9,32,55};
    static esp_timer_handle_t display_timer=nullptr;
    static const uint32_t DISPLAY_TIMEOUT_MS=30000;
    static const uint8_t* HR_DIGITS[10]={HR_0,HR_1,HR_2,HR_3,HR_4,HR_5,HR_6,HR_7,HR_8,HR_9};
    static bool display_dirty[NUM_SCREENS]={false};
    typedef struct {int command;int status;CurrentTime HM;}DrawCmd;
    static esp_timer_handle_t batch_timer=nullptr;
    static const int FLUSH_CMD=5;
    static const int MARK_CMD=6;
    static const int OFF_CMD=7;
    static const int INIT_CMD=8;
    static const int NOP_CMD=9;
    static bool display_ready=false;
    static CurrentTime t;
    // display handler service
    static int8_t display_page=-1;
    static const uint8_t DISPLAY_PAGE_COUNT=3;
    // devices
    static const gpio_num_t OUTPUT_DEV[4]={GPIO_NUM_41,GPIO_NUM_42,GPIO_NUM_40,GPIO_NUM_39};
    static const touch_pad_t BUTTON_DEV[4]={TOUCH_PAD_NUM7,TOUCH_PAD_NUM6,TOUCH_PAD_NUM5,TOUCH_PAD_NUM4};
    static esp_timer_handle_t timers[4]={nullptr,nullptr,nullptr,nullptr};
    static uint32_t last_press_time[4]={0,0,0,0};
    static const uint32_t DEBOUNCE_MS=200;
    static touch_button_handle_t button_handle[4];
    static QueueHandle_t touch_queue=nullptr;
    static QueueHandle_t storage_event_queue=nullptr;
    static void storage_event_task(void* arg);
    // sensors
    #define DEBOUNCE_SENSOR_MS 200
    static const gpio_num_t INPUT_DEV=GPIO_NUM_2;
    typedef struct{uint8_t level;uint32_t ts_ms;}SensorEvent;
    static QueueHandle_t sensor_queue=NULL;
    static esp_timer_handle_t init_sensor_timer=nullptr;
    // inicializações
    static void init_gpios(){
        for (int i = 0; i < 4; ++i) {
            gpio_reset_pin(OUTPUT_DEV[i]);
            gpio_set_direction(OUTPUT_DEV[i], GPIO_MODE_OUTPUT);
            const Device* device_ptr = StorageManager::getDevice(std::to_string(i));
            if(device_ptr){
                DeviceDTO device_dto;
                memcpy(&device_dto, device_ptr,sizeof(DeviceDTO));
                xQueueSend(storage_event_queue,&i,0);
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
        touch_button_global_config_t button_global_config = TOUCH_BUTTON_GLOBAL_DEFAULT_CONFIG();
        ESP_ERROR_CHECK(touch_button_install(&button_global_config));
        for (int i = 0; i < 4; ++i) {
            touch_button_config_t button_config={.channel_num=BUTTON_DEV[i],.channel_sens=0.5F};
            ESP_ERROR_CHECK(touch_button_create(&button_config,&button_handle[i]));
            ESP_ERROR_CHECK(touch_button_subscribe_event(button_handle[i],TOUCH_ELEM_EVENT_ON_PRESS,(void*)i));
            ESP_ERROR_CHECK(touch_button_set_dispatch_method(button_handle[i],TOUCH_ELEM_DISP_CALLBACK));
            ESP_ERROR_CHECK(touch_button_set_callback(button_handle[i],touch_event_cb));
        }
        ESP_ERROR_CHECK(touch_element_start());
        touch_queue = xQueueCreate(4, sizeof(uint8_t));
        xTaskCreatePinnedToCore(touch_task, "touch_task", 4096, NULL, 4, NULL, tskNO_AFFINITY);
        ESP_LOGI(TAG, "Touch Element inicializado");
    }
    static void init_display(){
        for(int i=0;i<NUM_SCREENS;i++){
            screen_buffers[i]=(uint8_t*)heap_caps_malloc(OLED_BUFFER_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);memset(screen_buffers[i],0x00,OLED_BUFFER_SIZE);
            screen_send_bufs[i]=(uint8_t*)heap_caps_malloc(OLED_BUFFER_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);memset(screen_send_bufs[i],0x00,OLED_BUFFER_SIZE);
        }
        i2c_config_t conf={I2C_MODE_MASTER,OLED_I2C_SDA,OLED_I2C_SCL,GPIO_PULLUP_ENABLE,GPIO_PULLUP_ENABLE,400000,0};
        ESP_ERROR_CHECK(i2c_param_config(OLED_I2C_PORT, &conf));
        ESP_ERROR_CHECK(i2c_driver_install(OLED_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));
        buffer_mutex=xSemaphoreCreateMutex();
        send_mutex=xSemaphoreCreateMutex();
        create_draw_queue_in_psram(128);
        xTaskCreatePinnedToCore(display_event_task,"display_event_task",4096,NULL,5,NULL,tskNO_AFFINITY);
        storage_event_queue=xQueueCreate(8,sizeof(int));
        xTaskCreatePinnedToCore(storage_event_task, "storage_event_task", 4096, nullptr, 3, nullptr, tskNO_AFFINITY);
        const esp_timer_create_args_t bcfg={.callback=&batch_timer_cb,.arg=NULL,.dispatch_method=ESP_TIMER_TASK,.name="display_batch_timer"};
        esp_timer_create(&bcfg,&batch_timer);
        esp_timer_create_args_t tcfg={.callback=display_timer_cb,.arg=nullptr,.dispatch_method=ESP_TIMER_TASK,.name="display_off_timer",};
        esp_timer_create(&tcfg,&display_timer);
        DrawCmd initCmd={.command=INIT_CMD,.status=0};
        xQueueSend(draw_queue,&initCmd,0);
        DrawCmd markCmd={.command=MARK_CMD,.status=0};
        xQueueSend(draw_queue,&markCmd,0);
        display_ready=true;
        ESP_LOGI(TAG, "Display manager inicializado");
    }
    static void init_buzzer(){
        gpio_reset_pin(BUZZER_GPIO);
        gpio_set_direction(BUZZER_GPIO,GPIO_MODE_OUTPUT);
        gpio_set_level(BUZZER_GPIO,BUZZER_INACTIVE);
        const esp_timer_create_args_t btz={.callback=&buzzer_timer_cb,.arg=NULL,.dispatch_method=ESP_TIMER_TASK,.name="buzzer_timer"};
        esp_timer_create(&btz,&buzzer_timer);
        ESP_LOGI(TAG, "Buzzer inicializado");
    }
    static void init_sensor(){
        gpio_reset_pin(INPUT_DEV);
        gpio_set_direction(INPUT_DEV,GPIO_MODE_INPUT);
        gpio_set_pull_mode(INPUT_DEV,GPIO_PULLUP_ONLY);
        gpio_set_intr_type(INPUT_DEV,GPIO_INTR_ANYEDGE);
        esp_err_t isr_res=gpio_install_isr_service(0);
        if(isr_res!=ESP_OK&&isr_res!=ESP_ERR_INVALID_STATE){ESP_LOGW(TAG,"gpio_install_isr_service falhou err=%d",isr_res);}
        gpio_isr_handler_add(INPUT_DEV,sensor_isr,NULL);
        sensor_queue=xQueueCreate(8,sizeof(SensorEvent));
        xTaskCreatePinnedToCore(sensor_task,"sensor_task",3072,NULL,4,NULL,tskNO_AFFINITY);
        const esp_timer_create_args_t init_st_args={.callback=&init_sensor_timer_cb,.arg=NULL,.dispatch_method=ESP_TIMER_TASK,.name="init_sensor_timer"};
        esp_timer_create(&init_st_args,&init_sensor_timer);
        esp_timer_start_once(init_sensor_timer,200*1000ULL);
        ESP_LOGI(TAG, "Sensor GPIO %d configurado (active-low)",(int)INPUT_DEV);
    }
    // qrcode
    static void generate_and_draw_wifi_qr_to_screen_n(uint8_t screen_num){
        std::string payload;
        if (screen_num == 2) {
            const char *ip_ptr = nullptr;
            if (StorageManager::id_cfg) ip_ptr = StorageManager::id_cfg->ip;
            if (ip_ptr && ip_ptr[0] != '\0') {
                payload = std::string("http://") + ip_ptr;
            } else {
                // fallback para SSID se IP não disponível
                const char* central_name_ptr = nullptr;
                if (StorageManager::cfg) central_name_ptr = StorageManager::cfg->central_name;
                if (!central_name_ptr || central_name_ptr[0] == '\0') {
                    ESP_LOGW(TAG, "generate_and_draw_wifi_qr_to_screen_n: IP ausente e SSID vazio; abortando");
                    return;
                }
                auto escape_ssid = [](const std::string &in)->std::string {
                    std::string out; out.reserve(in.size());
                    for (char c : in) {
                        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out.push_back('\\');
                        out.push_back(c);
                    }
                    return out;
                };
                payload = "WIFI:T:nopass;S:" + escape_ssid(std::string(central_name_ptr)) + ";;";
            }
        }else if (screen_num == 1) {
            const char* central_name_ptr = nullptr;
            if (StorageManager::cfg) central_name_ptr = StorageManager::cfg->central_name;
            if (!central_name_ptr || central_name_ptr[0] == '\0') {
                ESP_LOGW(TAG, "generate_and_draw_wifi_qr_to_screen_n: SSID vazio; abortando");
                return;
            }
            auto escape_ssid = [](const std::string &in)->std::string {
                std::string out; out.reserve(in.size());
                for (char c : in) {
                    if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out.push_back('\\');
                    out.push_back(c);
                }
                return out;
            };
            payload = "WIFI:T:nopass;S:" + escape_ssid(std::string(central_name_ptr)) + ";;";
        } else {
            ESP_LOGW(TAG, "generate_and_draw_wifi_qr_to_screen_n: apenas screen 1 (AP) ou 2 (IP) suportadas, recebido=%u", screen_num);
            return;
        }
        ESP_LOGI(TAG,"QRCODE: payload='%s'", payload.c_str());
        const size_t qbuf_len = (size_t)qrcodegen_BUFFER_LEN_FOR_VERSION(maxVersion);
        uint8_t *qrcode = (uint8_t*) heap_caps_malloc(qbuf_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint8_t *temp   = (uint8_t*) heap_caps_malloc(qbuf_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!qrcode || !temp) {
            ESP_LOGE(TAG, "generate_and_draw_wifi_qr_from_storage: malloc buffers qrcode/temp falhou (len=%u)", (unsigned)qbuf_len);
            if (qrcode) heap_caps_free(qrcode);
            if (temp)   heap_caps_free(temp);
            return;
        }
        memset(qrcode,0,qbuf_len);
        memset(temp,0,qbuf_len);
        bool ok = qrcodegen_encodeText(payload.c_str(), temp, qrcode, ecl, minVersion, maxVersion, mask, boostEcl);
        if (!ok) {
            ESP_LOGE(TAG, "qrcodegen_encodeText falhou (payload talvez muito grande)");
            heap_caps_free(qrcode);
            heap_caps_free(temp);
            return;
        }
        const int modules=qrcodegen_getSize(qrcode);
        // const int margin_modules=1;
        const int margin_modules=0;
        const int size_with_margin=modules+2*margin_modules;
        // const int top_band=16+1;
        const int top_band=16;
        const int available_h=63-top_band;
        const int available_w=OLED_WIDTH;
        int scale=std::min(available_w/size_with_margin,available_h/size_with_margin);
        if(scale<1){ESP_LOGE(TAG,"QRCODE ´rea insuficiente para QR (modules=%d need=%dpx each) scale=%d",modules,size_with_margin,scale);return;}
        const int qr_pixel_w=size_with_margin*scale;
        const int qr_pixel_h=size_with_margin*scale;
        const int x0=(OLED_WIDTH-qr_pixel_w)/2;
        const int y0=top_band+((available_h-qr_pixel_h)/2);
        ESP_LOGI(TAG, "QR: modules=%d scale=%d pixels=%dx%d @(%d,%d)",modules,scale,qr_pixel_w,qr_pixel_h,x0,y0);
        const int icon_w=qr_pixel_w;
        const int icon_h=qr_pixel_h;
        const int bytes_per_row=(icon_w+7)/8;
        const size_t buf_bytes=(size_t)bytes_per_row*(size_t)icon_h;
        uint8_t *icon_buf=(uint8_t*)heap_caps_malloc(buf_bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!icon_buf){ESP_LOGE(TAG,"QRCODE: malloc falhou");return;}
        memset(icon_buf,0x00,buf_bytes);
        for(int my=0;my<modules;++my){
            for(int mx=0;mx<modules;++mx){
                if(!qrcodegen_getModule(qrcode,mx,my))continue;
                const int base_x=(margin_modules+mx)*scale;
                const int base_y=(margin_modules+my)*scale;
                for(int ry=0;ry<scale;++ry){
                    int py=base_y+ry;
                    uint8_t *row=icon_buf+((size_t)py * (size_t)bytes_per_row);
                    for (int rx=0;rx<scale;++rx){
                        int px=base_x+rx;
                        int byte_index=px/8;
                        int bit_index=7-(px%8);
                        row[byte_index]|=(uint8_t)(1u<<bit_index);
                    }
                }
            }
        }
        if (xSemaphoreTake(buffer_mutex,pdMS_TO_TICKS(2000))==pdTRUE){
            draw_icon((uint8_t)screen_num,icon_buf,(uint16_t)icon_w,(uint16_t)icon_h,(uint16_t)x0,(uint16_t)y0);
            xSemaphoreGive(buffer_mutex);
            display_dirty[screen_num]=true;
            if(batch_timer){
                esp_timer_stop(batch_timer);
                esp_timer_start_once(batch_timer,(uint64_t)BATCH_MS * 1000ULL);
            }
            ESP_LOGI(TAG, "QRCode desenhado em screen_buffers[%u] @(%d,%d) %dx%d", screen_num, x0, y0, icon_w, icon_h);
        } else {
            ESP_LOGW(TAG, "QRCODE: timeout ao obter buffer_mutex");
        }
        heap_caps_free(icon_buf);
        heap_caps_free(qrcode);
        heap_caps_free(temp);
    }
    // buzzer
    static void buzzer_timer_cb(void*){
        gpio_set_level(BUZZER_GPIO,BUZZER_INACTIVE);
    }
    static void buzzer_beep_nonblocking(uint32_t ms){
        if(!buzzer_timer)return;
        gpio_set_level(BUZZER_GPIO,BUZZER_ACTIVE);
        esp_timer_start_once(buzzer_timer,(uint64_t)ms*1000ULL);
    }
    // sensor
    static void init_sensor_timer_cb(void* arg) {
        (void)arg;
        uint8_t level=(uint8_t)gpio_get_level(INPUT_DEV);
        uint8_t new_x_int=(level==0)?1:0;
        SensorDTO sensor_dto;
        memset(&sensor_dto,0,sizeof(sensor_dto));
        const Sensor* stored=StorageManager::getSensor(std::to_string(4));
        if(stored){
            memcpy(&sensor_dto,stored,sizeof(SensorDTO));
        } else {
            strncpy(sensor_dto.id,"4",sizeof(sensor_dto.id)-1);
            strncpy(sensor_dto.name,"Sensor da Central",sizeof(sensor_dto.name)-1);
            sensor_dto.type=0;
            sensor_dto.time=0;
            sensor_dto.x_int=0;
            sensor_dto.x_str[0]='\0';
        }
        sensor_dto.x_int = new_x_int;
        if (sensor_dto.x_int) {
            strncpy(sensor_dto.x_str, "ON", sizeof(sensor_dto.x_str) - 1);
        } else {
            strncpy(sensor_dto.x_str, "OFF", sizeof(sensor_dto.x_str) - 1);
        }
        RequestSave requester;
        memset(&requester,0,sizeof(requester));
        requester.requester=4;
        requester.request_int=4;
        requester.resquest_type=RequestTypes::REQUEST_INT;
        StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::SENSOR_DATA,&sensor_dto,sizeof(SensorDTO),requester,EventId::STO_SENSORSAVED);
        ESP_LOGI(TAG, "init_sensor_timer_cb: SAVE enfileirado sensor id=4 estado=%d", sensor_dto.x_int);
        if(init_sensor_timer){esp_timer_delete(init_sensor_timer);init_sensor_timer=nullptr;}
    }
    static void IRAM_ATTR sensor_isr(void* arg){
        BaseType_t xHigherPriorityTaskWoken=pdFALSE;
        if(!sensor_queue)return;
        SensorEvent ev;
        ev.level=(uint8_t)gpio_get_level(INPUT_DEV);
        ev.ts_ms=(uint32_t)(esp_timer_get_time()/1000ULL);
        xQueueSendFromISR(sensor_queue,&ev,&xHigherPriorityTaskWoken);
        if(xHigherPriorityTaskWoken){portYIELD_FROM_ISR();}
    }
    static void sensor_task(void* arg) {
        (void)arg;
        SensorEvent ev;
        uint8_t last_level=1;
        uint32_t last_change_ts=0;
        for (;;) {
            if(xQueueReceive(sensor_queue,&ev,portMAX_DELAY)!=pdTRUE){continue;}
            vTaskDelay(pdMS_TO_TICKS(30));
            uint8_t stable_level=(uint8_t)gpio_get_level(INPUT_DEV);
            uint32_t now_ms=(uint32_t)(esp_timer_get_time()/1000ULL);
            if(stable_level==last_level){continue;}
            if((now_ms-last_change_ts)<DEBOUNCE_SENSOR_MS){continue;}
            last_level=stable_level;
            last_change_ts=now_ms;
            SensorDTO sensor_dto;
            memset(&sensor_dto,0,sizeof(sensor_dto));
            bool have_stored=false;
            const Sensor* sensor_ptr=nullptr;
            sensor_ptr=StorageManager::getSensor(std::to_string(4));
            if(sensor_ptr){
                memcpy(&sensor_dto,sensor_ptr,sizeof(SensorDTO));
                have_stored=true;
            }else{
                strncpy(sensor_dto.id,"4",sizeof(sensor_dto.id)-1);
                strncpy(sensor_dto.name,"Sensor da Central",sizeof(sensor_dto.name)-1);
                sensor_dto.type=0;
                sensor_dto.time=0;
                sensor_dto.x_int=0;
                sensor_dto.x_str[0]='\0';
            }
            uint8_t new_x_int=(stable_level==0)?1:0;
            if(have_stored&&sensor_dto.x_int==new_x_int){
                continue;
            }
            sensor_dto.x_int=new_x_int;
            RequestSave requester;
            requester.requester=4;
            requester.request_int=4;
            requester.resquest_type=RequestTypes::REQUEST_INT;
            StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::SENSOR_DATA,&sensor_dto,sizeof(SensorDTO),requester,EventId::STO_SENSORSAVED);
            ESP_LOGI(TAG, "sensor_task: SAVE enfileirado para sensor id=4");
        }
    }
    // devices
    static void handlerDev(uint8_t dev_id){
        const Device* device_ptr = StorageManager::getDevice(std::to_string(dev_id));
        EventBus::post(EventDomain::NETWORK, EventId::NET_RTCDEVREQUEST);
        if (device_ptr) {
            DeviceDTO device_dto;
            memcpy(&device_dto, device_ptr, sizeof(DeviceDTO));
            if(device_dto.type==1){device_dto.status=1-device_dto.status;}else{device_dto.status=1;}
            RequestSave requester;
            requester.requester=dev_id;
            requester.request_int=dev_id;
            requester.resquest_type=RequestTypes::REQUEST_INT;
            StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::DEVICE_DATA,&device_dto,sizeof(DeviceDTO),requester,EventId::STO_DEVICESAVED);
        }
    }
    static void handlerService(){
        EventBus::post(EventDomain::NETWORK, EventId::NET_RTCDEVREQUEST);
        display_page=(int8_t)((display_page+1)%DISPLAY_PAGE_COUNT);
        ESP_LOGI(TAG,"handlerService: page %d selecionada",display_page);
        if(display_page>=0&&display_page<NUM_SCREENS){draw_buffer_send_safe((uint8_t)display_page);}
    }
    static void touch_event_cb(touch_button_handle_t handle, touch_button_message_t *msg, void *arg){
        if (msg->event != TOUCH_BUTTON_EVT_ON_PRESS) return;
        int dev = (int)arg;
        int64_t now_ms = esp_timer_get_time() / 1000;
        if ((now_ms - last_press_time[dev]) >= (int64_t)DEBOUNCE_MS) {
            last_press_time[dev] = now_ms;
            uint8_t d = dev;
            xQueueSendFromISR(touch_queue,&d,NULL);
            ESP_LOGI(TAG, "Touch detected - device %d", dev);
        }
    }
    static void touch_task(void*){
        uint8_t dev;
        for(;;){if(xQueueReceive(touch_queue,&dev,portMAX_DELAY)){
            if(dev == 0){buzzer_beep_nonblocking(BUZZER_MS);handlerService();}
            else{buzzer_beep_nonblocking(BUZZER_MS);handlerDev(dev);}}
        }
    }
    static void timer_callback(void* arg){
        int dev_id = *(int*)arg;
        free(arg);
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
    // display
    static const uint8_t* get_char_bitmap(char c)
    {
        switch (c) {
            case 'A': return A_;
            case 'D': return D_;
            case 'E': return E_;
            case 'F': return F_;
            case 'I': return I_;
            case 'P': return P_;
            case 'V': return V_;
            case 'W': return W_;
            default: return nullptr;
        }
    }
    static void create_draw_queue_in_psram(size_t queue_length){
        const size_t item_size=sizeof(DrawCmd);
        const size_t storage_bytes=queue_length*item_size;
        draw_queue_buffer=(uint8_t*)heap_caps_malloc(storage_bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        draw_queue_struct=(StaticQueue_t*)heap_caps_malloc(sizeof(StaticQueue_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        draw_queue=xQueueCreateStatic((UBaseType_t)queue_length,(UBaseType_t)item_size,draw_queue_buffer,draw_queue_struct);
        draw_queue_capacity=queue_length;
        ESP_LOGI(TAG,"create_draw_queue_in_psram: queue created in PSRAM len=%u",(unsigned)queue_length);
    }
    static void write_cmd(uint8_t cmd) {
        i2c_cmd_handle_t i2c_cmd=i2c_cmd_link_create();
        i2c_master_start(i2c_cmd);
        i2c_master_write_byte(i2c_cmd,(OLED_ADDR<<1)|I2C_MASTER_WRITE,true);
        i2c_master_write_byte(i2c_cmd,0x00,true);
        i2c_master_write_byte(i2c_cmd,cmd,true);
        i2c_master_stop(i2c_cmd);
        esp_err_t ret=i2c_master_cmd_begin(OLED_I2C_PORT,i2c_cmd,pdMS_TO_TICKS(200));
        i2c_cmd_link_delete(i2c_cmd);
        if(ret!=ESP_OK){ESP_LOGE(TAG,"write_cmd: falha I2C cmd=0x%02X err=%d",cmd,ret);}
    }
    static void write_data(const uint8_t *data,size_t len){
        if(!data||len==0)return;
        i2c_cmd_handle_t i2c_cmd=i2c_cmd_link_create();
        i2c_master_start(i2c_cmd);
        i2c_master_write_byte(i2c_cmd,(OLED_ADDR<<1)|I2C_MASTER_WRITE,true);
        i2c_master_write_byte(i2c_cmd,0x40,true);
        size_t remaining=len;
        const uint8_t *ptr=data;
        const size_t CHUNK_MAX=512;
        while(remaining>0){
            size_t chunk=(remaining>CHUNK_MAX)?CHUNK_MAX:remaining;
            i2c_master_write(i2c_cmd,ptr,chunk,true);
            ptr+=chunk;
            remaining-=chunk;
        }
        i2c_master_stop(i2c_cmd);
        esp_err_t ret=i2c_master_cmd_begin(OLED_I2C_PORT,i2c_cmd,pdMS_TO_TICKS(200));
        i2c_cmd_link_delete(i2c_cmd);
        if(ret!=ESP_OK){ESP_LOGE(TAG,"write_data: falha I2C len=%u err=%d",(unsigned)len,ret);}
    }
    static void draw_buffer_send_safe(uint8_t screen_num){
        if(screen_num>=NUM_SCREENS)return;
        if (xSemaphoreTake(buffer_mutex,pdMS_TO_TICKS(2000))!=pdTRUE){return;}
        memcpy(screen_send_bufs[screen_num],screen_buffers[screen_num],OLED_BUFFER_SIZE);
        xSemaphoreGive(buffer_mutex);
        if(xSemaphoreTake(send_mutex,pdMS_TO_TICKS(5000))!=pdTRUE){return;}
        write_cmd(0xAF);
        const uint8_t pages=(OLED_HEIGHT/8);
        for(uint8_t page=0;page<pages;++page){
            write_cmd(0xB0+page);
            write_cmd(0x00);
            write_cmd(0x10);
            write_data(screen_send_bufs[screen_num]+(page * OLED_WIDTH),OLED_WIDTH);
        }
        xSemaphoreGive(send_mutex);
        if(display_timer){
            esp_timer_stop(display_timer);
            esp_timer_start_once(display_timer,(uint64_t)DISPLAY_TIMEOUT_MS*1000ULL);
        }
        ESP_LOGI(TAG,"draw_buffer_send_safe: screen %u enviada",screen_num);
    }
    static void draw_icon(uint8_t screen_num,const uint8_t *icon,uint16_t icon_w,uint16_t icon_h,uint16_t x0,uint16_t y0){
        if(screen_num>=NUM_SCREENS)return;
        if(!icon)return;
        if(!screen_buffers[screen_num])return;
        uint8_t *buf=screen_buffers[screen_num];
        const uint16_t max_w=OLED_WIDTH;
        const uint16_t max_h=OLED_HEIGHT;
        const uint8_t bytes_per_row=(icon_w+7)/8;
        for (uint16_t iy=0;iy<icon_h;++iy){
            uint16_t y=y0+iy;
            if(y>=max_h)continue;
            uint8_t page=y/8;
            uint8_t bit=y%8;
            uint16_t page_base=page*OLED_WIDTH;
            const uint8_t *icon_row=icon+(iy*bytes_per_row);
            for(uint16_t ix=0;ix<icon_w;++ix){
                uint16_t x=x0+ix;
                if(x>=max_w)continue;
                uint16_t byte_index=ix/8;
                uint8_t  bit_index=7-(ix%8);
                uint8_t pixel=(icon_row[byte_index]>>bit_index)&0x01;
                uint16_t index=page_base+x;
                if(pixel){buf[index]|=(1<<bit);}else{buf[index]&=~(1<<bit);}
            }
        }
    }
    static void enqueue_draw_cmd(int command,int status) {
        DrawCmd d={.command=command,.status=status};
        xQueueSend(draw_queue,&d,pdMS_TO_TICKS(20));
        if(batch_timer){
            esp_timer_stop(batch_timer);
            esp_timer_start_once(batch_timer, (uint64_t)BATCH_MS * 1000ULL);
        }
    }
    static void display_event_task(void *pv) {
        (void)pv;
        DrawCmd cmd;
        for (;;) {
            if(xQueueReceive(draw_queue,&cmd,portMAX_DELAY)!=pdTRUE){continue;}
            uint8_t h1=t.hour/10;
            uint8_t h2=t.hour%10;
            uint8_t m1=t.minute/10;
            uint8_t m2=t.minute%10;
            if(xSemaphoreTake(buffer_mutex,pdMS_TO_TICKS(2000))==pdTRUE){
                for(uint8_t s=0;s<NUM_SCREENS;++s){
                    draw_icon(s,HR_DIGITS[h1],9,15,7,0);
                    draw_icon(s,HR_DIGITS[h2],9,15,19,0);
                    draw_icon(s,HR__,2,15,31,0);
                    draw_icon(s,HR_DIGITS[m1],9,15,36,0);
                    draw_icon(s,HR_DIGITS[m2],9,15,48,0);
                }
                xSemaphoreGive(buffer_mutex);
            }
            switch (cmd.command) {
                case INIT_CMD:{
                    ESP_LOGI(TAG,"display_event_task: INIT_CMD recebido");
                    if(xSemaphoreTake(send_mutex,pdMS_TO_TICKS(5000))==pdTRUE){
                        const size_t ncmds=sizeof(init_cmds)/sizeof(init_cmds[0]);
                        for(size_t i=0;i<ncmds;++i){write_cmd(init_cmds[i]);}
                        xSemaphoreGive(send_mutex);
                    }
                    break;
                }
                case OFF_CMD:{
                    ESP_LOGI(TAG,"display_event_task: OFF_CMD recebido");
                    if(xSemaphoreTake(send_mutex,pdMS_TO_TICKS(2000))==pdTRUE){
                        write_cmd(0xAE);
                        xSemaphoreGive(send_mutex);
                    }
                    break;
                }
                case NOP_CMD:{
                    ESP_LOGI(TAG,"display_event_task: NOP_CMD recebido");
                    if(display_page>0){display_dirty[display_page]=true;}else{display_dirty[0]=true;}
                    if(xSemaphoreTake(send_mutex,pdMS_TO_TICKS(2000))==pdTRUE){
                        esp_timer_stop(batch_timer);
                        esp_timer_start_once(batch_timer,(uint64_t)BATCH_MS*1000ULL);
                        xSemaphoreGive(send_mutex);
                    }
                    break;
                }
                case FLUSH_CMD:{
                    ESP_LOGI(TAG,"display_event_task: FLUSH_CMD flush dirty");
                    for(int s=(int)NUM_SCREENS-1;s>=0;--s){if(display_dirty[s]){draw_buffer_send_safe(s);display_dirty[s]=false;}}
                    esp_timer_stop(display_timer);
                    esp_timer_start_once(display_timer,(uint64_t)DISPLAY_TIMEOUT_MS*1000ULL);
                    break;
                }
                case MARK_CMD:{
                    ESP_LOGI(TAG,"display_event_task: update time command");
                    const uint8_t *bm;
                    bm= get_char_bitmap('W');draw_icon(2,bm,9,15,76,0);
                    bm= get_char_bitmap('I');draw_icon(2,bm,9,15,88,0);
                    bm= get_char_bitmap('F');draw_icon(2,bm,9,15,100,0);
                    bm= get_char_bitmap('I');draw_icon(2,bm,9,15,112,0);
                    bm= get_char_bitmap('A');draw_icon(1,bm,9,15,100,0);
                    bm= get_char_bitmap('P');draw_icon(1,bm,9,15,112,0);
                    bm= get_char_bitmap('D');draw_icon(0,bm,9,15,88,0);
                    bm= get_char_bitmap('E');draw_icon(0,bm,9,15,100,0);
                    bm= get_char_bitmap('V');draw_icon(0,bm,9,15,112,0);
                    break;
                }
                case 1:case 2:case 3:{
                    ESP_LOGI(TAG,"display_event_task: device icon update cmd=%d status=%d", cmd.command, cmd.status);
                    const uint8_t *icon=nullptr;
                    switch (cmd.command){
                        case 1:icon=(cmd.status==1)?DEV1_ON:DEV1_OFF;break;
                        case 2:icon=(cmd.status==1)?DEV2_ON:DEV2_OFF;break;
                        case 3:icon=(cmd.status==1)?DEV3_ON:DEV3_OFF;break;
                    }
                    int x=DEV_XPOS[cmd.command];
                    if(xSemaphoreTake(buffer_mutex,pdMS_TO_TICKS(2000))==pdTRUE){
                        draw_icon(0,icon,15,19,x,28);
                        xSemaphoreGive(buffer_mutex);
                        display_dirty[0]=true;
                        esp_timer_stop(batch_timer);
                        esp_timer_start_once(batch_timer,(uint64_t)BATCH_MS*1000ULL);
                    }
                    break;
                }
                case 4:{
                    ESP_LOGI(TAG,"display_event_task: sensor icon update status=%d", cmd.status);
                    const uint8_t *icon=(cmd.status==1)?SEN_ON:SEN_OFF;
                    if(xSemaphoreTake(buffer_mutex,pdMS_TO_TICKS(2000))==pdTRUE){
                        draw_icon(0,icon,38,19,81,28);
                        xSemaphoreGive(buffer_mutex);
                        display_dirty[0]=true;
                        esp_timer_stop(batch_timer);
                        esp_timer_start_once(batch_timer,(uint64_t)BATCH_MS*1000ULL);
                    }
                    break;
                }
                default: {
                    ESP_LOGW(TAG,"display_event_task: comando desconhecido %d status=%d",cmd.command,cmd.status);
                    break;
                }
            }
        }
    }
    static void batch_timer_cb(void*){
        DrawCmd flush={.command=FLUSH_CMD,.status=0};
        xQueueSend(draw_queue,&flush,0);    
    }
    static void display_timer_cb(void*){
        display_page=-1;
        DrawCmd off={.command=OFF_CMD,.status=0};
        xQueueSend(draw_queue,&off,0);    
    }
    //eventos
    static void storage_event_task(void* arg){
        int devsen_id;
        for(;;){
            if(xQueueReceive(storage_event_queue,&devsen_id,portMAX_DELAY)==pdTRUE){
                if(devsen_id<4){
                    const Device* device_ptr=StorageManager::getDevice(std::to_string(devsen_id));
                    if(device_ptr){
                        DeviceDTO device_dto;
                        memcpy(&device_dto,device_ptr,sizeof(DeviceDTO));
                        if(device_dto.status==0){
                            enqueue_draw_cmd(devsen_id,0);
                            gpio_set_level(OUTPUT_DEV[devsen_id],1);
                            if(timers[devsen_id]){
                                esp_timer_stop(timers[devsen_id]);
                                esp_timer_delete(timers[devsen_id]);
                                timers[devsen_id] = nullptr;
                            }
                        }else{
                            gpio_set_level(OUTPUT_DEV[devsen_id],0);
                            enqueue_draw_cmd(devsen_id,1);
                            uint32_t timeout_ms;
                            if(device_dto.type==2)timeout_ms=100;
                            else if(device_dto.type==3)timeout_ms=device_dto.time*1000;
                            else timeout_ms=0;
                            if (timeout_ms > 0) {
                                int *dev_id_ptr = (int*) malloc(sizeof(int));
                                *dev_id_ptr = devsen_id;
                                esp_timer_create_args_t timer_args={.callback=timer_callback,.arg=dev_id_ptr,.dispatch_method=ESP_TIMER_TASK,.name="device_timer",.skip_unhandled_events=true};
                                esp_timer_create(&timer_args, &timers[devsen_id]);
                                esp_timer_start_once(timers[devsen_id], timeout_ms * 1000);
                            }
                        }
                    }
                }else if(devsen_id==4){
                    const Sensor* sensor_ptr=StorageManager::getSensor(std::to_string(devsen_id));
                    if(sensor_ptr){
                        SensorDTO sensor_dto;
                        memcpy(&sensor_dto,sensor_ptr,sizeof(SensorDTO));
                        enqueue_draw_cmd(devsen_id,static_cast<int>(sensor_dto.x_int));
                    }
                }
            }
        }
    }
    static void onStorageEvent(void*,esp_event_base_t,int32_t id,void* event_data){
        if(!event_data)return;
        EventId ev=static_cast<EventId>(id);
        if(ev==EventId::STO_DEVICESAVED||ev==EventId::STO_SENSORSAVED){
            EventBus::post(EventDomain::NETWORK, EventId::NET_RTCDEVREQUEST);
            RequestSave requester;
            memcpy(&requester,event_data,sizeof(RequestSave));
            if(requester.resquest_type!=RequestTypes::REQUEST_INT)return;
            int32_t rid=requester.request_int;
            if(storage_event_queue){xQueueSend(storage_event_queue,&rid,0);ESP_LOGI(TAG,"Enfileirado id=%d (storage event %d)",rid,id);}
        }
    }
    static void onReadyEvent(void*,esp_event_base_t,int32_t id,void*) {
        if (static_cast<EventId>(id)==EventId::READY_ALL) {
            EventBus::unregHandler(EventDomain::READY, &onReadyEvent);
            init_display();
            init_touch();
            init_gpios();
            init_buzzer();
            init_sensor();
            generate_and_draw_wifi_qr_to_screen_n(1);
            EventBus::post(EventDomain::DEVICE, EventId::DEV_STARTED);
            ESP_LOGI(TAG, "→ DEV_STARTED publicado");
        }
    }
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void* event_data) {
        if (static_cast<EventId>(id) == EventId::NET_RTCDEVSUPLY) {
            t=*reinterpret_cast<CurrentTime*>(event_data);
            DrawCmd d={.command=NOP_CMD,.status=0};
            xQueueSend(draw_queue,&d,pdMS_TO_TICKS(20));
            ESP_LOGI(TAG, "Hora recebida: %02d:%02d", t.hour, t.minute);
        }else if (static_cast<EventId>(id) == EventId::NET_STAGOTIP) {
            ESP_LOGI(TAG, "Event NET_STAGOTIP recebido → montar QR com IP na screen 2");
            generate_and_draw_wifi_qr_to_screen_n(2);
            return;
        }else if (static_cast<EventId>(id) == EventId::NET_APCONNECTED) {
            if(display_ready)generate_and_draw_wifi_qr_to_screen_n(1);
        }
    }
    esp_err_t init(){
        EventBus::regHandler(EventDomain::READY, &onReadyEvent, nullptr);
        EventBus::regHandler(EventDomain::STORAGE, &onStorageEvent, nullptr);
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        EventBus::post(EventDomain::READY, EventId::DEV_READY);
        ESP_LOGI(TAG, "Device enviou DEV_READY.");
        return ESP_OK;
    }
}