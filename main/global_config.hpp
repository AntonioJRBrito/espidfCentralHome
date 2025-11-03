#pragma once
#include "esp_err.h"
#include <string>
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_log.h"

struct GlobalConfig {
    std::string mac;
    std::string id;
    std::string ip;
    std::string hostname;
    std::string ssid;
    std::string password;
    std::string central_name;
    std::string token_id;
    std::string token_password;
    std::string token_flag;
};
namespace GlobalConfigData {
    extern GlobalConfig* cfg;
    esp_err_t init();
    void updateIp(const std::string& ip);
    void updateName(const std::string& name);
    std::string replacePlaceholders(const std::string& content,const std::string& search,const std::string& replace);
    bool isBlankOrEmpty(const std::string& str);
    std::string trim(const std::string& str);
}