#include "dns_manager.hpp"

static const char* TAG = "DnsManager";
namespace DnsManager {
    static int sock = -1;
    static bool running = false;
    static TaskHandle_t taskHandle = nullptr;
    static void dns_task(void*) {
        ESP_LOGI(TAG, "Servidor DNS local iniciado...");
        sockaddr_in client{};
        socklen_t clen = sizeof(client);
        uint8_t buf[512];
        while (running) {
            int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&client, &clen);
            if (n <= 0) continue;
            sendto(sock, buf, n, 0, (struct sockaddr*)&client, clen);
        }
        close(sock);
        vTaskDelete(nullptr);
    }
    static void onEventBus(void*, esp_event_base_t, int32_t id, void*) {
        if (static_cast<EventId>(id) == EventId::NET_IFOK) {
            ESP_LOGI(TAG, "NET_IFOK → iniciando DNS...");
            esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            if (!ap) return;
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(ap, &ip) != ESP_OK) return;
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(53);
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            bind(sock, (struct sockaddr*)&addr, sizeof(addr));
            running = true;
            xTaskCreate(dns_task, "dns_task", 4096, nullptr, 3, &taskHandle);
            ESP_LOGI(TAG, "DNS escutando em " IPSTR, IP2STR(&ip.ip));
            EventBus::unregHandler(EventDomain::DNS, &onEventBus);
        }
    }
    esp_err_t init() {
        EventBus::regHandler(EventDomain::NETWORK, &onEventBus, nullptr);
        EventBus::post(EventDomain::DNS, EventId::DNS_READY);
        ESP_LOGI(TAG, "DnsManager registrado, aguardando NET_IFOK");
        return ESP_OK;
    }
}