#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include <string>
#include <vector>
#include <map>
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include <algorithm>
#include "storage_manager.hpp"

#define MAX_MQTT_CLIENT_ID_LEN 23
#define MAX_MQTT_TOPIC_LEN 64
#define MAX_DEVICE_ID_LEN 23

namespace BrokerManager
{
    struct ClientSession {
        int fd;
        char client_id[MAX_MQTT_CLIENT_ID_LEN + 1];
        std::string subscribed_topic;
        ClientSession() : fd(-1) {
            client_id[0] = '\0';
        }
    };
    esp_err_t init();
    void broker_listener_task(void *pvParameters);
    void client_handler_task(void *pvParameters);
    void removeClient(int fd);
    void publish_message_to_device(const std::string& target_device_id, const std::string& payload);
}