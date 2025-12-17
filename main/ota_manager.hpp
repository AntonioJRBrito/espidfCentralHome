#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include <string>
#include <vector>
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_crc.h"

namespace OtaManager
{
    std::vector<std::string> fetchFirmwareList();
    esp_err_t handleFrmCommand(int fd);
    void downloadFirmwareAsync(const std::string& filename,int client_fd);
    void downloadFirmware(const std::string& filename,int client_fd);
    void factoryAsync(int client_fd);
}