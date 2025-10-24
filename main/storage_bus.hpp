#pragma once
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
struct config{char APssid[17];char STAssid[51];char STApassword[21];char CTRname[21];char CTRtoken[51];char CTRpassword[21];uint8_t CTRflag;};
namespace StorageBus {
    extern config* configCentral;
    esp_err_t init();
}