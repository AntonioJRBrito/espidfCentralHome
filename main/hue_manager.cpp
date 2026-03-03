#include "hue_manager.hpp"

namespace HueManager {
    static const char* TAG = "HueManager";
    static TaskHandle_t announcer_task_handle = nullptr;
    static TaskHandle_t listener_task_handle  = nullptr;
    static std::atomic<bool> announcer_running(false);
    static std::atomic<bool> listener_running(false);
    static int announcer_sock = -1;
    static int listener_sock  = -1;
    static void ssdp_announce_task(void* pvParameters)
    {
        announcer_running = true;
        announcer_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if(announcer_sock<0){ESP_LOGE(TAG,"Falha ao criar announcer");announcer_running=false;vTaskDelete(nullptr);return;}
        int reuse = 1;
        setsockopt(announcer_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        uint8_t ttl = 2;
        setsockopt(announcer_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(SSDP_ADDR);
        addr.sin_port = htons(SSDP_PORT);
        ESP_LOGI(TAG, "SSDP announcer iniciado");
        while (announcer_running) {
            const Page* notify_page = StorageManager::getPage("notify");
            if (notify_page && notify_page->data) {
                int ret = sendto(announcer_sock,notify_page->data,notify_page->size,0,(struct sockaddr*)&addr,sizeof(addr));
                if (ret < 0) {ESP_LOGE(TAG, "Erro ao enviar SSDP notify");}
            }
            for (int i = 0; i < 30 && announcer_running; ++i) {vTaskDelay(pdMS_TO_TICKS(1000));}
        }
        if (announcer_sock >= 0) {close(announcer_sock);announcer_sock = -1;}
        ESP_LOGI(TAG, "Announcer finalizado");
        announcer_task_handle = nullptr;
        vTaskDelete(nullptr);
    }
    static void ssdp_listener_task(void* pvParameters)
    {
        listener_running = true;
        listener_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if(listener_sock<0){ESP_LOGE(TAG,"Falha ao criar listener");listener_running=false;vTaskDelete(nullptr);return;}
        int reuse = 1;
        setsockopt(listener_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(SSDP_PORT);
        if (bind(listener_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            ESP_LOGE(TAG, "Falha ao fazer bind na porta 1900");
            close(listener_sock);
            listener_running = false;
            vTaskDelete(nullptr);
            return;
        }
        struct ip_mreq mreq{};
        mreq.imr_multiaddr.s_addr = inet_addr(SSDP_ADDR);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(listener_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            ESP_LOGE(TAG, "Falha ao entrar no grupo multicast");
            close(listener_sock);
            listener_running = false;
            vTaskDelete(nullptr);
            return;
        }
        ESP_LOGI(TAG, "SSDP listener iniciado na porta 1900");
        while (listener_running) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(listener_sock, &readfds);
            struct timeval timeout{};
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            int activity = select(listener_sock + 1,&readfds,nullptr,nullptr,&timeout);
            if (activity > 0 && FD_ISSET(listener_sock, &readfds)) {
                struct sockaddr_in src_addr{};
                socklen_t src_addr_len = sizeof(src_addr);
                char buffer[1024];
                int len = recvfrom(listener_sock,buffer,sizeof(buffer) - 1,0,(struct sockaddr*)&src_addr,&src_addr_len);
                if (len > 0) {
                    buffer[len] = '\0';
                    if(strstr(buffer,"M-SEARCH")&&strstr(buffer,"ssdp:discover")&&(strstr(buffer,"ssdp:all")||strstr(buffer,"upnp:rootdevice"))){
                        vTaskDelay(pdMS_TO_TICKS(esp_random() % 100));
                        const Page* msearch = StorageManager::getPage("msearch");
                        if (msearch && msearch->data) {
                            ESP_LOGW(TAG,"M-SEARCH de %s:%d",inet_ntoa(src_addr.sin_addr),ntohs(src_addr.sin_port));
                            ESP_LOGW(TAG,"buffer:\r\n%s",buffer);
                            int ret = sendto(listener_sock,msearch->data,msearch->size,0,(struct sockaddr*)&src_addr,src_addr_len);
                            if (ret < 0) {ESP_LOGE(TAG,"Erro ao enviar resposta M-SEARCH");}
                            else{ESP_LOGW(TAG,"Resposta:\r\n%s",msearch->data);}
                        }
                        // ESP_LOGW(TAG,"M-SEARCH de %s:%d",inet_ntoa(src_addr.sin_addr),ntohs(src_addr.sin_port));
                        // ESP_LOGW(TAG,"buffer:\r\n%s",buffer);
                        // int ret = sendto(listener_sock,StorageManager::content.c_str(),StorageManager::content.size(),0,(struct sockaddr*)&src_addr,src_addr_len);
                        // if (ret < 0) {ESP_LOGE(TAG,"Erro ao enviar resposta M-SEARCH");}
                        // else{ESP_LOGW(TAG,"Resposta:\r\n%s",StorageManager::content.c_str());}
                    }
                }
            }
        }
        if (listener_sock >= 0) {
            shutdown(listener_sock, SHUT_RDWR);
            close(listener_sock);
            listener_sock = -1;
        }
        ESP_LOGI(TAG, "Listener finalizado");
        listener_task_handle = nullptr;
        vTaskDelete(nullptr);
    }
    void start_ssdp_announcer()
    {
        if (announcer_task_handle != nullptr) {ESP_LOGW(TAG,"Announcer já está rodando");return;}
        xTaskCreatePinnedToCore(ssdp_announce_task,"ssdp_announce",4096,nullptr,5,&announcer_task_handle,1);
        ESP_LOGI(TAG, "SSDP announcer iniciado");
    }
    void start_ssdp_listener()
    {
        if (listener_task_handle != nullptr) {ESP_LOGW(TAG,"Listener já está rodando");return;}
        xTaskCreatePinnedToCore(ssdp_listener_task,"ssdp_listener",4096,nullptr,5,&listener_task_handle,1);
        ESP_LOGI(TAG, "SSDP listener iniciado");
    }
    void stop_ssdp_announcer()
    {
        if (announcer_task_handle == nullptr) {ESP_LOGW(TAG,"Announcer não está rodando");return;}
        announcer_running = false;
        if (announcer_sock >= 0) {shutdown(announcer_sock, SHUT_RDWR);}
        ESP_LOGI(TAG, "Solicitado stop do announcer");
    }

    void stop_ssdp_listener()
    {
        if (listener_task_handle == nullptr) {ESP_LOGW(TAG,"Listener não está rodando");return;}
        listener_running = false;
        if (listener_sock >= 0) {shutdown(listener_sock, SHUT_RDWR);}
        ESP_LOGI(TAG, "Solicitado stop do listener");
    }
    static void onEventNetworkBus(void* handler_args,esp_event_base_t base,int32_t id,void* data)
    {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_STAGOTIP) {
            ESP_LOGI(TAG, "Rede pronta, iniciando SSDP");
            start_ssdp_announcer();
            start_ssdp_listener();
        }
        else if (evt == EventId::NET_STADISCONNECTED) {
            ESP_LOGI(TAG, "Rede desconectada, parando SSDP");
            stop_ssdp_announcer();
            stop_ssdp_listener();
        }
    }
    void init()
    {
        ESP_LOGI(TAG, "Inicializando HUE SSDP...");
        EventBus::regHandler(EventDomain::NETWORK,&onEventNetworkBus,nullptr);
        EventBus::post(EventDomain::READY,EventId::HUE_READY);
        ESP_LOGI(TAG, "→ HUE_READY publicado");
    }
}