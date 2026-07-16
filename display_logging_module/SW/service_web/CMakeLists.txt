add_library(service_web_backend STATIC
    src/service_action_runner.cpp
    src/service_action_runner.h
    src/service_config_manager.cpp
    src/service_config_manager.h
    src/service_data_store.cpp
    src/service_data_store.h
    src/service_log_catalog.cpp
    src/service_log_catalog.h
    src/simple_http_server.cpp
    src/simple_http_server.h
)
target_include_directories(service_web_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(service_web_backend PUBLIC
    dash_signal_backend
    vehicle_data_core
    Qt6::Core
    Qt6::Network
)

qt_add_executable(miata_service_web
    src/main.cpp
)
qt_add_resources(miata_service_web service_web_assets
    PREFIX "/service-web"
    FILES
        static/index.html
)
target_link_libraries(miata_service_web PRIVATE
    service_web_backend
    systemd_support
    Qt6::Core
    Qt6::Network
)
