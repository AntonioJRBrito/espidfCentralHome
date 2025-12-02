#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include <errno.h>

static const char* TAG = "LittleFS_Console";

static int ls_command(int argc, char** argv) {
    ESP_LOGI("LS_CMD", "Comando 'ls' chamado com %d argumentos.", argc);
    const char* dir_path = "/littlefs";
    if (argc > 1) {dir_path = argv[1];ESP_LOGI("LS_CMD", "Caminho especificado: %s", dir_path);}
    else {ESP_LOGI("LS_CMD", "Nenhum caminho especificado, usando padrão: %s", dir_path);}
    DIR* dir = opendir(dir_path);
    if (!dir) {printf("Erro: Nao foi possivel abrir o diretorio '%s'.\n", dir_path);return 1;}
    struct dirent* entry;
    printf("Conteudo de '%s':\n", dir_path);
    while ((entry = readdir(dir)) != NULL) {printf("- %s\n", entry->d_name);}
    closedir(dir);
    ESP_LOGI("LS_CMD", "Comando 'ls' concluido com sucesso.");
    return 0;
}
static int cat_command(int argc, char** argv) {
    ESP_LOGI(TAG, "Comando 'cat' chamado com %d argumentos.", argc);
    if (argc > 0) {ESP_LOGI(TAG, "argv[0] (comando): '%s'", argv[0]);}
    if (argc > 1) {ESP_LOGI(TAG, "argv[1] (caminho): '%s'", argv[1]);}
    if (argc < 2) {ESP_LOGE(TAG, "Uso incorreto: cat <caminho_do_arquivo>");printf("Uso: cat <caminho_do_arquivo>\n");return 1;}
    const char* file_path = argv[1];
    ESP_LOGI(TAG, "Tentando abrir arquivo: '%s'", file_path);
    FILE* f = fopen(file_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Falha ao abrir arquivo '%s': %s", file_path, strerror(errno));
        printf("Erro: Nao foi possivel abrir o arquivo '%s'. (%s)\n", file_path, strerror(errno));
        return 1;
    }
    char line[256];
    printf("Conteudo de '%s':\n", file_path);
    while (fgets(line, sizeof(line), f) != NULL) {printf("%s", line);}
    fclose(f);
    ESP_LOGI(TAG, "Comando 'cat' concluido com sucesso.");
    return 0;
}
void register_littlefs_commands() {
    ESP_LOGI(TAG, "register_littlefs_commands executado");
    const esp_console_cmd_t ls_cmd = {
        .command = "ls",
        .help = "Lista o conteúdo de um diretório LittleFS",
        .hint = "[caminho]",
        .func = &ls_command,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ls_cmd));
    const esp_console_cmd_t cat_cmd = {
        .command = "cat",
        .help = "Exibe o conteúdo de um arquivo LittleFS",
        .hint = "<caminho_do_arquivo>",
        .func = &cat_command,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cat_cmd));
    esp_console_config_t console_config = {256,8};
    ESP_ERROR_CHECK(esp_console_init(&console_config));
    uart_driver_install((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0);
    esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    uart_set_rx_timeout((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 100);
    esp_console_register_help_command();
}