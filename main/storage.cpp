#include "storage.hpp"

static const char* TAG = "Storage";

namespace Storage {
    static void loadFileToPsram(const char* path, const char* key, const char* mime) {
        FILE* f = fopen(path, "rb");
        if (!f) {ESP_LOGW(TAG, "Arquivo ausente: %s", path);return;}
        fseek(f, 0, SEEK_END);
        size_t sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        void* buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if(!buf){ESP_LOGE(TAG, "Falha ao alocar PSRAM (%s)", key);fclose(f);return;}
        fread(buf, 1, sz, f);
        fclose(f);
        void* page_mem = heap_caps_malloc(sizeof(Page), MALLOC_CAP_SPIRAM);
        if (!page_mem) {ESP_LOGE(TAG, "Falha ao alocar PSRAM (%s)", key);heap_caps_free(buf);return;}
        Page* p_psram = new (page_mem) Page();
        p_psram->data = buf;
        p_psram->size = sz;
        p_psram->mime = mime;
        StorageManager::registerPage(key, p_psram);
    }
    static void loadGlobalConfigFile() {
        const char* path = "/littlefs/config/config";
        FILE* f = fopen(path, "r");
        if(!f) {ESP_LOGW(TAG, "Arquivo ausente: %s", path);return;}
        char line[128];
        int index = 0;
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            switch (index) {
                case 0: if (*line) GlobalConfigData::cfg->ssid = line; break;
                case 1: if (*line) GlobalConfigData::cfg->password = line; break;
                case 2: if (*line) GlobalConfigData::cfg->central_name = line; break;
                case 3: if (*line) GlobalConfigData::cfg->token_id = line; break;
                case 4: if (*line) GlobalConfigData::cfg->token_password = line; break;
                case 5: if (*line) GlobalConfigData::cfg->token_flag = line; break;
            }
            ++index;
        }
        fclose(f);
        ESP_LOGI(TAG, "Configuração /config/config carregada (%d linhas)", index);
    }
    static void loadDeviceFile(const char* path, const char* id) {
        FILE* f = fopen(path, "r");
        if(!f){ESP_LOGW(TAG, "Arquivo de dispositivo ausente: %s", path);return;}
        void* dev_mem = heap_caps_malloc(sizeof(Device), MALLOC_CAP_SPIRAM);
        if(!dev_mem){ESP_LOGE(TAG, "Falha ao alocar PSRAM %s", id);fclose(f);return;}
        Device* dev = new (dev_mem) Device();
        char line[128];
        int index = 0;
        dev->id = id;
        while (fgets(line, sizeof(line), f) && index < 4) {
            line[strcspn(line, "\r\n")] = 0;
            switch (index) {
                case 0: dev->name = line; break;
                case 1: dev->type = static_cast<uint8_t>(atoi(line)); break;
                case 2: dev->time = static_cast<uint16_t>(atoi(line)); break;
                case 3: dev->status = static_cast<uint8_t>(atoi(line)); break;
                case 4: dev->x_str = line; break;
                case 5: dev->x_int = static_cast<uint8_t>(atoi(line)); break;
            }
            ++index;
        }
        fclose(f);
        StorageManager::registerDevice(id, dev);
    }
    void loadAllDevices() {
        const char* dir_path = "/littlefs/device";
        DIR* dir = opendir(dir_path);
        if(!dir){ESP_LOGW(TAG, "Diretório /device não encontrado");return;}
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {continue;}
            char full_path[512];  // ← Aumentado de 256 para 512
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            loadDeviceFile(full_path, entry->d_name);
        }
        closedir(dir);
        ESP_LOGI(TAG, "Total de dispositivos: %zu", StorageManager::getDeviceCount());
    }
    esp_err_t init() {
        ESP_LOGI(TAG, "Montando LittleFS...");
        esp_vfs_littlefs_conf_t conf = {"/littlefs","littlefs",nullptr,false,false,false,false};
        esp_err_t ret = esp_vfs_littlefs_register(&conf);
        if(ret!=ESP_OK){ESP_LOGE(TAG,"Falha ao montar LittleFS (%s)",esp_err_to_name(ret));return ret;}
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
        loadFileToPsram("/littlefs/ha/description.xml","description.xml","text/xml");
        loadFileToPsram("/littlefs/ha/apiget.json","apiget.json","application/json");
        loadFileToPsram("/littlefs/ha/lights_all.json", "lights_all.json", "application/json");
        loadFileToPsram("/littlefs/ha/light_detail.json", "light_detail.json", "application/json");
        loadGlobalConfigFile();
        loadAllDevices();
        ESP_LOGI(TAG, "Arquivos carregados na PSRAM.");
        return ESP_OK;
    }
}