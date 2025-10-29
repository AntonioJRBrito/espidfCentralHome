#pragma once
#include "esp_err.h"
#include "event_bus.hpp"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif_ip_addr.h"
#include "lwip/dns.h"

namespace DnsManager {
    esp_err_t init();
}