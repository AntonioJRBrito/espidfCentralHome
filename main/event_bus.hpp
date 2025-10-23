#pragma once
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
enum class EventDomain : uint8_t {NETWORK,RTC,DEVICE,DNS,SOCKET,WEB,UDP,BROKER,MQTT,MATTER,AUTOMATION,OTA,STORAGE,BLE};
enum class EventId : int32_t {
    NETIF_READY = 1,
    NETAP_DISCONNECTED = 2,
    NETAP_CONNECTED = 3,
    NETSTA_DISCONNECTED = 4,
    NETSTA_CONNECTED = 5,
    NETSTA_GOTIP = 6
};
struct EventPayload {void* data; size_t size;};
namespace EventBus {
    esp_err_t init();
    esp_err_t post(EventDomain domain, EventId id, void* data = nullptr, size_t size = 0, TickType_t timeout = 0);
    esp_err_t regHandler(EventDomain domain, esp_event_handler_t handler, void* arg = nullptr);
}