#include "storage.hpp"

static const char* TAG = "Storage";

namespace Storage {
    static void loadGlobalConfigFile() {
        const char* path = "/littlefs/config/config";
        FILE* f = fopen(path, "r");
        if(!f) {ESP_LOGW(TAG, "Arquivo ausente: %s", path);return;}
        char line[128];
        int index = 0;
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            switch (index) {
                case 0: if (*line) GlobalConfigData::cfg->ssid           = line; break;
                case 1: if (*line) GlobalConfigData::cfg->password       = line; break;
                case 2: if (*line) GlobalConfigData::cfg->central_name   = line; break;
                case 3: if (*line) GlobalConfigData::cfg->token_id       = line; break;
                case 4: if (*line) GlobalConfigData::cfg->token_password = line; break;
                case 5: if (*line) GlobalConfigData::cfg->token_flag     = line; break;
            }
            ++index;
        }
        fclose(f);
        ESP_LOGI(TAG, "Configuração /config/config carregada (%d linhas)", index);
    }
    esp_err_t init() {
        ESP_LOGI(TAG, "Montando LittleFS...");
        esp_vfs_littlefs_conf_t conf = {"/littlefs","littlefs",nullptr,false,false,false,false};
        esp_err_t ret = esp_vfs_littlefs_register(&conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao montar LittleFS (%s)", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "LittleFS montado com sucesso, carregando arquivos...");
        loadFileToPsram("/littlefs/index.html","index.html","text/html");
        loadFileToPsram("/littlefs/agenda.html","agenda.html","text/html");
        loadFileToPsram("/littlefs/automacao.html","automacao.html","text/html");
        loadFileToPsram("/littlefs/atualizar.html","atualizar.html","text/html");
        loadFileToPsram("/littlefs/central.html","central.html","text/html");
        loadFileToPsram("/littlefs/css/igra.css","css/igra.css","text/css");
        loadFileToPsram("/littlefs/css/bootstrap.min.css","css/bootstrap.min.css","text/css");
        loadFileToPsram("/littlefs/js/messages.js","js/messages.js","application/javascript");
        loadFileToPsram("/littlefs/js/icons.js","js/icons.js","application/javascript");
        loadFileToPsram("/littlefs/img/logomarca","img/logomarca","image/png");
        loadGlobalConfigFile();
        ESP_LOGI(TAG, "Arquivos carregados na PSRAM.");
        return ESP_OK;
    }
    static void loadFileToPsram(const char* path, const char* key, const char* mime) {
        FILE* f = fopen(path, "rb");
        if (!f) {
            ESP_LOGW(TAG, "Arquivo ausente: %s", path);
            return;
        }
        fseek(f, 0, SEEK_END);
        size_t sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        void* buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if (!buf) {
            ESP_LOGE(TAG, "Falha ao alocar PSRAM (%s)", key);
            fclose(f);
            return;
        }
        fread(buf, 1, sz, f);
        fclose(f);
        StorageManager::registerPage(key, {buf, sz, mime});
    }
}