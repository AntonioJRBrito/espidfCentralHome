#include "event_bus.hpp"
#include "unordered_map"

ESP_EVENT_DEFINE_BASE(NETWORK_BASE);
ESP_EVENT_DEFINE_BASE(RTC_BASE);
ESP_EVENT_DEFINE_BASE(DEVICE_BASE);
ESP_EVENT_DEFINE_BASE(DNS_BASE);
ESP_EVENT_DEFINE_BASE(SOCKET_BASE);
ESP_EVENT_DEFINE_BASE(WEB_BASE);
ESP_EVENT_DEFINE_BASE(UDP_BASE);
ESP_EVENT_DEFINE_BASE(BROKER_BASE);
ESP_EVENT_DEFINE_BASE(MQTT_BASE);
ESP_EVENT_DEFINE_BASE(MATTER_BASE);
ESP_EVENT_DEFINE_BASE(AUTOMATION_BASE);
ESP_EVENT_DEFINE_BASE(OTA_BASE);
ESP_EVENT_DEFINE_BASE(STORAGE_BASE);
ESP_EVENT_DEFINE_BASE(BLE_BASE);
static const char* TAG = "EVENT_BUS";
// Loops internos
static esp_event_loop_handle_t loopPriority = nullptr;
static esp_event_loop_handle_t loopFast = nullptr;
static esp_event_loop_handle_t loopMedium = nullptr;
static esp_event_loop_handle_t loopSlow = nullptr;
// Struct domain
struct DomainInfo {esp_event_base_t base; esp_event_loop_handle_t loop;};
static std::unordered_map<EventDomain, DomainInfo> domainMap;
// Inicialização e registro
esp_err_t EventBus::init() {
    esp_event_loop_args_t args = {
        .queue_size = 8,
        .task_name = nullptr,
        .task_priority = 8,
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY
    };
    //loop_priority
    esp_err_t ret = esp_event_loop_create(&args,&loopPriority);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Loop priority criado");
    //loop_fast
    args.task_core_id = 0;
    args.task_priority = 5;
    ret = esp_event_loop_create(&args, &loopFast);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Loop fast criado");
    //loop_medium
    args.task_core_id = 1;
    args.task_priority = 3;
    ret = esp_event_loop_create(&args, &loopMedium);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Loop medium criado");
    //loop_slow
    args.task_priority = 1;
    ret = esp_event_loop_create(&args, &loopSlow);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Loop slow criado");
    domainMap[EventDomain::NETWORK] = {NETWORK_BASE,loopPriority};
    domainMap[EventDomain::RTC] = {RTC_BASE,loopPriority};
    domainMap[EventDomain::DEVICE] = {DEVICE_BASE,loopFast};
    domainMap[EventDomain::DNS] = {DNS_BASE,loopFast};
    domainMap[EventDomain::SOCKET] = {SOCKET_BASE,loopMedium};
    domainMap[EventDomain::WEB] = {WEB_BASE,loopMedium};
    domainMap[EventDomain::UDP] = {UDP_BASE,loopMedium};
    domainMap[EventDomain::BROKER] = {BROKER_BASE,loopMedium};
    domainMap[EventDomain::MQTT] = {MQTT_BASE,loopMedium};
    domainMap[EventDomain::MATTER] = {MATTER_BASE,loopMedium};
    domainMap[EventDomain::AUTOMATION] = {AUTOMATION_BASE,loopMedium};
    domainMap[EventDomain::OTA] = {OTA_BASE,loopMedium};
    domainMap[EventDomain::STORAGE] = {STORAGE_BASE,loopSlow};
    domainMap[EventDomain::BLE] = {BLE_BASE,loopMedium};
    return ESP_OK;
}
// Post event
esp_err_t EventBus::post(EventDomain domain, EventId id, void* data, size_t size, TickType_t timeout){
    ESP_LOGI(TAG, "post domain=%d id=%d data=%s size=%u", (int)domain, (int)id, data, (unsigned)size);
    auto &info = domainMap.at(domain);
    return esp_event_post_to(info.loop, info.base, static_cast<int32_t>(id), data, size, timeout);
}
// Handler event
esp_err_t EventBus::regHandler(EventDomain domain, esp_event_handler_t handler, void* arg){
    auto &info = domainMap.at(domain);
    return esp_event_handler_register_with(info.loop, info.base, ESP_EVENT_ANY_ID, handler, arg);
}