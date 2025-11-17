#include "storage.hpp"

static const char* TAG = "Storage";
static const char* BASE_PATH = "/littlefs";

namespace Storage {
    static void loadGlobalConfigFile() {
        const char* path = (std::string(BASE_PATH)+std::string("/config/config")).c_str();
        FILE* f = fopen(path, "r");
        if(!f){ESP_LOGW(TAG, "Arquivo ausente: %s", path);return;}
        char line[128];
        int index = 0;
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            switch (index) {
                case 0:if(*line){strncpy(StorageManager::cfg->ssid,line,sizeof(StorageManager::cfg->ssid)-1);StorageManager::cfg->ssid[sizeof(StorageManager::cfg->ssid)-1]='\0';}break;
                case 1:if(*line){strncpy(StorageManager::cfg->password,line,sizeof(StorageManager::cfg->password)-1);StorageManager::cfg->password[sizeof(StorageManager::cfg->password)-1]='\0';}break;
                case 2:if(*line){strncpy(StorageManager::cfg->central_name,line,sizeof(StorageManager::cfg->central_name)-1);StorageManager::cfg->central_name[sizeof(StorageManager::cfg->central_name)-1]='\0';}break;
                case 3:if(*line){strncpy(StorageManager::cfg->token_id,line,sizeof(StorageManager::cfg->token_id)-1);StorageManager::cfg->token_id[sizeof(StorageManager::cfg->token_id)-1]='\0';}break;
                case 4:if(*line){strncpy(StorageManager::cfg->token_password,line,sizeof(StorageManager::cfg->token_password)-1);StorageManager::cfg->token_password[sizeof(StorageManager::cfg->token_password)-1]='\0';}break;
                case 5:if(*line){strncpy(StorageManager::cfg->token_flag,line,sizeof(StorageManager::cfg->token_flag)-1);StorageManager::cfg->token_flag[sizeof(StorageManager::cfg->token_flag)-1]='\0';}break;
            }
            ++index;
        }
        fclose(f);
        ESP_LOGI(TAG, "Configuração /config/config carregada (%d linhas)", index);
    }
    static void loadFileToPsram(const char* id, const char* k, const char* m) {
        const char* path = (std::string(BASE_PATH)+std::string(id)).c_str();
        FILE* f = fopen(path, "rb");
        if (!f) {ESP_LOGW(TAG, "Arquivo ausente: %s", path);return;}
        fseek(f, 0, SEEK_END);
        size_t sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        void* buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if(!buf){ESP_LOGE(TAG, "Falha ao alocar PSRAM (%s)", k);fclose(f);return;}
        fread(buf, 1, sz, f);
        fclose(f);
        void* page_mem = heap_caps_malloc(sizeof(Page), MALLOC_CAP_SPIRAM);
        if (!page_mem) {ESP_LOGE(TAG, "Falha ao alocar PSRAM (%s)", k);heap_caps_free(buf);return;}
        Page* p_psram = new (page_mem) Page();
        p_psram->data = buf;
        p_psram->size = sz;
        p_psram->mime = m;
        StorageManager::registerPage(k, p_psram);
    }
    static void loadDeviceFile(const char* path, const char* id_from_filename) {
        FILE* f = fopen(path, "r");
        if(!f){ESP_LOGW(TAG, "Arquivo de dispositivo ausente: %s", path);return;}
        void* dev_mem = heap_caps_malloc(sizeof(Device), MALLOC_CAP_SPIRAM);
        if(!dev_mem){ESP_LOGE(TAG, "Falha ao alocar PSRAM %s", id_from_filename);fclose(f);return;}
        Device* dev = new (dev_mem) Device();
        memset(dev->id, 0, sizeof(dev->id));
        memset(dev->name, 0, sizeof(dev->name));
        memset(dev->x_str, 0, sizeof(dev->x_str));
        char line[128];
        int index = 0;
        while (fgets(line, sizeof(line), f) && index < 6) {
            line[strcspn(line, "\r\n")] = 0;
            switch (index) {
                case 0:strncpy(dev->name, line, sizeof(dev->name) - 1);dev->name[sizeof(dev->name) - 1] = '\0';break;
                case 1:dev->type = static_cast<uint8_t>(atoi(line));break;
                case 2:dev->time = static_cast<uint16_t>(atoi(line));break;
                case 3:dev->status = static_cast<uint8_t>(atoi(line));break;
                case 4:strncpy(dev->x_str, line, sizeof(dev->x_str) - 1);dev->x_str[sizeof(dev->x_str) - 1] = '\0';break;
                case 5:dev->x_int = static_cast<uint8_t>(atoi(line));break;
            }
            ++index;
        }
        fclose(f);
        StorageManager::registerDevice(std::string(id_from_filename), dev);
    }
    void loadAllDevices() {
        const char* dir_path = (std::string(BASE_PATH)+std::string("/device")).c_str();
        DIR* dir = opendir(dir_path);
        if(!dir){ESP_LOGW(TAG, "Diretório /device não encontrado");return;}
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {continue;}
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            loadDeviceFile(full_path, entry->d_name);
        }
        closedir(dir);
        ESP_LOGI(TAG, "Total de dispositivos: %zu", StorageManager::getDeviceCount());
    }
    esp_err_t saveDeviceFile(Device* device) {
        if (!device) {ESP_LOGE(TAG, "Ponteiro de dispositivo nulo para salvar.");return ESP_ERR_INVALID_ARG;}
        const char* file_path = (std::string(BASE_PATH)+std::string("/device/")+std::string(device->id)).c_str();
        FILE* f = fopen(file_path, "w");
        if (!f) {ESP_LOGE(TAG, "Falha ao abrir arquivo para escrita: %s", file_path);return ESP_FAIL;}
        fprintf(f, "%s\n", device->id);
        fprintf(f, "%s\n", device->name);
        fprintf(f, "%u\n", device->type);
        fprintf(f, "%u\n", device->time);
        fprintf(f, "%u\n", device->status);
        fprintf(f, "%s\n", device->x_str);
        fprintf(f, "%u\n", device->x_int);
        fclose(f);
        ESP_LOGI(TAG, "Dispositivo '%s' salvo em %s", device->id, file_path);
        return ESP_OK;
    }
    static void loadSensorFile(const char* path, const char* id_from_filename) {
        FILE* f = fopen(path, "r");
        if(!f){ESP_LOGW(TAG, "Arquivo de sensor ausente: %s", path);return;}
        void* sen_mem = heap_caps_malloc(sizeof(Sensor), MALLOC_CAP_SPIRAM);
        if(!sen_mem){ESP_LOGE(TAG, "Falha ao alocar PSRAM %s", id_from_filename);fclose(f);return;}
        Sensor* sen = new (sen_mem) Sensor();
        memset(sen->id, 0, sizeof(sen->id));
        memset(sen->name, 0, sizeof(sen->name));
        memset(sen->x_str, 0, sizeof(sen->x_str));
        char line[128];
        int index = 0;
        while (fgets(line, sizeof(line), f) && index < 6) {
            line[strcspn(line, "\r\n")] = 0;
            switch (index) {
                case 0:strncpy(sen->name, line, sizeof(sen->name) - 1);sen->name[sizeof(sen->name) - 1] = '\0';break;
                case 1:sen->type = static_cast<uint8_t>(atoi(line));break;
                case 2:sen->time = static_cast<uint16_t>(atoi(line));break;
                case 3:sen->status = static_cast<uint8_t>(atoi(line));break;
                case 4:strncpy(sen->x_str, line, sizeof(sen->x_str) - 1);sen->x_str[sizeof(sen->x_str) - 1] = '\0';break;
                case 5:sen->x_int = static_cast<uint8_t>(atoi(line));break;
            }
            ++index;
        }
        fclose(f);
        StorageManager::registerSensor(std::string(id_from_filename), sen);
    }
    void loadAllSensors() {
        const char* dir_path = (std::string(BASE_PATH)+std::string("/sensor")).c_str();
        DIR* dir = opendir(dir_path);
        if(!dir){ESP_LOGW(TAG, "Diretório /sensor não encontrado");return;}
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {continue;}
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            loadSensorFile(full_path, entry->d_name);
        }
        closedir(dir);
        ESP_LOGI(TAG, "Total de sensores: %zu", StorageManager::getSensorCount());
    }
    esp_err_t saveSensorFile(Sensor* sensor) {
        if (!sensor) {ESP_LOGE(TAG, "Ponteiro de sensor nulo para salvar.");return ESP_ERR_INVALID_ARG;}
        const char* file_path = (std::string(BASE_PATH)+std::string("/sensor/")+std::string(sensor->id)).c_str();
        FILE* f = fopen(file_path, "w");
        if (!f) {ESP_LOGE(TAG, "Falha ao abrir arquivo para escrita: %s", file_path);return ESP_FAIL;}
        fprintf(f, "%s\n", sensor->id);
        fprintf(f, "%s\n", sensor->name);
        fprintf(f, "%u\n", sensor->type);
        fprintf(f, "%u\n", sensor->time);
        fprintf(f, "%u\n", sensor->status);
        fprintf(f, "%s\n", sensor->x_str);
        fprintf(f, "%u\n", sensor->x_int);
        fclose(f);
        ESP_LOGI(TAG, "Dispositivo '%s' salvo em %s", sensor->id, file_path);
        return ESP_OK;
    }
    esp_err_t saveGlobalConfigFile(GlobalConfig* cfg) {
        if(!cfg){ESP_LOGE(TAG,"Ponteiro GlobalConfig nulo na salva.");return ESP_ERR_INVALID_ARG;}
        const char* path = (std::string(BASE_PATH)+std::string("/config/config")).c_str();
        FILE* f = fopen(path, "w");
        if(!f){ESP_LOGE(TAG, "Falha ao abrir arquivo LittleFS para escrita: %s", path);return ESP_FAIL;}
        fprintf(f, "%s\n", cfg->ssid);
        fprintf(f, "%s\n", cfg->password);
        fprintf(f, "%s\n", cfg->central_name);
        fprintf(f, "%s\n", cfg->token_id);
        fprintf(f, "%s\n", cfg->token_password);
        fprintf(f, "%s\n", cfg->token_flag);
        if(ferror(f)){ESP_LOGE(TAG, "Erro de escrita no arquivo LittleFS: %s", path);fclose(f);return ESP_FAIL;}
        fclose(f);
        ESP_LOGI(TAG, "Configuração salva com sucesso em LittleFS: %s", path);
        return ESP_OK;
    }
    esp_err_t init() {
        ESP_LOGI(TAG, "Montando LittleFS...");
        esp_vfs_littlefs_conf_t conf = {BASE_PATH,"littlefs",nullptr,false,false,false,false};
        esp_err_t ret = esp_vfs_littlefs_register(&conf);
        if(ret!=ESP_OK){ESP_LOGE(TAG,"Falha ao montar LittleFS (%s)",esp_err_to_name(ret));return ret;}
        ESP_LOGI(TAG, "LittleFS montado com sucesso, carregando arquivos...");
        loadGlobalConfigFile();
        loadFileToPsram("/index.html","index.html","text/html");
        loadFileToPsram("/agenda.html","agenda.html","text/html");
        loadFileToPsram("/automacao.html","automacao.html","text/html");
        loadFileToPsram("/atualizar.html","atualizar.html","text/html");
        loadFileToPsram("/central.html","central.html","text/html");
        loadFileToPsram("/css/igra.css","css/igra.css","text/css");
        loadFileToPsram("/css/bootstrap.min.css","css/bootstrap.min.css","text/css");
        loadFileToPsram("/js/messages.js","js/messages.js","application/javascript");
        loadFileToPsram("/js/icons.js","js/icons.js","application/javascript");
        loadFileToPsram("/img/logomarca","img/logomarca","image/png");
        loadFileToPsram("/img/favicon.ico","favicon.ico","image/x-icon");
        loadFileToPsram("/ha/description.xml","description.xml","text/xml");
        loadFileToPsram("/ha/apiget.json","apiget.json","application/json");
        loadFileToPsram("/ha/lights_all.json", "lights_all.json", "application/json");
        loadFileToPsram("/ha/light_detail.json", "light_detail.json", "application/json");
        std::string devices_dir = std::string(BASE_PATH) + "/devices";
        DIR* dir = opendir(devices_dir.c_str());
        ESP_LOGI(TAG, "Diretório de dispositivos garantido: %s", devices_dir.c_str());
        loadAllDevices();
        std::string sensors_dir = std::string(BASE_PATH) + "/sensors";
        DIR* dir = opendir(sensors_dir.c_str());
        ESP_LOGI(TAG, "Diretório de sensores garantido: %s", sensors_dir.c_str());
        loadAllSensors();
        return ESP_OK;
        ESP_LOGI(TAG, "Arquivos carregados na PSRAM.");
        return ESP_OK;
    }
}