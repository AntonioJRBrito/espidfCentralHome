#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "driver/touch_pad.h"
#include "touch_element/touch_button.h"
#include "event_bus.hpp"
#include "storage_manager.hpp"
#include "esp_mac.h"
#include <stdio.h>
#include <string.h>

namespace DeviceManager
{
    esp_err_t init();
    static void timer_callback(void* arg);
    static void turnOn(uint8_t dev_id, uint32_t timeout_ms);
    static void turnOff(uint8_t dev_id);
    static void handlerDev(uint8_t dev_id);
    static void handlerService();
    static void init_gpios();
    static void init_touch();
    static void touch_event_cb(touch_button_handle_t handle, touch_button_message_t *msg, void *arg);
};