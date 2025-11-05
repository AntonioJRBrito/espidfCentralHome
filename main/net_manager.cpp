#include "net_manager.hpp"

static const char* TAG = "NetManager";

namespace NetManager
{
    static bool s_isTestConnection = false;
    static int s_retry_count = 0;
    static constexpr int MAX_RETRY = 5;
    static esp_netif_t* netif_ap = nullptr;
    static esp_netif_t* netif_sta = nullptr;
    static bool s_scan_in_progress = false;
    static int s_requesting_fd = -1;
    static esp_err_t connectSTA(const char* testSsid = nullptr, const char* testPass = nullptr, bool isTest = false)
    {
        s_isTestConnection = isTest;
        const char* ssidToUse = testSsid ? testSsid : GlobalConfigData::cfg->ssid.c_str();
        const char* passToUse = testPass ? testPass : GlobalConfigData::cfg->password.c_str();
        ESP_LOGI(TAG, "%s conexão STA... (SSID: %s)",s_isTestConnection ? "Testando" : "Iniciando", ssidToUse);
        wifi_config_t sta_cfg{};
        strncpy((char*)sta_cfg.sta.ssid, ssidToUse, sizeof(sta_cfg.sta.ssid) - 1);
        strncpy((char*)sta_cfg.sta.password, passToUse, sizeof(sta_cfg.sta.password) - 1);
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_cfg.sta.pmf_cfg.capable = true;
        sta_cfg.sta.pmf_cfg.required = false;
        if (isTest) {
            // 🔹 Futuro: adicionar timeout ou callback para validar
            ESP_LOGI(TAG, "Modo TESTE: esta conexão não será salva.");
        }
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
        esp_err_t ret = esp_wifi_connect();
        if (ret == ESP_OK) ESP_LOGI(TAG, "STA %s iniciada, aguardando conexão...", s_isTestConnection ? "de teste" : "real");
        else ESP_LOGE(TAG, "Falha ao iniciar STA (%s): %s", s_isTestConnection ? "teste" : "real", esp_err_to_name(ret));
        return ret;
    }
    static void onEventReadyBus(void*,esp_event_base_t base,int32_t id,void*)
    {
        if(static_cast<EventId>(id)==EventId::READY_ALL){
            EventBus::post(EventDomain::NETWORK, EventId::NET_IFOK);
            ESP_LOGI(TAG, "NET_IFOK enviado");
        }
    }
    static void onEventNetworkBus(void*,esp_event_base_t base,int32_t id,void* data)
    {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_LISTQRY) {
            if(data){memcpy(&s_requesting_fd,data,sizeof(int));ESP_LOGI(TAG,"Pedido de lista WiFi recebido (fd=%d)",s_requesting_fd);}
            else{ESP_LOGW(TAG,"Pedido de lista WiFi sem fd válido!");s_requesting_fd = -1;}
            if (GlobalConfigData::isWifiCacheValid()) {
                ESP_LOGI(TAG, "Cache WiFi válido, enviando imediatamente");
                EventBus::post(EventDomain::NETWORK, EventId::NET_LISTOK,&s_requesting_fd, sizeof(int));
                s_requesting_fd = -1;
            } else {
                if (s_scan_in_progress) {
                    ESP_LOGW(TAG, "Scan já em progresso, aguardando...");
                } else {
                    ESP_LOGI(TAG, "Cache inválido, iniciando scan WiFi...");
                    s_scan_in_progress = true;
                    wifi_scan_config_t scan_config = {
                        .ssid = nullptr,
                        .bssid = nullptr,
                        .channel = 0,
                        .show_hidden = true,
                        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                        .scan_time = { .active = { .min = 100, .max = 300 } }
                    };
                    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);  // false = não bloqueante
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Falha ao iniciar scan: %s", esp_err_to_name(ret));
                        s_scan_in_progress = false;
                        GlobalConfigData::updateWifiCache("<option value=''>Erro ao buscar redes</option>");
                        EventBus::post(EventDomain::NETWORK, EventId::NET_LISTFAIL,&s_requesting_fd,sizeof(int));
                        s_requesting_fd = -1;
                    }
                }
            }
        }
    }
    static void onEventStorageBus(void*,esp_event_base_t base,int32_t id,void*)
    {
        if (static_cast<EventId>(id)==EventId::STO_SSIDOK) {
            ESP_LOGI(TAG, "Iniciando STA");
            connectSTA();
        }
    }
    static void onWifiEvent(void*, esp_event_base_t base, int32_t id, void* data)
    {
        if(base==WIFI_EVENT)
        {
            switch (id)
            {
                case WIFI_EVENT_AP_START:
                {
                    dns_server_config_t config = DNS_SERVER_CONFIG_SINGLE("*","WIFI_AP_DEF");
                    start_dns_server(&config);
                    ESP_LOGI(TAG, "AP iniciado");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_APCONNECTED);
                    break;
                }
                case WIFI_EVENT_AP_STOP:
                    ESP_LOGW(TAG, "AP parado");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_APDISCONNECTED);
                    break;
                case WIFI_EVENT_STA_START:
                    ESP_LOGI(TAG, "STA iniciou");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_STASTARTED);
                    break;
                case WIFI_EVENT_STA_STOP:
                    ESP_LOGI(TAG, "WiFi STA parado");
                    GlobalConfigData::cfg->is_connected_sta="0";
                    EventBus::post(EventDomain::NETWORK, EventId::NET_STASTOPPED);
                    break;
                case WIFI_EVENT_STA_CONNECTED:
                    ESP_LOGI(TAG, "STA conectada");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_STACONNECTED);
                    break;
                case WIFI_EVENT_STA_DISCONNECTED:
                    GlobalConfigData::cfg->is_connected_sta="0";
                    if (s_isTestConnection) {
                        ESP_LOGW(TAG, "[TESTE] Falha ao conectar à rede fornecida.");
                        EventBus::post(EventDomain::NETWORK, EventId::NET_TESTFAILED);
                        s_isTestConnection = false;
                    } else {
                        if (s_retry_count < MAX_RETRY) {
                            s_retry_count++;
                            ESP_LOGW(TAG, "Tentando reconectar... (%d/%d)", s_retry_count, MAX_RETRY);
                            esp_wifi_connect();
                        } else {
                            ESP_LOGE(TAG, "Falha após %d tentativas.", s_retry_count);
                            EventBus::post(EventDomain::NETWORK, EventId::NET_STADISCONNECTED);
                        }
                    }
                    break;
                case WIFI_EVENT_AP_STACONNECTED:
                {
                    wifi_event_ap_staconnected_t* info = (wifi_event_ap_staconnected_t*)data;
                    uint32_t aid = info->aid;
                    ESP_LOGI(TAG, "Cliente conectado (AID=%u)", aid);
                    EventBus::post(EventDomain::NETWORK, EventId::NET_APCLICONNECTED,&aid,sizeof(aid));
                    break;
                }
                case WIFI_EVENT_AP_STADISCONNECTED:
                {
                    wifi_event_ap_staconnected_t* info = (wifi_event_ap_staconnected_t*)data;
                    uint32_t aid = info->aid;
                    ESP_LOGI(TAG, "Cliente desconectado (AID=%u)", aid);
                    EventBus::post(EventDomain::NETWORK, EventId::NET_APCLIDISCONNECTED,&aid,sizeof(aid));
                    break;
                }
                case WIFI_EVENT_SCAN_DONE: {
                    ESP_LOGI(TAG, "Scan WiFi concluído");
                    s_scan_in_progress = false;
                    uint16_t num_networks = 0;
                    esp_wifi_scan_get_ap_num(&num_networks);
                    if (num_networks == 0) {
                        ESP_LOGW(TAG, "Nenhuma rede encontrada");
                        GlobalConfigData::updateWifiCache("<option value=''>Nenhuma rede encontrada</option>");
                        if (s_requesting_fd != -1) {EventBus::post(EventDomain::NETWORK, EventId::NET_LISTOK, &s_requesting_fd, sizeof(int));s_requesting_fd = -1;}
                        return;
                    }
                    wifi_ap_record_t* ap_records = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * num_networks);
                    if (!ap_records) {
                        ESP_LOGE(TAG, "Falha ao alocar memória para scan");
                        GlobalConfigData::updateWifiCache("<option value=''>Erro de memória</option>");
                        if (s_requesting_fd != -1) {EventBus::post(EventDomain::NETWORK, EventId::NET_LISTFAIL, &s_requesting_fd, sizeof(int));s_requesting_fd = -1;}
                        return;
                    }
                    esp_wifi_scan_get_ap_records(&num_networks, ap_records);
                    ESP_LOGI(TAG, "Scan encontrou %d redes", num_networks);
                    std::string html_options;
                    std::string current_ssid = GlobalConfigData::cfg->ssid;
                    if (!current_ssid.empty()) {ESP_LOGI(TAG, "SSID atual configurado: %s", current_ssid.c_str()); }
                    for (uint16_t i = 0; i < num_networks; i++) {
                        std::string ssid = (char*)ap_records[i].ssid;
                        bool is_current = (ssid == current_ssid);
                        if (is_current) {ESP_LOGI(TAG, "Rede atual encontrada: %s", ssid.c_str());}
                        html_options += "<option value='" + ssid + "'";
                        if (is_current) {html_options += " selected";}
                        html_options += ">" + ssid + "</option>";
                    }
                    GlobalConfigData::updateWifiCache(html_options);
                    ESP_LOGI(TAG, "Cache WiFi atualizado: %zu redes", num_networks);
                    if (s_requesting_fd != -1) {
                        EventBus::post(EventDomain::NETWORK, EventId::NET_LISTOK, &s_requesting_fd, sizeof(int));
                        s_requesting_fd = -1;
                    }
                    free(ap_records);
                    break;
                }
                default:
                    break;
            }
        }
        else if(base==IP_EVENT)
        {
            switch (id)
            {
                case IP_EVENT_AP_STAIPASSIGNED:
                    // ESP_LOGI(TAG, "Cliente conectado ao AP");
                    // EventBus::post(EventDomain::NETWORK, EventId::NET_STAGOTIP);
                    // break;
                case IP_EVENT_STA_GOT_IP:
                    ESP_LOGI(TAG, "STA obteve IP.");
                    s_retry_count = 0;
                    GlobalConfigData::cfg->is_connected_sta="1";
                    if (s_isTestConnection) {
                        ESP_LOGI(TAG, "[TESTE] Rede válida. IP obtido com sucesso!");
                        EventBus::post(EventDomain::NETWORK, EventId::NET_TESTSUCCESS);
                        s_isTestConnection = false;
                    } else {EventBus::post(EventDomain::NETWORK, EventId::NET_STAGOTIP);}
                    break;
                default:
                    break;
            }
        }
    }
    static esp_err_t startAP()
    {
        // --- Inicialização do NVS ---
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
            ret == ESP_ERR_NVS_NEW_VERSION_FOUND ||
            ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS inválido, apagando...");
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        // --- Inicialização das interfaces e eventos ---
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        // Cria as interfaces AP e STA (as duas, pois o modo será AP+STA)
        netif_ap  = esp_netif_create_default_wifi_ap();
        netif_sta = esp_netif_create_default_wifi_sta();
        // --- Inicializa o Wi-Fi ---
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        // --- Registra handlers de evento ---
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   ESP_EVENT_ANY_ID, &onWifiEvent, nullptr));
        // --- Lê o MAC (caso precise para compor o SSID) ---
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        // --- Configura o Access Point ---
        wifi_config_t ap_cfg{};
        strncpy((char*)ap_cfg.ap.ssid, GlobalConfigData::cfg->hostname.c_str(), sizeof(ap_cfg.ap.ssid));
        ap_cfg.ap.ssid_len = strlen((char*)ap_cfg.ap.ssid);
        ap_cfg.ap.channel = 1;
        ap_cfg.ap.max_connection = 4;
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        // --- Define modo e configurações ---
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));       // Modo duplo AP+STA
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg)); // Configura o AP
        // --- Inicia o Wi-Fi ---
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "Access Point iniciado: SSID=%s, canal=%d", ap_cfg.ap.ssid, ap_cfg.ap.channel);
        return ESP_OK;
    }
    esp_err_t init()
    {
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr);
        esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr);
        EventBus::regHandler(EventDomain::NETWORK, &onEventNetworkBus, nullptr);
        EventBus::regHandler(EventDomain::STORAGE, &onEventStorageBus, nullptr);
        EventBus::regHandler(EventDomain::READY, &onEventReadyBus, nullptr);
        if (startAP() != ESP_OK){
            ESP_LOGE(TAG, "Falha ao criar AP");
            return ESP_FAIL;
        }else{
            vTaskDelay(pdMS_TO_TICKS(300));
            EventBus::post(EventDomain::READY, EventId::NET_READY);
            ESP_LOGI(TAG, "→ NET_READY publicado");
        }
        ESP_LOGI(TAG, "NetManager inicializado (AP ativo, NET_READY enviado)");
        return ESP_OK;
    }
}