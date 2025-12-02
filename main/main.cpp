#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "storage.hpp"
#include "sync_manager.hpp"
#include "net_manager.hpp"
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
// #include "littlefs_console.cpp"

extern "C" void app_main(void) {
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI("MAIN", "Teste inicial do EventBus");
    EventBus::init();
    SyncManager::init();
    NetManager::init();
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
    // vTaskDelay(pdMS_TO_TICKS(10000));
    // register_littlefs_commands();
    // ESP_LOGI("APP_MAIN", "Comandos LittleFS registrados. Digite 'help' no monitor.");
    // setvbuf(stdin, NULL, _IONBF, 0);  // Sem buffer para stdin
    // setvbuf(stdout, NULL, _IONBF, 0); // Sem buffer para stdout
    // printf("esp32> "); // Primeiro prompt
    // char cmdline_buf[256];
    // int cmd_ret_code;
    // fflush(stdout);
    // while (1) {
    //    int i = 0;
    //     int c;
    //     bool line_received = false;

    //     // Loop para ler caracteres até encontrar um newline ou o buffer encher
    //     while ((c = fgetc(stdin)) != EOF && i < sizeof(cmdline_buf) - 1) {
    //         if (c == '\r' || c == '\n') {
    //             if (i > 0) { // Se algo foi digitado antes do newline
    //                 line_received = true;
    //                 break;
    //             }
    //             // Se for apenas um newline vazio, continue esperando por entrada real
    //             // ou apenas ignore e espere mais.
    //             // Para evitar prompts duplos, podemos ignorar newlines vazios.
    //             if (i == 0) {
    //                 printf("esp32> "); // Reimprime o prompt se apenas um newline vazio foi recebido
    //                 // fflush(stdout); // setvbuf(_IONBF) já faz isso
    //                 continue;
    //             }
    //         }
    //         cmdline_buf[i++] = (char)c;
    //     }
    //     cmdline_buf[i] = '\0'; // Termina a string

    //     if (line_received) {
    //         // Remove o caractere de nova linha (se presente, embora fgetc não o inclua no buffer)
    //         // cmdline_buf[strcspn(cmdline_buf, "\r\n")] = 0; // Não é mais necessário com fgetc

    //         if (strlen(cmdline_buf) > 0) {
    //             ESP_LOGI("APP_MAIN", "Comando recebido: '%s'", cmdline_buf); // Log para depuração
    //             esp_console_run(cmdline_buf, &cmd_ret_code);
    //             // fflush(stdout); // setvbuf(_IONBF) já faz isso
    //         }
    //         printf("esp32> "); // Próximo prompt
    //         // fflush(stdout); // setvbuf(_IONBF) já faz isso
    //     }
    //     // Se não houver entrada, ou se fgetc retornou EOF imediatamente, espere um pouco
    //     else if (c == EOF) {
    //         vTaskDelay(pdMS_TO_TICKS(10)); // Pequeno delay para outras tarefas
    //     }
    //     // Se não houver entrada e não for EOF, significa que não houve caractere, apenas loop rápido
    //     else {
    //         vTaskDelay(pdMS_TO_TICKS(10)); // Pequeno delay para outras tarefas
    //     }
    // }

}