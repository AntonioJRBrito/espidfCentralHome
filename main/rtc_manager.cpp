#include "rtc_manager.hpp"

static const char* TAG = "RtcManager";
#define NTP_SERVER "pool.ntp.org"

namespace RtcManager {
    static bool s_time_synced = false;
    void time_sync_notification_cb(struct timeval *tv){
        vTaskDelay(pdMS_TO_TICKS(200));
        ESP_LOGI(TAG,"Notificação de sincronização de tempo: Hora atual %s",get_current_time_str().c_str());
        CurrentTime currentTime = get_current_time_struct();
        s_time_synced = true;
        EventBus::post(EventDomain::NETWORK, EventId::NET_RTCSYNCED,&currentTime,sizeof(CurrentTime));
        EventBus::post(EventDomain::NETWORK, EventId::NET_RTCDEVSUPLY,&currentTime,sizeof(CurrentTime));
    }
    void initialize_sntp() {
        ESP_LOGI(TAG, "Inicializando SNTP");
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, NTP_SERVER);
        esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
        esp_sntp_init();
    }
    std::string get_current_time_str() {
        time_t now;
        struct tm timeinfo;
        char strftime_buf[64];
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year < (2000 - 1900)) {return "Tempo nao sincronizado";}
        strftime(strftime_buf, sizeof(strftime_buf), "%d/%m/%Y %H:%M:%S", &timeinfo);
        return std::string(strftime_buf);
    }
    void set_manual_time(int year, int month, int day, int hour, int minute, int second) {
        struct tm timeinfo = { 0 };
        timeinfo.tm_year = year - 1900;
        timeinfo.tm_mon = month - 1;
        timeinfo.tm_mday = day;
        timeinfo.tm_hour = hour;
        timeinfo.tm_min = minute;
        timeinfo.tm_sec = second;
        time_t new_time = mktime(&timeinfo);
        struct timeval tv = { .tv_sec = new_time, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        s_time_synced = true;
        EventBus::post(EventDomain::NETWORK, EventId::NET_RTCSYNCED);
        ESP_LOGI(TAG, "Tempo definido manualmente para: %s", get_current_time_str().c_str());
    }
    CurrentTime get_current_time_struct() {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        CurrentTime currentTime;
        currentTime.dayOfWeek = timeinfo.tm_wday;
        currentTime.hour = timeinfo.tm_hour;
        currentTime.minute = timeinfo.tm_min;
        currentTime.second = timeinfo.tm_sec;
        return currentTime;
    }
    static void onEventNetworkBus(void*,esp_event_base_t base,int32_t id,void* data) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_STAGOTIP) {
            initialize_sntp();
        }
        if (evt == EventId::NET_RTCDEVREQUEST) {
            if (s_time_synced){
                CurrentTime currentTime = get_current_time_struct();
                EventBus::post(EventDomain::NETWORK, EventId::NET_RTCDEVSUPLY,&currentTime,sizeof(CurrentTime));
            }
            else {EventBus::post(EventDomain::NETWORK, EventId::NET_RTCNOSYNCED);}
        }
        if (evt == EventId::NET_RTCSCHREQUEST) {
            if (s_time_synced){
                CurrentTime currentTime = get_current_time_struct();
                EventBus::post(EventDomain::NETWORK, EventId::NET_RTCSCHSUPLY,&currentTime,sizeof(CurrentTime));
            }
            else {EventBus::post(EventDomain::NETWORK, EventId::NET_RTCNOSYNCED);}
        }
    }
    void set_timezone(const char* timezone_str) {
        ESP_LOGI(TAG, "Definindo fuso horário para: %s", timezone_str);
        setenv("TZ", timezone_str, 1);
        tzset();
        ESP_LOGI(TAG, "Fuso horário definido. Hora atual: %s", get_current_time_str().c_str());
    }
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Rtc...");
        EventBus::regHandler(EventDomain::NETWORK, &onEventNetworkBus, nullptr);
        RtcManager::set_timezone("<-03>3");
        EventBus::post(EventDomain::READY, EventId::RTC_READY);
        ESP_LOGI(TAG, "→ RTC_READY publicado");
        return ESP_OK;
    }
}