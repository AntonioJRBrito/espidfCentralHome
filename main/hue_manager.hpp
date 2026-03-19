#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "event_bus.hpp"
#include "esp_err.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <vector>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/igmp.h"
#include <atomic>
#include <sys/select.h>
#include "storage_manager.hpp"
#include <string.h>
#include <ctype.h>
#include <stdbool.h>


namespace HueManager
{
    // Constantes
    constexpr uint16_t SSDP_PORT = 1900;
    constexpr const char* SSDP_ADDR = "239.255.255.250";
    // Funções públicas
    void init();
    void start_ssdp_announcer();
    void stop_ssdp_announcer();
}