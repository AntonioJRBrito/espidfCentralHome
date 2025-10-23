idf_component_register(
    SRCS
        "main.cpp"
        "event_bus.cpp"
       
    INCLUDE_DIRS
        "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++17
    -Wno-return-type
)