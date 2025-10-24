#pragma once
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
enum class EventDomain : uint8_t {NETWORK,RTC,DEVICE,DNS,SOCKET,WEB,UDP,BROKER,MQTT,MATTER,AUTOMATION,OTA,STORAGE,BLE};
enum class EventId : int32_t {
    NETIF_READY = 1,
    NETAP_DISCONNECTED,
    NETAP_CONNECTED,
    NETSTA_DISCONNECTED,
    NETSTA_CONNECTED,
    NETSTA_GOTIP
};
struct EventPayload {void* data; size_t size;};
namespace EventBus {
    esp_err_t init();
    esp_err_t post(EventDomain domain, EventId id, void* data = nullptr, size_t size = 0, TickType_t timeout = 0);
    esp_err_t regHandler(EventDomain domain, esp_event_handler_t handler, void* arg = nullptr);
}