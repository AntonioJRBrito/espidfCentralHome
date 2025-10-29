#include "sync_manager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "SyncManager";

namespace SyncManager
{
    static constexpr uint8_t DOMAIN_COUNT = static_cast<uint8_t>(EventDomain::BLE) + 1;
    constexpr uint32_t BIT_NET = 1u << 0;
    constexpr uint32_t BIT_RTC = 1u << 1;
    constexpr uint32_t BIT_DEV = 1u << 2;
    constexpr uint32_t BIT_BLE = 1u << 3;
    constexpr uint32_t BIT_SOC = 1u << 4;
    constexpr uint32_t BIT_WEB = 1u << 5;
    constexpr uint32_t BIT_UDP = 1u << 6;
    constexpr uint32_t BIT_BRK = 1u << 7;
    constexpr uint32_t BIT_MQT = 1u << 8;
    constexpr uint32_t BIT_MTT = 1u << 9;
    constexpr uint32_t BIT_AUT = 1u << 10;
    constexpr uint32_t BIT_OTA = 1u << 11;
    constexpr uint32_t BIT_STO = 1u << 12;
    static constexpr uint32_t ALL_MASK =
        BIT_NET | BIT_RTC | BIT_DEV | BIT_SOC | BIT_WEB | BIT_UDP | BIT_BRK | BIT_MQT | BIT_MTT | BIT_AUT | BIT_OTA | BIT_STO | BIT_BLE;
    static uint32_t ready_mask = 0;
    static void onReady(void*, esp_event_base_t base, int32_t id, void* arg)
    {
        auto domain = static_cast<EventDomain>(reinterpret_cast<uintptr_t>(arg));
        EventId eventId = static_cast<EventId>(id);
        ESP_LOGI(TAG, "Recebido evento base=%s id=%d", base ? (const char*)base : "(null)", (int)id);
        if (eventId == EventId::READY_ALL) return;
        if (eventId == EventId::NET_READY  ||
            eventId == EventId::RTC_READY  ||
            eventId == EventId::DEV_READY  ||
            eventId == EventId::SOC_READY  ||
            eventId == EventId::WEB_READY  ||
            eventId == EventId::UDP_READY  ||
            eventId == EventId::BRK_READY  ||
            eventId == EventId::MQT_READY  ||
            eventId == EventId::MTT_READY  ||
            eventId == EventId::AUT_READY  ||
            eventId == EventId::OTA_READY  ||
            eventId == EventId::STO_READY  ||
            eventId == EventId::BLE_READY)
        {
            switch (eventId)
            {
                case EventId::NET_READY:  ready_mask |= BIT_NET; break;
                case EventId::RTC_READY:  ready_mask |= BIT_RTC; break;
                case EventId::DEV_READY:  ready_mask |= BIT_DEV; break;
                case EventId::SOC_READY:  ready_mask |= BIT_SOC; break;
                case EventId::WEB_READY:  ready_mask |= BIT_WEB; break;
                case EventId::UDP_READY:  ready_mask |= BIT_UDP; break;
                case EventId::BRK_READY:  ready_mask |= BIT_BRK; break;
                case EventId::MQT_READY:  ready_mask |= BIT_MQT; break;
                case EventId::MTT_READY:  ready_mask |= BIT_MTT; break;
                case EventId::AUT_READY:  ready_mask |= BIT_AUT; break;
                case EventId::OTA_READY:  ready_mask |= BIT_OTA; break;
                case EventId::STO_READY:  ready_mask |= BIT_STO; break;
                case EventId::BLE_READY:  ready_mask |= BIT_BLE; break;
                default:
                    return;
            }
            ESP_LOGI(TAG, "Domínio %u pronto (mask=0x%08X)", static_cast<unsigned>(domain), ready_mask);
        }
        if (ready_mask == ALL_MASK)
        {
            ESP_LOGI(TAG, "→ Todos os domínios prontos: publicando READY_ALL");
            EventBus::post(EventDomain::NETWORK, EventId::READY_ALL);
            deinit();
        }
    }
    esp_err_t init()
    {
        ESP_LOGI(TAG, "Registrando handlers de sincronização (%u domínios)", DOMAIN_COUNT);
        for (uint8_t d = 0; d < DOMAIN_COUNT; ++d)
        {
            EventBus::regHandler(static_cast<EventDomain>(d), &onReady, reinterpret_cast<void*>(static_cast<uintptr_t>(d)));
        }
        return ESP_OK;
    }
    esp_err_t deinit()
    {
        ESP_LOGW(TAG, "Encerrando SyncManager e removendo handlers...");
        for (uint8_t d = 0; d < DOMAIN_COUNT; ++d)
        {
            EventBus::unregHandler(static_cast<EventDomain>(d), &onReady);
        }
        ESP_LOGI(TAG, "SyncManager fora das filas de evento");
        return ESP_OK;
    }
}