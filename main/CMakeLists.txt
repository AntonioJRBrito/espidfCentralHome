idf_component_register(
    SRCS
        "main.cpp"
        "event_bus.cpp"
        "storage_bus.cpp"
        "net_manager.cpp"
        "sync_manager.cpp"
       
    INCLUDE_DIRS
        "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++17
    -Wno-return-type
)