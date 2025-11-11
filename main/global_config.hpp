#pragma once
#include "esp_err.h"
#include <cstring>
#include <string>
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_system.h"
#include <ctime>
#include <algorithm>
#include <cctype>

// Definindo tamanhos máximos para as strings
#define MAX_MAC_LEN            18
#define MAX_ID_LEN             13
#define MAX_IP_LEN             16
#define MAX_HOSTNAME_LEN       25
#define MAX_SSIDWAN_LEN        33
#define MAX_PASSWORD_LEN       65
#define MAX_CENTRAL_NAME_LEN   33
#define MAX_TOKEN_ID_LEN       33
#define MAX_TOKEN_PASSWORD_LEN 65
#define MAX_TOKEN_FLAG_LEN      2
#define MAX_HTML_OPTIONS_BUFFER_SIZE 8192

struct WifiScanCache {
    char* networks_html_ptr;
    size_t networks_html_len;
    time_t last_scan;
    bool is_sta_connected;
};
struct GlobalConfig {
    char mac[MAX_MAC_LEN];
    char id[MAX_ID_LEN];
    char ip[MAX_IP_LEN];
    char hostname[MAX_HOSTNAME_LEN];
    char ssid[MAX_SSIDWAN_LEN];
    char password[MAX_PASSWORD_LEN];
    char central_name[MAX_CENTRAL_NAME_LEN];
    char token_id[MAX_TOKEN_ID_LEN];
    char token_password[MAX_TOKEN_PASSWORD_LEN];
    char token_flag[MAX_TOKEN_FLAG_LEN];
    WifiScanCache wifi_cache;
};
namespace GlobalConfigData {
    extern GlobalConfig* cfg;
    esp_err_t init();
    std::string replacePlaceholders(const std::string& content,const std::string& search,const std::string& replace);
    bool isBlankOrEmpty(const char* str);
    const char* trim(char* str);
    bool isWifiCacheValid();
    void invalidateWifiCache();
}