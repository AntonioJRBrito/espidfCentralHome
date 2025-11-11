#include "global_config.hpp"

static const char* TAG = "GlobalConfig";
GlobalConfig* GlobalConfigData::cfg = nullptr;
namespace GlobalConfigData{
    bool isBlankOrEmpty(const char* str) {
        if (!str || *str == '\0') return true;
        for (size_t i = 0; i < strlen(str); ++i){if (!isspace(static_cast<unsigned char>(str[i]))){return false;}}
        return true;
    }
    const char* trim(char* str) {
        if (!str || *str == '\0') return str;
        char* start = str;
        while (*start != '\0' && isspace(static_cast<unsigned char>(*start))) {start++;}
        char* end = start + strlen(start) - 1;
        while (end >= start && isspace(static_cast<unsigned char>(*end))) {end--;}
        *(end + 1) = '\0';
        if (start != str) {memmove(str, start, strlen(start) + 1);}
        return str;
    }
    esp_err_t init() {
        cfg = (GlobalConfig*)heap_caps_calloc(1, sizeof(GlobalConfig), MALLOC_CAP_SPIRAM);
        if (!cfg) {ESP_LOGE(TAG, "Falha ao alocar GlobalConfig na PSRAM");return ESP_ERR_NO_MEM;}
        cfg->wifi_cache.networks_html_ptr = (char*)heap_caps_malloc(MAX_HTML_OPTIONS_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
        if (!cfg->wifi_cache.networks_html_ptr) {
            ESP_LOGE(TAG, "Falha ao alocar %d bytes para cache WiFi na PSRAM", MAX_HTML_OPTIONS_BUFFER_SIZE);
            heap_caps_free(cfg);cfg = nullptr;return ESP_ERR_NO_MEM;
        }
        cfg->wifi_cache.networks_html_ptr[0] = '\0';
        cfg->wifi_cache.networks_html_len = 0;
        cfg->wifi_cache.is_sta_connected = false;
        cfg->wifi_cache.last_scan = 0;
        memset(cfg->mac, 0, sizeof(cfg->mac));
        memset(cfg->id, 0, sizeof(cfg->id));
        memset(cfg->ip, 0, sizeof(cfg->ip));
        memset(cfg->hostname, 0, sizeof(cfg->hostname));
        memset(cfg->ssid, 0, sizeof(cfg->ssid));
        memset(cfg->password, 0, sizeof(cfg->password));
        memset(cfg->central_name, 0, sizeof(cfg->central_name));
        memset(cfg->token_id, 0, sizeof(cfg->token_id));
        memset(cfg->token_password, 0, sizeof(cfg->token_password));
        memset(cfg->token_flag, 0, sizeof(cfg->token_flag));
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(cfg->mac, sizeof(cfg->mac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        snprintf(cfg->id, sizeof(cfg->id), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        snprintf(cfg->hostname, sizeof(cfg->hostname), "CTR_%s", cfg->id);
        strncpy(cfg->token_flag, "0", sizeof(cfg->token_flag) - 1);
        cfg->token_flag[sizeof(cfg->token_flag) - 1] = '\0';
        ESP_LOGI(TAG, "GlobalConfig inicializado na PSRAM.");
        ESP_LOGI(TAG, "MAC: %s, ID: %s, Hostname: %s", cfg->mac, cfg->id, cfg->hostname);
        return ESP_OK;
    }
        bool isWifiCacheValid() {
        if (!GlobalConfigData::cfg || !GlobalConfigData::cfg->wifi_cache.networks_html_ptr) {return false;}
        if (GlobalConfigData::cfg->wifi_cache.networks_html_len == 0 || GlobalConfigData::cfg->wifi_cache.networks_html_ptr[0] == '\0') {return false;}
        time_t now = time(nullptr);
        time_t elapsed = now - GlobalConfigData::cfg->wifi_cache.last_scan;
        if (elapsed > 300) {ESP_LOGD(TAG, "Cache WiFi expirado (%ld segundos)", elapsed);return false;}
        return true;
    }
    void invalidateWifiCache() {
        if (!GlobalConfigData::cfg || !GlobalConfigData::cfg->wifi_cache.networks_html_ptr) return;
        GlobalConfigData::cfg->wifi_cache.networks_html_ptr[0] = '\0';
        GlobalConfigData::cfg->wifi_cache.networks_html_len = 0;
        GlobalConfigData::cfg->wifi_cache.last_scan = 0;
        ESP_LOGI(TAG, "Cache WiFi invalidado (conteúdo limpo).");
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