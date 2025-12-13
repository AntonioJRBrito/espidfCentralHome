idf_component_register(
    SRCS
        "main.cpp"
        "event_bus.cpp"
        "net_manager.cpp"
        "sync_manager.cpp"
        "storage_manager.cpp"
        "storage.cpp"
        "socket_manager.cpp"
        "web_manager.cpp"
        "udp_manager.cpp"
        "broker_manager.cpp"
        "mqtt_manager.cpp"
        "matter_manager.cpp"
        "automation_manager.cpp"
        "ota_manager.cpp"
        "device_manager.cpp"
        "ble_manager.cpp"
        "rtc_manager.cpp"
        # "littlefs_console.cpp"
       
    INCLUDE_DIRS
        "."
        "../components/dns_server/include"
        "../components/qrcode"
        
    REQUIRES
        esp_common
        nvs_flash
        esp_wifi
        esp_http_server
        esp_eth
        cjson
        driver
        espressif__touch_element
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++20
    -Wno-return-type
    -Wno-missing-field-initializers
    -Wno-unused-function
)