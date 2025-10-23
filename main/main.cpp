#include "event_bus.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main(void) {
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI("MAIN", "Teste inicial do EventBus");
    EventBus::init();
    int counter = 1;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        char msg[32];
        snprintf(msg, sizeof(msg), "PING %d", counter++);
        EventBus::post(EventDomain::NETWORK,EventId::NETIF_READY,msg,strlen(msg)+1);
    }
}