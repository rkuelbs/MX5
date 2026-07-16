add_library(vehicle_signal_contract INTERFACE)
target_include_directories(vehicle_signal_contract INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vehicle_signal_contract INTERFACE Qt6::Core)

add_library(systemd_support STATIC
    platform/process_termination_monitor.cpp
    platform/process_termination_monitor.h
    platform/systemd_notifier.cpp
    platform/systemd_notifier.h
)
target_include_directories(systemd_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(systemd_support PUBLIC Qt6::Core)

add_library(vehicle_signal_ipc STATIC
    ipc/replay_control.cpp
    ipc/replay_control.h
    ipc/signal_ipc_protocol.cpp
    ipc/signal_ipc_protocol.h
    ipc/signal_ipc_server.cpp
    ipc/signal_ipc_server.h
)
target_include_directories(vehicle_signal_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vehicle_signal_ipc PUBLIC vehicle_signal_contract Qt6::Core Qt6::Network)

add_library(vehicle_data_core STATIC
    can/can_bus_endpoint.cpp
    can/can_bus_endpoint.h
    can/can_frame_record.h
    can/can_replay_source.cpp
    can/can_replay_source.h
    can/candump_codec.cpp
    can/candump_codec.h
    can/dbc_decoder.cpp
    can/dbc_decoder.h
    core/signal_registry.cpp
    core/signal_registry.h
    core/signal_definition.h
    core/signal_sample.h
    core/source_health_monitor.cpp
    core/source_health_monitor.h
    vn300/vn300_binary_parser.cpp
    vn300/vn300_binary_parser.h
    vn300/vn300_log_codec.cpp
    vn300/vn300_log_codec.h
    vn300/vn300_packet_record.h
    vn300/vn300_local_source.cpp
    vn300/vn300_local_source.h
    vn300/vn300_replay_source.cpp
    vn300/vn300_replay_source.h
    vn300/vn300_serial_source.cpp
    vn300/vn300_serial_source.h
    logging/async_session_logger.cpp
    logging/async_session_logger.h
    logging/decoded_log_policy.cpp
    logging/decoded_log_policy.h
    logging/log_sink.h
    logging/log_storage_manager.cpp
    logging/log_storage_manager.h
    logging/mdf4_log_sink.cpp
    logging/mdf4_log_sink.h
    logging/session_log_sink.cpp
    logging/session_log_sink.h
    sim/ecm_simulator.cpp
    sim/ecm_simulator.h
    sim/vn300_simulator.cpp
    sim/vn300_simulator.h
    sim/wcm_simulator.cpp
    sim/wcm_simulator.h
)

target_include_directories(vehicle_data_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vehicle_data_core
    PUBLIC vehicle_signal_contract Qt6::Core Qt6::Network Qt6::SerialBus Qt6::SerialPort Threads::Threads
    PRIVATE ${MIATA_MDF_TARGET}
)

add_executable(vehicle_loggerd apps/vehicle_loggerd_main.cpp)
target_link_libraries(vehicle_loggerd PRIVATE vehicle_data_core vehicle_signal_ipc systemd_support)

add_executable(ecm_can_simulator apps/ecm_can_simulator_main.cpp)
target_link_libraries(ecm_can_simulator PRIVATE vehicle_data_core)

add_executable(logger_benchmark apps/logger_benchmark_main.cpp)
target_link_libraries(logger_benchmark PRIVATE vehicle_data_core)

add_executable(vn300_simulator apps/vn300_simulator_main.cpp)
target_link_libraries(vn300_simulator PRIVATE vehicle_data_core)
