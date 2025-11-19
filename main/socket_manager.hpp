#pragma once
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "event_bus.hpp"
#include <vector>
#include <mutex>
#include <cstring>
#include <algorithm>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_eth.h"
#include <string>
#include "cJSON.h"
#include "storage_manager.hpp"

namespace SocketManager {
    esp_err_t init();
    esp_err_t start(httpd_handle_t server);
    esp_err_t stop();
    // Envia mensagem para um cliente específico
    esp_err_t sendToClient(int fd, const char* message);
    // Envia mensagem para todos os clientes conectados (broadcast)
    esp_err_t broadcast(const char* message);
    // Retorna número de clientes conectados
    size_t getClientCount();
}