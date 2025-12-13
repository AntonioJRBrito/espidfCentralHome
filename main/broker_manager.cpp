#include "broker_manager.hpp"

static const char* TAG = "BrokerManager";
namespace BrokerManager {
    // --- Constantes para Autenticação Fixa ---
    const std::string FIXED_USERNAME = "iacentral";
    const std::string FIXED_PASSWORD = "iapass";
    // --- Estruturas de Dados para o Broker ---
    static std::map<std::string, int> s_device_id_to_fd;
    // Mutex para proteger o acesso a s_device_id_to_fd
    static SemaphoreHandle_t s_broker_data_mutex = NULL;
    // Função para remover um cliente e suas associações
    void removeClient(int fd) {
        if (xSemaphoreTake(s_broker_data_mutex, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Removendo cliente com FD %d", fd);
            std::string device_id_to_remove;
            bool found = false;
            for(auto const& [dev_id, client_fd]:s_device_id_to_fd){if(client_fd==fd){device_id_to_remove=dev_id;found=true;break;}}
            if(found){s_device_id_to_fd.erase(device_id_to_remove);ESP_LOGI(TAG, "Cliente com ID '%s' (FD %d) removido.",device_id_to_remove.c_str(),fd);}
            else{ESP_LOGW(TAG, "Tentativa de remover cliente FD %d que não está mapeado.", fd);}
            xSemaphoreGive(s_broker_data_mutex);
        }else{ESP_LOGE(TAG, "Falha ao adquirir mutex para remover cliente. Evitando crash.");}
        close(fd);
    }
    // Função para publicar uma mensagem para um dispositivo específico
    void publish_message_to_device(const std::string& target_device_id, const std::string& payload) {
        if (xSemaphoreTake(s_broker_data_mutex, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "Tentando publicar para Device ID '%s': '%s'", target_device_id.c_str(), payload.c_str());
            auto it = s_device_id_to_fd.find(target_device_id);
            if (it != s_device_id_to_fd.end()) {
                int target_fd = it->second;
                std::string topic = "DSP/" + target_device_id;
                // Calcular Remaining Length
                uint16_t topic_len = topic.length();
                uint16_t payload_len = payload.length();
                int remaining_length = 2 + topic_len + payload_len;
                uint8_t vbi_buffer[4];
                int vbi_bytes = 0;
                int temp_len = remaining_length;
                do {uint8_t digit = temp_len % 128;temp_len /= 128;if (temp_len > 0) {digit |= 0x80;}vbi_buffer[vbi_bytes++] = digit;} while (temp_len > 0);
                // Montar o pacote completo
                std::vector<uint8_t> publish_packet;
                publish_packet.push_back(0x30);
                // Adicionar Remaining Length (VBI)
                for (int i = 0; i < vbi_bytes; ++i) {publish_packet.push_back(vbi_buffer[i]);}
                // Adicionar Topic Name Length (Big-endian)
                publish_packet.push_back((topic_len >> 8) & 0xFF);
                publish_packet.push_back(topic_len & 0xFF);
                // Adicionar Topic Name
                for (char c : topic) {publish_packet.push_back(static_cast<uint8_t>(c));}
                // Adicionar Payload
                for (char c : payload) {publish_packet.push_back(static_cast<uint8_t>(c));}
                // Enviar o pacote
                int sent = send(target_fd, publish_packet.data(), publish_packet.size(), 0);
                if(sent<0){ESP_LOGE(TAG,"Falha PUB (FD %d)(Device '%s')(tópico '%s':%s)",target_fd,target_device_id.c_str(),topic.c_str(),strerror(errno));removeClient(target_fd);}
                else{ESP_LOGI(TAG,"PUBLISH enviado FD %d (Device ID '%s') no tópico '%s' (len %d)",target_fd,target_device_id.c_str(),topic.c_str(),sent);}
            }else{ESP_LOGW(TAG, "Device ID '%s' não encontrado ou não conectado. Mensagem não enviada.", target_device_id.c_str());}
            xSemaphoreGive(s_broker_data_mutex);
        }else{ESP_LOGE(TAG, "Falha ao adquirir mutex para publicar mensagem. Evitando crash.");}
    }
    // Retorna o comprimento e o número de bytes lidos para o comprimento
    static std::pair<int, int> decode_variable_byte_integer(const uint8_t* buffer, int* offset, int max_buffer_len) {
        int multiplier = 1;
        int value = 0;
        uint8_t encoded_byte;
        int bytes_read = 0;
        do {
            if (*offset >= max_buffer_len) {return { -1, 0 };}
            encoded_byte = buffer[*offset];
            value += (encoded_byte & 127) * multiplier;
            multiplier *= 128;
            (*offset)++;
            bytes_read++;
            if (bytes_read > 4) {return { -1, 0 };}
        } while ((encoded_byte & 128) != 0);
        return { value, bytes_read };
    }
    // Função para ler uma string MQTT
    static std::string read_mqtt_string(const uint8_t* buffer, int* offset, int max_buffer_len) {
        if (*offset + 2 > max_buffer_len) return "";
        uint16_t len = (buffer[*offset] << 8) | buffer[*offset + 1];
        *offset += 2;
        if (*offset + len > max_buffer_len) return "";
        if (len > MAX_MQTT_TOPIC_LEN) {ESP_LOGW(TAG, "String MQTT muito longa (%d bytes).", len);return "";}
        std::string str((const char*)(buffer + *offset), len);
        *offset += len;
        return str;
    }
    // Função para construir e enviar um pacote CONNACK
    static void send_connack(int fd, uint8_t return_code) {
        uint8_t connack_packet[4];
        connack_packet[0]=0x20;connack_packet[1]=0x02;connack_packet[2]=0x00;connack_packet[3]=return_code;
        int sent = send(fd, connack_packet, sizeof(connack_packet), 0);
        if (sent < 0) {ESP_LOGE(TAG, "Falha ao enviar CONNACK para FD %d: %s", fd, strerror(errno));}
        else {ESP_LOGI(TAG, "CONNACK enviado para FD %d com código %d", fd, return_code);}
    }
    // Função para construir e enviar um pacote SUBACK
    static void send_suback(int fd, uint16_t packet_id, uint8_t return_code) {
        uint8_t suback_packet[5];
        suback_packet[0]=0x90;suback_packet[1]=0x03;suback_packet[2]=(packet_id >> 8) & 0xFF;suback_packet[3]=packet_id & 0xFF;suback_packet[4]=return_code;
        int sent = send(fd, suback_packet, sizeof(suback_packet), 0);
        if (sent < 0) {ESP_LOGE(TAG, "Falha ao enviar SUBACK para FD %d (Packet ID %d): %s", fd, packet_id, strerror(errno));}
        else {ESP_LOGI(TAG, "SUBACK enviado para FD %d (Packet ID %d) com código %d", fd, packet_id, return_code);}
    }
    // --- Tarefa para lidar com um cliente MQTT individual ---
    void client_handler_task(void *pvParameters) {
        int client_fd = (int)pvParameters;
        std::string client_devsen_id;
        bool authenticated = false;
        int keep_alive_seconds = 0;
        int recv_timeout_ms = 5000;
        std::vector<uint8_t> rx_buffer(512); 
        int current_buffer_len = 0;
        struct timeval tv;
        tv.tv_sec = recv_timeout_ms / 1000;
        tv.tv_usec = (recv_timeout_ms % 1000) * 1000;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        int bytes_read = recv(client_fd, rx_buffer.data(), rx_buffer.size(), 0);
        if (bytes_read <= 0) {
            ESP_LOGW(TAG, "FD %d: Erro ou timeout ao receber CONNECT: %s", client_fd, (bytes_read == 0 ? "Conexão fechada" : strerror(errno)));
            removeClient(client_fd);
            vTaskDelete(NULL);
            return;
        }
        current_buffer_len = bytes_read;
        int offset = 0;
        uint8_t fixed_header = rx_buffer[offset++];
        uint8_t packet_type = (fixed_header >> 4) & 0x0F;
        if (packet_type != 0x01) {
            ESP_LOGW(TAG, "FD %d: Primeiro pacote não é CONNECT (tipo %d). Desconectando.", client_fd, packet_type);
            send_connack(client_fd, 0x01);
            removeClient(client_fd);
            vTaskDelete(NULL);
            return;
        }
        auto rl_result = decode_variable_byte_integer(rx_buffer.data(), &offset, current_buffer_len);
        int remaining_length = rl_result.first;
        if (remaining_length == -1 || (offset + remaining_length > current_buffer_len)) {
            ESP_LOGW(TAG, "FD %d: Remaining Length inválido ou buffer insuficiente para CONNECT. Desconectando.", client_fd);
            send_connack(client_fd, 0x01);
            removeClient(client_fd);
            vTaskDelete(NULL);
            return;
        }
        std::string protocol_name = read_mqtt_string(rx_buffer.data(), &offset, current_buffer_len);
        if (protocol_name != "MQTT") {
            ESP_LOGW(TAG, "FD %d: Protocol Name inválido ('%s'). Desconectando.", client_fd, protocol_name.c_str());
            send_connack(client_fd, 0x01);
            removeClient(client_fd);
            vTaskDelete(NULL);
            return;
        }
        uint8_t protocol_level = rx_buffer[offset++];
        if (protocol_level != 0x04) {
            ESP_LOGW(TAG, "FD %d: Protocol Level inválido (0x%02X). Desconectando.", client_fd, protocol_level);
            send_connack(client_fd, 0x01);
            removeClient(client_fd);
            vTaskDelete(NULL);
            return;
        }
        uint8_t connect_flags = rx_buffer[offset++];
        // bool clean_session = (connect_flags >> 1) & 0x01;  //mantenho?
        // bool will_flag = (connect_flags >> 2) & 0x01;      //mantenho?
        // uint8_t will_qos = (connect_flags >> 3) & 0x03;
        // bool will_retain = (connect_flags >> 5) & 0x01;    //mantenho?
        bool password_flag = (connect_flags >> 6) & 0x01;
        bool username_flag = (connect_flags >> 7) & 0x01;
        keep_alive_seconds = (rx_buffer[offset] << 8) | rx_buffer[offset + 1];
        offset += 2;
        std::string client_id_mqtt = read_mqtt_string(rx_buffer.data(), &offset, current_buffer_len);
        if (client_id_mqtt.empty() || client_id_mqtt.length() > MAX_MQTT_CLIENT_ID_LEN) {
            ESP_LOGW(TAG, "FD %d: Client ID inválido ou muito longo (%d bytes). Desconectando.", client_fd, client_id_mqtt.length());
            send_connack(client_fd, 0x02);
            removeClient(client_fd);
            vTaskDelete(NULL);
            return;
        }
        client_devsen_id = client_id_mqtt;
        if (client_devsen_id.length() > MAX_DEVICE_ID_LEN) {
            ESP_LOGW(TAG, "FD %d: Device ID '%s' excede MAX_DEVICE_ID_LEN (%d).",client_fd, client_devsen_id.c_str(), MAX_DEVICE_ID_LEN);
            send_connack(client_fd, 0x02);
            removeClient(client_fd);
            vTaskDelete(NULL);
            return;
        }
        // if (will_flag) {  //mantenho?
        //     std::string will_topic = read_mqtt_string(rx_buffer.data(), &offset, current_buffer_len);
        //     std::string will_message = read_mqtt_string(rx_buffer.data(), &offset, current_buffer_len);
        //     ESP_LOGD(TAG, "FD %d: Will Topic: '%s', Will Message: '%s' (ignorado)", client_fd, will_topic.c_str(), will_message.c_str());
        // }
        std::string username_rx;
        if (username_flag) {username_rx = read_mqtt_string(rx_buffer.data(), &offset, current_buffer_len);}
        std::string password_rx;
        if (password_flag) {password_rx = read_mqtt_string(rx_buffer.data(), &offset, current_buffer_len);}
        if (username_rx != FIXED_USERNAME || password_rx != FIXED_PASSWORD) {
            ESP_LOGW(TAG, "(FD %d)(ID '%s'):Autenticação falhou (User:'%s')(Pass:'%s')",client_fd,client_devsen_id.c_str(),username_rx.c_str(),password_rx.c_str());
            send_connack(client_fd, 0x04);
            removeClient(client_fd);
            vTaskDelete(NULL);
            return;
        }
        if (xSemaphoreTake(s_broker_data_mutex, portMAX_DELAY) == pdTRUE) {
            auto it = s_device_id_to_fd.find(client_devsen_id);
            if (it != s_device_id_to_fd.end()) {
                ESP_LOGW(TAG, "ID '%s' já conectado (FD %d). Desconectando o antigo (FD %d).",client_devsen_id.c_str(),it->second, client_fd);
                close(it->second);
                s_device_id_to_fd.erase(it);
            }
            s_device_id_to_fd[client_devsen_id] = client_fd;
            authenticated = true;
            ESP_LOGI(TAG, "(ID '%s')(FD %d) autenticado e registrado.", client_devsen_id.c_str(), client_fd);
            xSemaphoreGive(s_broker_data_mutex);
        } else {
            ESP_LOGE(TAG, "Falha ao adquirir mutex para registrar ID '%s'. Desconectando.", client_devsen_id.c_str());
            send_connack(client_fd, 0x05);
            removeClient(client_fd);
            vTaskDelete(NULL);
            return;
        }
        // segundo protocolo, 1s +/- 0,5s. Posso colocar 2s?
        send_connack(client_fd, 0x00);
        recv_timeout_ms = (keep_alive_seconds * 1000) + (keep_alive_seconds * 1000 / 2);
        if (recv_timeout_ms == 0) recv_timeout_ms = portMAX_DELAY;
        tv.tv_sec = recv_timeout_ms / 1000;
        tv.tv_usec = (recv_timeout_ms % 1000) * 1000;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        while (authenticated) {
            bytes_read = recv(client_fd, rx_buffer.data(), rx_buffer.size(), 0);
            if (bytes_read <= 0) {
                if (bytes_read == 0) {ESP_LOGI(TAG, "FD %d (ID '%s'): Conexão fechada pelo cliente.", client_fd, client_devsen_id.c_str());}
                else if(errno==EWOULDBLOCK||errno==EAGAIN){ESP_LOGW(TAG,"(FD %d)(ID '%s'):Timeout de Keep Alive. Desconectando.",client_fd,client_devsen_id.c_str());}
                else {ESP_LOGE(TAG, "FD %d (ID '%s'): Erro de recv: %s. Desconectando.", client_fd, client_devsen_id.c_str(), strerror(errno));}
                authenticated = false;
                break;
            }
            current_buffer_len = bytes_read;
            offset = 0;
            fixed_header = rx_buffer[offset++];
            packet_type = (fixed_header >> 4) & 0x0F;
            rl_result = decode_variable_byte_integer(rx_buffer.data(), &offset, current_buffer_len);
            remaining_length = rl_result.first;
            if (remaining_length == -1 || (offset + remaining_length > current_buffer_len)) {
                ESP_LOGW(TAG,"FD %d (ID '%s'):RLength inválido ou buffer pequeno para pacote tipo %d. Desconectando.",client_fd,client_devsen_id.c_str(),packet_type);
                authenticated = false;
                break;
            }
            switch (packet_type) {
                case 0x03: {
                    ESP_LOGI(TAG, "PUBLISH recebido de FD %d (Device ID '%s').", client_fd, client_devsen_id.c_str());
                    std::string topic = read_mqtt_string(rx_buffer.data(), &offset, current_buffer_len);
                    std::string payload((const char*)(rx_buffer.data() + offset), remaining_length - (topic.length() + 2));
                    offset += payload.length();
                    ESP_LOGI(TAG, "PUBLISH de '%s': Tópico '%s', Payload '%s'",client_devsen_id.c_str(), topic.c_str(), payload.c_str());
                    std::string expected_topic_prefix = "CTR/";
                    if (topic.rfind(expected_topic_prefix, 0) == 0 && topic.length() > expected_topic_prefix.length()) {
                        std::string received_device_id_from_topic = topic.substr(expected_topic_prefix.length());
                        if (received_device_id_from_topic == client_devsen_id) {
                            // O payload pode ser REG, INF, SEN, SNA, ACT:<n>, BRG:<n>
                            if (payload == "REG") {
                                ESP_LOGI(TAG, "Payload REG recebido de '%s'.", client_devsen_id.c_str());
                                publish_message_to_device(received_device_id_from_topic,"INF");
                            } else if (payload.rfind("INF:", 0) == 0) {
                                ESP_LOGI(TAG, "Payload INF recebido de '%s'.", client_devsen_id.c_str());
                                std::string payload_without_prefix = payload.substr(4);
                                std::vector<std::string> parts = StorageManager::splitString(payload_without_prefix, ':');
                                if(parts.size()!=5){ESP_LOGE(TAG,"INF inválido: %s. Recebido %zu partes.",payload_without_prefix.c_str(),parts.size());return;}
                                DeviceDTO device_dto;
                                strncpy(device_dto.id, client_devsen_id.c_str(), sizeof(device_dto.id) - 1);
                                device_dto.id[sizeof(device_dto.id) - 1] = '\0';
                                device_dto.type = static_cast<uint8_t>(atoi(parts[0].c_str()));
                                strncpy(device_dto.name, parts[1].c_str(), sizeof(device_dto.name) - 1);
                                device_dto.name[sizeof(device_dto.name) - 1] = '\0';
                                device_dto.time = static_cast<uint16_t>(atoi(parts[2].c_str()));
                                device_dto.status = static_cast<uint8_t>(atoi(parts[3].c_str()));
                                device_dto.x_int = static_cast<uint8_t>(atoi(parts[4].c_str()));
                                ESP_LOGI(TAG, "INF_HANDLER:DTO para (ID '%s')(Nome='%s')(Tipo=%u)(Tempo=%u)(Status=%u)(X_Int=%u)(X_Str='%s')",device_dto.id,device_dto.name,device_dto.type,device_dto.time,device_dto.status,device_dto.x_int,device_dto.x_str);
                                RequestSave requester;
                                strncpy(requester.request_char,device_dto.id,sizeof(device_dto.id)-1);
                                requester.request_char[sizeof(requester.request_char)-1]='\0';
                                requester.requester=client_fd;
                                requester.resquest_type=RequestTypes::REQUEST_CHAR;
                                esp_err_t device_ret=StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::DEVICE_DATA,&device_dto,sizeof(DeviceDTO),requester,EventId::STO_DEVICESAVED);
                                if (device_ret != ESP_OK) {ESP_LOGE(TAG, "INF_HANDLER: Falha ao enfileirar requisição SAVE para Device '%s'", device_dto.id);}
                                else {ESP_LOGI(TAG, "INF_HANDLER: Requisição SAVE para Device '%s' enfileirada com sucesso.", device_dto.id);}
                            } else if (payload.rfind("SEN:", 0) == 0) {
                                ESP_LOGI(TAG, "Payload SEN recebido de '%s': %s", client_devsen_id.c_str(), payload.c_str());
                                std::string payload_without_prefix = payload.substr(4);
                                std::vector<std::string> parts = StorageManager::splitString(payload_without_prefix, ':');
                                if(parts.size()!=4){ESP_LOGE(TAG,"INF inválido: %s. Recebido %zu partes.",payload_without_prefix.c_str(),parts.size());return;}
                                SensorDTO sensor_dto;
                                strncpy(sensor_dto.id, client_devsen_id.c_str(), sizeof(sensor_dto.id) - 1);
                                sensor_dto.id[sizeof(sensor_dto.id) - 1] = '\0';
                                sensor_dto.type = static_cast<uint8_t>(atoi(parts[0].c_str()));
                                strncpy(sensor_dto.name, parts[1].c_str(), sizeof(sensor_dto.name) - 1);
                                sensor_dto.name[sizeof(sensor_dto.name) - 1] = '\0';
                                sensor_dto.time = static_cast<uint16_t>(atoi(parts[2].c_str()));
                                sensor_dto.x_int = static_cast<uint8_t>(atoi(parts[3].c_str()));
                                ESP_LOGI(TAG, "SEN_HANDLER:DTO para (ID '%s')(Nome='%s')(Tipo=%u)(Tempo=%u)(X_Int=%u)(X_Str='%s')",sensor_dto.id,sensor_dto.name,sensor_dto.type,sensor_dto.time,sensor_dto.x_int,sensor_dto.x_str);
                                RequestSave requester;
                                strncpy(requester.request_char,sensor_dto.id,sizeof(sensor_dto.id)-1);
                                requester.request_char[sizeof(requester.request_char)-1]='\0';
                                requester.requester=client_fd;
                                requester.resquest_type=RequestTypes::REQUEST_CHAR;
                                esp_err_t sensor_ret=StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::SENSOR_DATA,&sensor_dto,sizeof(SensorDTO),requester,EventId::STO_SENSORSAVED);
                                if (sensor_ret != ESP_OK) {ESP_LOGE(TAG, "INF_HANDLER: Falha ao enfileirar requisição SAVE para Device '%s'", sensor_dto.id);}
                                else {ESP_LOGI(TAG, "INF_HANDLER: Requisição SAVE para Device '%s' enfileirada com sucesso.", sensor_dto.id);}
                            } else if (payload.rfind("SNA:", 0) == 0) {
                                //depois eu vejo o que recebe
                                ESP_LOGI(TAG, "Payload SNA recebido de '%s'.", client_devsen_id.c_str());
                                PublishBrokerData pub_data("123456654321","ACT:1");
                                EventBus::post(EventDomain::BROKER, EventId::BRK_PUBLISHREQUEST,&pub_data,sizeof(pub_data));
                            } else {
                                ESP_LOGW(TAG, "Payload desconhecido '%s' de '%s'.", payload.c_str(), client_devsen_id.c_str());
                            }
                        }else{ESP_LOGW(TAG, "ID no tóp('%s') não corresponde ao Client ID('%s'). Ignor PUB.",received_device_id_from_topic.c_str(),client_devsen_id.c_str());}
                    }else{ESP_LOGW(TAG, "Tópico de PUBLISH inválido ('%s') de '%s'. Esperado 'CTR/<id>'.", topic.c_str(), client_devsen_id.c_str());}
                    break;
                }
                case 0x08: {
                    ESP_LOGI(TAG, "SUBSCRIBE recebido de FD %d (Device ID '%s').", client_fd, client_devsen_id.c_str());
                    // fazer o Parsing do pacote SUBSCRIBE
                    uint16_t packet_id = (rx_buffer[offset] << 8) | rx_buffer[offset + 1];
                    offset += 2;
                    std::string requested_topic = read_mqtt_string(rx_buffer.data(), &offset, current_buffer_len);
                    ESP_LOGI(TAG, "SUBSCRIBE de '%s': Tópico '%s', Packet ID %d",client_devsen_id.c_str(), requested_topic.c_str(), packet_id);
                    std::string expected_subscribe_topic = "DSP/" + client_devsen_id;
                    if(requested_topic==expected_subscribe_topic){send_suback(client_fd,packet_id,0x00);ESP_LOGI(TAG,"SUBSCRIBE '%s' aceito.",requested_topic.c_str());}
                    else{send_suback(client_fd,packet_id,0x80);ESP_LOGW(TAG,"SUBSCRIBE inválido (tóp.'%s').",requested_topic.c_str());}
                    break;
                }
                case 0x0C: {
                    ESP_LOGI(TAG, "PINGREQ de FD %d (Device ID '%s'). PINGRESP.", client_fd, client_devsen_id.c_str());
                    uint8_t pingresp_packet[] = {0xD0,0x00};
                    send(client_fd, pingresp_packet,sizeof(pingresp_packet), 0);
                    break;
                }
                case 0x0E: {
                    ESP_LOGI(TAG, "DISCONNECT recebido de FD %d (Device ID '%s').", client_fd, client_devsen_id.c_str());
                    authenticated = false;
                    break;
                }
                case 0x0A: {
                    ESP_LOGI(TAG, "UNSUBSCRIBE recebido de FD %d (Device ID '%s'). Ignorado no modelo 1:1.", client_fd, client_devsen_id.c_str());
                    break;
                }
                default: {
                    ESP_LOGW(TAG, "Pacote MQTT desconhecido (tipo %d) de FD %d (Device ID '%s'). Desconectando.", packet_type, client_fd, client_devsen_id.c_str());
                    authenticated = false;
                    break;
                }
            }
        }
        removeClient(client_fd);
        vTaskDelete(NULL);
    }
    // --- Tarefa que escuta por novas conexões TCP para o broker ---
    void broker_listener_task(void *pvParameters) {
        char addr_str[128];
        int addr_family = AF_INET;
        int ip_protocol = IPPROTO_IP;
        int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (listen_sock < 0) {ESP_LOGE(TAG, "Falha ao criar socket: %s", strerror(errno));vTaskDelete(NULL);return;}
        int opt = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(1884);
        int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {ESP_LOGE(TAG, "Socket não fez bind: %s", strerror(errno));close(listen_sock);vTaskDelete(NULL);return;}
        ESP_LOGI(TAG, "Socket bound to port %d", 1884);
        err = listen(listen_sock, 5);
        if (err != 0) {ESP_LOGE(TAG, "Error ouvindo: %s", strerror(errno));close(listen_sock);vTaskDelete(NULL);return;}
        ESP_LOGI(TAG, "Socket listening");
        while (true) {
            ESP_LOGI(TAG, "Aguardando nova conexão...");
            struct sockaddr_storage source_addr;
            socklen_t addr_len = sizeof(source_addr);
            int client_fd = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            if (client_fd < 0) {ESP_LOGE(TAG, "Unable to accept connection: %s", strerror(errno));continue;}
            if (source_addr.ss_family == AF_INET) {inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);}
            ESP_LOGI(TAG, "Cliente conectado: FD %d, IP: %s", client_fd, addr_str);
            xTaskCreate(client_handler_task, "mqtt_client_handler", 4096, (void*)client_fd, 5, NULL);
        }
        close(listen_sock);
        vTaskDelete(NULL);
    }
    // --- Handler de Eventos do Broker ---
    static void onEventBrokerBus(void*, esp_event_base_t base, int32_t id, void* data) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::BRK_PUBLISHREQUEST) {
            ESP_LOGI(TAG, "Evento BRK_PUBLISHREQUEST recebido.");
            PublishBrokerData* pub_data = static_cast<PublishBrokerData*>(data);
            if(pub_data){publish_message_to_device(pub_data->device_id,pub_data->payload);}
            else{ESP_LOGE(TAG, "BRK_PUBLISHREQUEST com dados inválidos.");}
        }
    }
    // --- Handler de Eventos do Network ---
    static void onEventNetworkBus(void*, esp_event_base_t base, int32_t id, void* data) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_STAGOTIP) {
            ESP_LOGI(TAG, "Evento NET_STAGOTIP recebido. Criando tarefa do Broker Listener.");
            s_broker_data_mutex = xSemaphoreCreateMutex();
            if (s_broker_data_mutex == NULL) {ESP_LOGE(TAG, "Falha ao criar mutex para dados do broker!");return;}
            xTaskCreate(broker_listener_task, "mqtt_broker_listener", 4096, NULL, 5, NULL);
        }
    }
    // --- Função de Inicialização do Módulo BrokerManager ---
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando BrokerManager...");
        EventBus::regHandler(EventDomain::NETWORK, &onEventNetworkBus, nullptr);
        EventBus::regHandler(EventDomain::BROKER, &onEventBrokerBus, nullptr);
        EventBus::post(EventDomain::READY, EventId::BRK_READY);
        ESP_LOGI(TAG, "→ BRK_READY publicado");
        return ESP_OK;
    }
}