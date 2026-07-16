function(add_vehicle_data_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE vehicle_data_core Qt6::Test)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_vehicle_data_test(dbc_decoder_test dbc_decoder_test.cpp)
target_compile_definitions(dbc_decoder_test PRIVATE
    MIATA_TEST_DBC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/miata.dbc"
)

add_vehicle_data_test(signal_registry_test signal_registry_test.cpp)
add_vehicle_data_test(candump_codec_test candump_codec_test.cpp)
add_vehicle_data_test(can_replay_source_test can_replay_source_test.cpp)
target_compile_definitions(can_replay_source_test PRIVATE
    MIATA_TEST_REPLAY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/ecm_sample.log"
)

add_vehicle_data_test(session_log_sink_test session_log_sink_test.cpp)
target_compile_definitions(session_log_sink_test PRIVATE
    MIATA_TEST_DBC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/miata.dbc"
)

add_vehicle_data_test(decoded_log_policy_test decoded_log_policy_test.cpp)
target_compile_definitions(decoded_log_policy_test PRIVATE
    MIATA_TEST_DBC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/miata.dbc"
    MIATA_TEST_LOGGING_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/logging.json"
)

add_vehicle_data_test(log_storage_manager_test log_storage_manager_test.cpp)

add_vehicle_data_test(async_session_logger_test async_session_logger_test.cpp)
target_link_libraries(async_session_logger_test PRIVATE ${MIATA_MDF_TARGET})
target_compile_definitions(async_session_logger_test PRIVATE
    MIATA_TEST_DBC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/miata.dbc"
)

add_vehicle_data_test(ecm_simulator_test ecm_simulator_test.cpp)
target_compile_definitions(ecm_simulator_test PRIVATE
    MIATA_TEST_DBC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/miata.dbc"
)

add_vehicle_data_test(wcm_simulator_test wcm_simulator_test.cpp)
target_compile_definitions(wcm_simulator_test PRIVATE
    MIATA_TEST_DBC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/miata.dbc"
)

add_vehicle_data_test(vn300_binary_parser_test vn300_binary_parser_test.cpp)
add_vehicle_data_test(vn300_simulator_test vn300_simulator_test.cpp)
add_vehicle_data_test(source_health_monitor_test source_health_monitor_test.cpp)

add_executable(systemd_notifier_test systemd_notifier_test.cpp)
target_link_libraries(systemd_notifier_test PRIVATE systemd_support Qt6::Test)
add_test(NAME systemd_notifier_test COMMAND systemd_notifier_test)

add_executable(service_web_test service_web_test.cpp)
target_link_libraries(service_web_test PRIVATE service_web_backend Qt6::Test)
target_compile_definitions(service_web_test PRIVATE
    MIATA_TEST_DBC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/miata.dbc"
    MIATA_TEST_LOGGING_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/logging.json"
    MIATA_TEST_DASH_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/dash.json"
)
add_test(NAME service_web_test COMMAND service_web_test)

add_executable(signal_ipc_protocol_test signal_ipc_protocol_test.cpp)
target_link_libraries(signal_ipc_protocol_test PRIVATE vehicle_signal_ipc Qt6::Test)
add_test(NAME signal_ipc_protocol_test COMMAND signal_ipc_protocol_test)

add_executable(replay_control_test replay_control_test.cpp)
target_link_libraries(replay_control_test PRIVATE vehicle_signal_ipc Qt6::Test)
add_test(NAME replay_control_test COMMAND replay_control_test)

add_executable(dash_signal_model_test dash_signal_model_test.cpp)
target_link_libraries(dash_signal_model_test PRIVATE dash_signal_backend Qt6::Test)
add_test(NAME dash_signal_model_test COMMAND dash_signal_model_test)

add_executable(dash_input_controller_test dash_input_controller_test.cpp)
target_link_libraries(dash_input_controller_test PRIVATE dash_signal_backend Qt6::Test)
add_test(NAME dash_input_controller_test COMMAND dash_input_controller_test)

add_executable(wcm_dash_integration_test wcm_dash_integration_test.cpp)
target_link_libraries(wcm_dash_integration_test
    PRIVATE dash_signal_backend vehicle_data_core Qt6::Test
)
target_compile_definitions(wcm_dash_integration_test PRIVATE
    MIATA_TEST_DBC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/miata.dbc"
)
add_test(NAME wcm_dash_integration_test COMMAND wcm_dash_integration_test)

add_executable(dash_interaction_controller_test dash_interaction_controller_test.cpp)
target_link_libraries(dash_interaction_controller_test PRIVATE dash_signal_backend Qt6::Test)
add_test(NAME dash_interaction_controller_test COMMAND dash_interaction_controller_test)

add_executable(signal_ipc_integration_test signal_ipc_integration_test.cpp)
target_link_libraries(signal_ipc_integration_test
    PRIVATE dash_signal_backend vehicle_signal_ipc Qt6::Test
)
add_test(NAME signal_ipc_integration_test COMMAND signal_ipc_integration_test)

add_executable(dash_configuration_test dash_configuration_test.cpp)
target_link_libraries(dash_configuration_test PRIVATE dash_signal_backend Qt6::Test)
target_compile_definitions(dash_configuration_test PRIVATE
    MIATA_TEST_DASH_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../config/dash.json"
)
add_test(NAME dash_configuration_test COMMAND dash_configuration_test)
