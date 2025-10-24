#include "storage_bus.hpp"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"

static const char* TAG = "STORAGE_BUS";
config* StorageBus::configCentral = nullptr;

esp_err_t StorageBus::init(){
    uint8_t mac[6];
    esp_read_mac(mac,ESP_MAC_WIFI_STA);
    char macRaw[18],macShort[13];
    sprintf(macRaw,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    sprintf(macShort,"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    configCentral=static_cast<config*>(heap_caps_malloc(sizeof(config),MALLOC_CAP_SPIRAM));
    if(!configCentral){
        ESP_LOGW(TAG,"PSRAM indisponível, alocando em DRAM");
        configCentral=static_cast<config*>(heap_caps_malloc(sizeof(config),MALLOC_CAP_8BIT));
        if(!configCentral){ESP_LOGE(TAG,"Falha total de alocação");return ESP_ERR_NO_MEM;}
    }
    memset(configCentral,0,sizeof(config));
    snprintf(configCentral->APssid,sizeof(configCentral->APssid),"CTR_%s",macShort);
    ESP_LOGI(TAG,"configCentral->APssid = %s", configCentral->APssid);
    return ESP_OK;
}