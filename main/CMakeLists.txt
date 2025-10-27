idf_component_register(
    SRCS
        "main.cpp"
        "event_bus.cpp"
        "storage_bus.cpp"
        "net_manager.cpp"
        "sync_manager.cpp"
        "dns_manager.cpp"
        "storage_manager.cpp"
        "socket_manager.cpp"
        "web_manager.cpp"
        "udp_manager.cpp"
        "broker_manager.cpp"
       
    INCLUDE_DIRS
        "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++20
    -Wno-return-type
)