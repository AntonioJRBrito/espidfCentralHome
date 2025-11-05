#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "global_config.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "dns_server.h"
#include "lwip/lwip_napt.h"
#include "lwip/netif.h"
#include "lwip/esp_netif_net_stack.h"

namespace NetManager
{
    esp_err_t init();
}