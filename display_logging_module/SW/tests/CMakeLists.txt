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

add_vehicle_data_test(async_session_logger_test async_session_logger_test.cpp)
target_link_libraries(async_session_logger_test PRIVATE ${MIATA_MDF_TARGET})
target_compile_definitions(async_session_logger_test PRIVATE
    MIATA_TEST_DBC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/miata.dbc"
)

add_vehicle_data_test(ecm_simulator_test ecm_simulator_test.cpp)
target_compile_definitions(ecm_simulator_test PRIVATE
    MIATA_TEST_DBC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/miata.dbc"
)

add_vehicle_data_test(vn300_binary_parser_test vn300_binary_parser_test.cpp)
add_vehicle_data_test(vn300_simulator_test vn300_simulator_test.cpp)
add_vehicle_data_test(source_health_monitor_test source_health_monitor_test.cpp)
