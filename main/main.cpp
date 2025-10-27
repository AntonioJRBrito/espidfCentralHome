#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "storage_bus.hpp"
#include "sync_manager.hpp"
#include "net_manager.hpp"
#include "dns_manager.hpp"
#include "storage_manager.hpp"
#include "socket_manager.hpp"

extern "C" void app_main(void) {
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI("MAIN", "Teste inicial do EventBus");
    StorageBus::init();
    EventBus::init();
    SyncManager::init();
    NetManager::init();
    DnsManager::init();
    StorageManager::init();
    SocketManager::init();
    // int counter = 1;
    // while (true) {
    //     vTaskDelay(pdMS_TO_TICKS(2000));
    //     char msg[32];
    //     snprintf(msg, sizeof(msg), "PING %d", counter++);
    //     EventBus::post(EventDomain::NETWORK,EventId::NET_IFREADY,msg,strlen(msg)+1);
    // }
}