#include "ota_manager.hpp"
#include "socket_manager.hpp"

static const char* TAG = "OtaManager";
namespace OtaManager {
    #define OTA_CHUNK_SIZE 102400
    #define OTA_TIMEOUT_MS 90000
    #define OTA_BUFFER_SIZE 10240
    std::string frmVersion="sendFrmAtual(Versão Atual do Firmware: v1_0_000";
    struct OtaTaskParams {std::string filename;int client_fd;
};
    static void ota_download_task(void* pvParameters) {
        OtaTaskParams* params = static_cast<OtaTaskParams*>(pvParameters);
        downloadFirmware(params->filename, params->client_fd);
        delete params;
        vTaskDelete(nullptr);
    }
    void downloadFirmwareAsync(const std::string& filename, int client_fd) {
        OtaTaskParams* params = new OtaTaskParams{filename, client_fd};
        xTaskCreatePinnedToCore(ota_download_task,"ota_download",8192,params,5,nullptr,1);
    }
    static void ota_factory_task(void* pvParameters) {
        OtaTaskParams* params = static_cast<OtaTaskParams*>(pvParameters);
        ESP_LOGI(TAG, "Resetando para factory...");
        const esp_partition_t *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,NULL);
        if (!factory) {ESP_LOGE(TAG, "Partição factory não encontrada");return;}
        ESP_LOGI(TAG,"Partição factory encontrada: %s (0x%08x, %u bytes)",factory->label,(unsigned int)factory->address,(unsigned int)factory->size);
        esp_err_t err = esp_ota_set_boot_partition(factory);
        if(err==ESP_OK)ESP_LOGI(TAG,"factory redirecionado");else ESP_LOGW(TAG,"factory não redirecionado");
        SocketManager::sendToClient(params->client_fd, "sendFabricaOK");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
        vTaskDelete(nullptr);
    }
    void factoryAsync(int client_fd){
        OtaTaskParams* params = new OtaTaskParams{"factory",client_fd};
        xTaskCreatePinnedToCore(ota_factory_task,"ota_factory",8192,params,5,nullptr,1);
    }
    std::vector<std::string> fetchFirmwareList() {
        std::vector<std::string> firmware_list;
        char* response_buffer = (char*)heap_caps_malloc(10240, MALLOC_CAP_SPIRAM);
        if (!response_buffer) {ESP_LOGE(TAG, "fetchFirmwareList: falha ao alocar buffer na PSRAM");return firmware_list;}
        esp_http_client_config_t config = {.url = "http://server.ia.srv.br/firmware/central/",.timeout_ms = 10000,.buffer_size = 1024,};
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {ESP_LOGE(TAG, "fetchFirmwareList: falha ao inicializar cliente HTTP");free(response_buffer);return firmware_list;}
        esp_err_t ret = esp_http_client_open(client, 0);
        if (ret != ESP_OK) {ESP_LOGE(TAG, "FList: falhou - %s", esp_err_to_name(ret));esp_http_client_cleanup(client);free(response_buffer);return firmware_list;}
        int content_length = esp_http_client_fetch_headers(client);
        if(content_length<=0 || content_length > 10240) {
            ESP_LOGE(TAG, "FList: content_length inválido %d", content_length);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            free(response_buffer);
            return firmware_list;
        }
        int total_read = 0;
        int bytes_read = 0;
        while((bytes_read=esp_http_client_read(client,response_buffer+total_read,10240-total_read-1))>0){total_read+=bytes_read;}
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if(total_read<=0){ESP_LOGE(TAG,"FList: nenhum dado recebido");free(response_buffer);return firmware_list;}
        response_buffer[total_read] = '\0';
        ESP_LOGI(TAG, "FList: resposta recebida (%d bytes)", total_read);
        const char* pos = response_buffer;
        while ((pos = strstr(pos, "<a href=\"")) != nullptr) {
            pos += 9;
            const char* start = pos;
            const char* end = strchr(pos, '"');
            if (!end) break;
            size_t len = end - start;
            if (len > 0 && len < 64) {
                std::string candidate(start, len);
                if (candidate.length() >= 4 && candidate.substr(candidate.length() - 4) == ".bin") {
                    size_t slash_pos = candidate.rfind('/');
                    std::string filename = (slash_pos != std::string::npos) ? 
                        candidate.substr(slash_pos + 1) : candidate;
                    if (std::find(firmware_list.begin(), firmware_list.end(), filename) == firmware_list.end()) {
                        firmware_list.push_back(filename);
                        ESP_LOGI(TAG, "fetchFirmwareList: encontrado %s", filename.c_str());
                    }
                }
            }
            pos = end + 1;
        }
        free(response_buffer);
        ESP_LOGI(TAG, "fetchFirmwareList: %zu firmwares encontrados", firmware_list.size());
        return firmware_list;
    }
    esp_err_t handleFrmCommand(int fd){
        ESP_LOGI(TAG, "handleFrmCommand: iniciado para fd=%d", fd);
        SocketManager::sendToClient(fd,frmVersion.c_str());
        std::vector<std::string> firmware_list = fetchFirmwareList();
        std::string response;
        if (firmware_list.empty()) {
            response = "msgFirmware: Não existem firmwares disponíveis";
            ESP_LOGW(TAG, "handleFrmCommand: lista vazia");
        } else {
            response = "firmware";
            for (const auto& filename : firmware_list) {
                response += "<a href=\"#\" class=\"ota-link\" data-filename=\"";
                response += filename;
                response += "\">";
                response += filename;
                response += "</a><br>\n";
            }
            ESP_LOGI(TAG, "handleFrmCommand: %zu firmwares montados", firmware_list.size());
        }
        SocketManager::sendToClient(fd, response.c_str());
        return ESP_OK;
    }
    static int calculateProgress(int bytes_downloaded, int total_bytes) {
        if (total_bytes <= 0) return 0;
        return (bytes_downloaded * 100) / total_bytes;
    }
    static void sendProgress(int client_fd, int percentage) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "progress%d", percentage);
        SocketManager::sendToClient(client_fd, buffer);
    }
    static int getFileSize(const std::string& url) {
        esp_http_client_config_t config = {};
        config.url = url.c_str();
        config.timeout_ms = OTA_TIMEOUT_MS;
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) return -1;
        esp_http_client_set_method(client, HTTP_METHOD_HEAD);
        esp_err_t ret = esp_http_client_perform(client);
        int content_length = -1;
        if (ret == ESP_OK) {content_length = esp_http_client_get_content_length(client);}
        esp_http_client_cleanup(client);
        return content_length;
    }
    bool performOtaDownload(const std::string& url, int client_fd) {
        ESP_LOGI(TAG, "performOtaDownload: iniciado para %s", url.c_str());
        int total_size = getFileSize(url);
        if (total_size <= 0) {
            ESP_LOGE(TAG, "performOtaDownload: falha ao obter tamanho do arquivo");
            return false;
        }
        ESP_LOGI(TAG, "performOtaDownload: tamanho total %d bytes", total_size);
        esp_http_client_config_t config = {};
        config.url = url.c_str();
        config.timeout_ms = OTA_TIMEOUT_MS;
        config.buffer_size = OTA_BUFFER_SIZE;
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {ESP_LOGE(TAG, "performOtaDownload: falha ao inicializar cliente HTTP");return false;}
        esp_err_t ret = esp_http_client_open(client, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "performOtaDownload: esp_http_client_open falhou - %s", esp_err_to_name(ret));
            esp_http_client_cleanup(client);
            return false;
        }
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length != total_size) {
            ESP_LOGW(TAG, "performOtaDownload: content_length mismatch %d vs %d", content_length, total_size);
        }
        uint8_t* chunk_buffer = (uint8_t*)heap_caps_malloc(OTA_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
        if (!chunk_buffer) {
            ESP_LOGE(TAG, "performOtaDownload: falha ao alocar buffer de chunk");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        esp_ota_handle_t ota_handle = 0;
        const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(nullptr);
        if (!ota_partition) {
            ESP_LOGE(TAG, "performOtaDownload: falha ao obter partição OTA");
            free(chunk_buffer);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        ret = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "performOtaDownload: esp_ota_begin falhou - %s", esp_err_to_name(ret));
            free(chunk_buffer);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        int bytes_downloaded = 0;
        int last_progress = -1;
        while (true) {
            int bytes_read = esp_http_client_read(client, (char*)chunk_buffer, OTA_CHUNK_SIZE);
            if (bytes_read < 0) {
                ESP_LOGE(TAG, "performOtaDownload: erro ao ler dados");
                esp_ota_abort(ota_handle);
                free(chunk_buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }
            if (bytes_read == 0) break;
            ret = esp_ota_write(ota_handle, chunk_buffer, bytes_read);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "performOtaDownload: esp_ota_write falhou - %s", esp_err_to_name(ret));
                esp_ota_abort(ota_handle);
                free(chunk_buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }
            bytes_downloaded += bytes_read;
            int progress = calculateProgress(bytes_downloaded, total_size);
            if (progress != last_progress) {
                sendProgress(client_fd, progress);
                last_progress = progress;
                ESP_LOGI(TAG, "performOtaDownload: progresso %d%%", progress);
            }
        }
        ret = esp_ota_end(ota_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "performOtaDownload: esp_ota_end falhou - %s", esp_err_to_name(ret));
            free(chunk_buffer);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        ret = esp_ota_set_boot_partition(ota_partition);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "performOtaDownload: esp_ota_set_boot_partition falhou - %s", esp_err_to_name(ret));
            free(chunk_buffer);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        free(chunk_buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        ESP_LOGI(TAG, "performOtaDownload: sucesso - %d bytes baixados", bytes_downloaded);
        return true;
    }
    void downloadFirmware(const std::string& filename, int client_fd) {
        ESP_LOGI(TAG, "downloadFirmware: iniciado para %s", filename.c_str());
        SocketManager::sendToClient(client_fd, "msgFirmwareIniciando atualização de firmware!");
        std::string url = "http://server.ia.srv.br/firmware/central/" + filename;
        if (!performOtaDownload(url, client_fd)) {
            SocketManager::sendToClient(client_fd, "msgFirmwareErro na instalação!");
            return;
        }
        SocketManager::sendToClient(client_fd, "msgFirmwareInstalação concluída, reiniciando Central!");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
}