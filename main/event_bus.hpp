#pragma once
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include "unordered_map"

enum class EventDomain : uint8_t {NETWORK,RTC,DEVICE,DNS,SOCKET,WEB,UDP,BROKER,MQTT,MATTER,AUTOMATION,OTA,STORAGE,BLE};
enum class EventId : int32_t {
    READY_ALL,
    NET_READY,NET_APDISCONNECTED,NET_APCONNECTED,NET_STADISCONNECTED,NET_STACONNECTED,NET_STASTARTED,NET_STAGOTIP,NET_APCLICONNECTED,NET_APCLIDISCONNECTED,NET_IFOK,
    RTC_READY,
    DEV_READY,
    DNS_READY,
    SOC_READY,
    WEB_READY,
    UDP_READY,
    BRK_READY,
    MQT_READY,
    MTT_READY,
    AUT_READY,
    OTA_READY,
    STO_READY,STO_QUERY,STO_UPDATE_REQ,
    BLE_READY
};
struct EventPayload {void* data; size_t size;};
namespace EventBus {
    esp_err_t init();
    esp_err_t post(EventDomain domain, EventId id, void* data = nullptr, size_t size = 0, TickType_t timeout = 0);
    esp_err_t regHandler(EventDomain domain, esp_event_handler_t handler, void* arg = nullptr);
    esp_err_t unregHandler(EventDomain domain, esp_event_handler_t handler);
}