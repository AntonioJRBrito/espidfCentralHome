#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_config.hpp"
#include <string>
#include <sys/stat.h>
#include <vector>
#include <fstream>
#include "esp_err.h"
#include "esp_log.h"
#include "event_bus.hpp"
#include "storage_manager.hpp"
#include "esp_http_server.h"
#include "esp_netif.h"

// Namespace principal do gerenciador Web
namespace WebManager {
    // Inicialização do módulo Web. 
    // Registra handlers para NETWORK e STORAGE e publica WEB_READY.
    esp_err_t init();
}
