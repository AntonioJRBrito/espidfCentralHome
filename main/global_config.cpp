#include "global_config.hpp"

static const char* TAG = "GlobalConfig";
GlobalConfig* GlobalConfigData::cfg = nullptr;
namespace GlobalConfigData{
    bool isBlankOrEmpty(const std::string& str) {
        if (str.empty()) return true;
        return str.find_first_not_of(" \t\n\r\f\v") == std::string::npos;
    }
    std::string trim(const std::string& str) {
        if (str.empty()) return "";
        size_t start = str.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\n\r\f\v");
        return str.substr(start, end - start + 1);
    }
    esp_err_t init() {
        cfg = (GlobalConfig*)heap_caps_calloc(1, sizeof(GlobalConfig), MALLOC_CAP_SPIRAM);
        if (!cfg) return ESP_ERR_NO_MEM;
        cfg->wifi_cache.last_scan = 0;
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
    bool isWifiCacheValid() {
        if (!cfg || cfg->wifi_cache.networks_html.empty()) {return false;}
        if (cfg->wifi_cache.last_scan == 0) {return false;}
        if (cfg->wifi_cache.networks_html.empty()) {return false;}
        time_t now = time(nullptr);
        time_t elapsed = now - cfg->wifi_cache.last_scan;
        if (elapsed > 300) {return false;}
        return true;
    }
    void updateWifiCache(const std::string& html_options) {
        if (!cfg) return;
        cfg->wifi_cache.networks_html = html_options;
        cfg->wifi_cache.last_scan = time(nullptr);
    }
    const std::string& getWifiCache() {
        static const std::string empty;
        if (!isWifiCacheValid()) {
            return empty;
        }
        return cfg->wifi_cache.networks_html;
    }
    void invalidateWifiCache() {
        if (!cfg) return;
        cfg->wifi_cache.networks_html.clear();
        cfg->wifi_cache.last_scan = 0;
    }
    void updateIp(const std::string& ip) {
        // cfg->ip = ip;
    }
    void updateName(const std::string& name) {
        // cfg->central_name = name;
    }
    std::string replacePlaceholders(const std::string& content,const std::string& search,const std::string& replace) {
        std::string result = content;
        size_t pos = 0;
        while ((pos = result.find(search, pos)) != std::string::npos) {
            result.replace(pos, search.length(), replace);
            pos += replace.length();
        }
        return result;
    }
}