#include "global_config.hpp"

static const char* TAG = "GlobalConfig";
GlobalConfig* GlobalConfigData::cfg = nullptr;
namespace GlobalConfigData{
    esp_err_t init() {
        cfg = (GlobalConfig*)heap_caps_calloc(1, sizeof(GlobalConfig), MALLOC_CAP_SPIRAM);
        if (!cfg) return ESP_ERR_NO_MEM;
        uint8_t mac[6];
        esp_read_mac(mac,ESP_MAC_WIFI_STA);
        char buf[18];
        snprintf(buf,sizeof(buf),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        cfg->mac = buf;
        snprintf(buf,sizeof(buf),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        cfg->id = buf;
        snprintf(buf,sizeof(buf),"CTR_%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        cfg->hostname = buf;
        ESP_LOGI(TAG, "GlobalConfig inicializado");
        return ESP_OK;
    }
    void updateIp(const std::string& ip) {
        // cfg->ip = ip;
    }
    void updateName(const std::string& name) {
        // cfg->central_name = name;
    }
}