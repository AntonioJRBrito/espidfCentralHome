#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "storage.hpp"
#include "sync_manager.hpp"
#include "net_manager.hpp"
#include "dns_manager.hpp"
#include "storage_manager.hpp"
#include "socket_manager.hpp"
#include "web_manager.hpp"
#include "udp_manager.hpp"
#include "broker_manager.hpp"
#include "mqtt_manager.hpp"
#include "matter_manager.hpp"
#include "automation_manager.hpp"
#include "ota_manager.hpp"
#include "device_manager.hpp"
#include "ble_manager.hpp"
#include "rtc_manager.hpp"

extern "C" void app_main(void) {
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI("MAIN", "Teste inicial do EventBus");
    EventBus::init();
    SyncManager::init();
    NetManager::init();
    DnsManager::init();
    StorageManager::init();
    SocketManager::init();
    WebManager::init();
    UdpManager::init();
    BrokerManager::init();
    MqttManager::init();
    MatterManager::init();
    AutomationManager::init();
    OtaManager::init();
    DeviceManager::init();
    BleManager::init();
    RtcManager::init();
    // int counter = 1;
    // while (true) {
    //     vTaskDelay(pdMS_TO_TICKS(2000));
    //     char msg[32];
    //     snprintf(msg, sizeof(msg), "PING %d", counter++);
    //     EventBus::post(EventDomain::NETWORK,EventId::NET_IFREADY,msg,strlen(msg)+1);
    // }
}