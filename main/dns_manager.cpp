#include "dns_manager.hpp"

static const char* TAG = "DnsManager";

// Estrutura simplificada do cabeçalho DNS
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount; // Número de perguntas
    uint16_t ancount; // Número de respostas
    uint16_t nscount; // Número de registros de autoridade
    uint16_t arcount; // Número de registros adicionais
};

// Estrutura simplificada de uma resposta DNS (para um registro A)
struct dns_answer {
    uint16_t name_ptr; // Ponteiro para o nome da pergunta (0xC00C)
    uint16_t type;     // Tipo de registro (1 para A record)
    uint16_t class_in; // Classe (1 para IN - Internet)
    uint32_t ttl;      // Time To Live (tempo de vida do registro)
    uint16_t rdlength; // Comprimento dos dados do recurso (4 bytes para IPv4)
    uint32_t rdata;    // Endereço IP
};

namespace DnsManager {
    static int sock = -1;
    static bool running = false;
    static TaskHandle_t taskHandle = nullptr;
    static esp_ip4_addr_t ap_ip; // Armazena o IP do AP

    static void dns_task(void*) {
        ESP_LOGI(TAG, "Servidor DNS local iniciado...");
        sockaddr_in client{};
        socklen_t clen = sizeof(client);
        uint8_t buf[512]; // Buffer para pacotes DNS

        while (running) {
            int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&client, &clen);
            if (n <= 0) continue;

            esp_ip4_addr_t client_ip_log;
            client_ip_log.addr = client.sin_addr.s_addr;
            ESP_LOGI(TAG, "Consulta DNS recebida de " IPSTR ":%d (tamanho: %d)", IP2STR(&client_ip_log), ntohs(client.sin_port), n);

            struct dns_header *dns_hdr = (struct dns_header *)buf;

            // Verifica se é uma consulta padrão para um registro A
            if ((ntohs(dns_hdr->flags) & 0x8000) == 0 && // É uma consulta (não uma resposta)
                (ntohs(dns_hdr->flags) & 0x7800) == 0 && // É uma consulta padrão
                ntohs(dns_hdr->qdcount) == 1) { // Tem exatamente uma pergunta

                // Encontra o final da seção de pergunta (QNAME)
                uint8_t *ptr = buf + sizeof(struct dns_header);
                while (*ptr != 0) { // Itera pelos rótulos do QNAME
                    ptr += *ptr + 1;
                }
                ptr += 1; // Avança o terminador nulo do QNAME

                uint16_t qtype = ntohs(*(uint16_t*)ptr); ptr += 2; // QTYPE
                uint16_t qclass = ntohs(*(uint16_t*)ptr); ptr += 2; // QCLASS

                if (qtype == 1 && qclass == 1) { // Se for um registro A (1) e classe IN (1)
                    // Constrói a resposta DNS
                    dns_hdr->flags = htons(0x8180); // Resposta de consulta padrão, sem erro, recursão desejada, recursão disponível
                    dns_hdr->ancount = htons(1);    // Uma resposta
                    dns_hdr->nscount = 0;
                    dns_hdr->arcount = 0;

                    // Anexa a seção de resposta
                    struct dns_answer *dns_ans = (struct dns_answer *)ptr;
                    dns_ans->name_ptr = htons(0xC00C); // Ponteiro para o nome da pergunta (offset 12 do início do pacote)
                    dns_ans->type = htons(1);          // Registro A
                    dns_ans->class_in = htons(1);      // Classe IN
                    dns_ans->ttl = htonl(60);          // TTL (60 segundos)
                    dns_ans->rdlength = htons(4);      // Comprimento dos dados do recurso (4 bytes para IPv4)
                    dns_ans->rdata = ap_ip.addr;       // Endereço IP do AP (já em network byte order)

                    int response_len = ptr + sizeof(struct dns_answer) - buf; // Calcula o novo comprimento do pacote

                    ESP_LOGI(TAG, "Respondendo DNS com IP " IPSTR " para " IPSTR ":%d", IP2STR(&ap_ip), IP2STR(&client_ip_log), ntohs(client.sin_port));
                    sendto(sock, buf, response_len, 0, (struct sockaddr*)&client, clen);
                } else {
                    ESP_LOGW(TAG, "Consulta DNS não suportada (QTYPE=%u, QCLASS=%u) de " IPSTR ":%d. Ignorando.", qtype, qclass, IP2STR(&client_ip_log), ntohs(client.sin_port));
                }
            } else {
                ESP_LOGW(TAG, "Consulta DNS não padrão ou inválida de " IPSTR ":%d (tamanho: %d). Ignorando.", IP2STR(&client_ip_log), ntohs(client.sin_port), n);
            }
        }
        close(sock);
        vTaskDelete(nullptr);
    }

    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*) {
        if (static_cast<EventId>(id) == EventId::NET_IFOK) {
            ESP_LOGI(TAG, "NET_IFOK → iniciando DNS...");
            esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            if (!ap_netif) {
                ESP_LOGE(TAG, "Falha ao obter netif do AP");
                return;
            }
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(ap_netif, &ip_info) != ESP_OK) {
                ESP_LOGE(TAG, "Falha ao obter IP do AP");
                return;
            }
            ap_ip = ip_info.ip; // Armazena o IP do AP

            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock < 0) {
                ESP_LOGE(TAG, "Falha ao criar socket DNS");
                return;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(53);
            addr.sin_addr.s_addr = htonl(INADDR_ANY); // Escuta em todas as interfaces

            if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                ESP_LOGE(TAG, "Falha ao fazer bind do socket DNS");
                close(sock);
                sock = -1;
                return;
            }

            running = true;
            xTaskCreate(dns_task, "dns_task", 4096, nullptr, 3, &taskHandle);
            ESP_LOGI(TAG, "DNS escutando em " IPSTR, IP2STR(&ap_ip));
            EventBus::unregHandler(EventDomain::DNS, &onNetworkEvent);
        }
    }

    esp_err_t init() {
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        EventBus::post(EventDomain::DNS, EventId::DNS_READY);
        ESP_LOGI(TAG, "DnsManager registrado, aguardando NET_IFOK");
        return ESP_OK;
    }
}
