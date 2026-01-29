#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "esp_err.h"
#include "mqtt_client.h"
#include "storage_manager.hpp"

namespace MqttManager
{
    esp_err_t init();
    esp_err_t connect();
    void disconnect();
    static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
    esp_err_t publish(const char* data);
    esp_err_t subscribe();
}