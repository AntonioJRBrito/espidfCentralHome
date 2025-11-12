#pragma once
#include "esp_err.h"
#include "esp_log.h"
#include "event_bus.hpp"
#include <unordered_map>
#include <string>
#include <vector>
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "global_config.hpp"

struct Page {
    void* data;
    size_t size;
    std::string mime;
};
struct Device {
    std::string id;
    std::string name;
    uint8_t type;
    uint16_t time;
    uint8_t status;
    std::string x_str;
    uint8_t x_int;
};
enum class StorageCommand {SAVE,READ,DELETE};
enum class StorageStructType {CONFIG_DATA,CREDENTIAL_DATA,SENSOR_DATA,DEVICE_DATA,AUTOMA_DATA,SCHEDULE_DATA};
struct StorageRequest {
    StorageCommand command;
    StorageStructType type;
    void* data_ptr;
    size_t data_len;
    int client_fd;
    EventId response_event_id;
};
namespace StorageManager {
    esp_err_t init();
    // page
    void registerPage(const char* uri, Page* page);
    const Page* getPage(const char* uri);
    // device
    void registerDevice(const std::string& id, Device* device);
    const Device* getDevice(const std::string& id);
    size_t getDeviceCount();
    std::vector<std::string> getDeviceIds();
    // handlers
    void onNetworkEvent(void*, esp_event_base_t, int32_t, void*);
    void onStorageEvent(void*, esp_event_base_t, int32_t, void*);
    // storage
    esp_err_t enqueueRequest(StorageCommand cmd,StorageStructType type,const void* data_to_copy,size_t data_len,int client_fd=-1,EventId response_event_id=EventId::NONE);
}