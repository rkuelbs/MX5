add_library(dash_signal_backend STATIC
    src/backend/dash_action.h
    src/backend/dash_input_controller.cpp
    src/backend/dash_input_controller.h
    src/backend/dash_config_store.cpp
    src/backend/dash_config_store.h
    src/backend/fake_signal_source.cpp
    src/backend/fake_signal_source.h
    src/backend/ipc_signal_source.cpp
    src/backend/ipc_signal_source.h
    src/backend/navigation_controller.cpp
    src/backend/navigation_controller.h
    src/backend/signal_data_source.h
    src/backend/signal_filter_model.cpp
    src/backend/signal_filter_model.h
    src/backend/signal_history_store.cpp
    src/backend/signal_history_store.h
    src/backend/signal_list_model.cpp
    src/backend/signal_list_model.h
    src/backend/signal_value_provider.cpp
    src/backend/signal_value_provider.h
    src/backend/warning_evaluator.cpp
    src/backend/warning_evaluator.h
    src/backend/warning_manager.cpp
    src/backend/warning_manager.h
)

target_include_directories(dash_signal_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(dash_signal_backend
    PUBLIC vehicle_signal_contract vehicle_signal_ipc Qt6::Core Qt6::Network
)

qt_add_executable(miata_dash
    src/main.cpp
)

qt_add_qml_module(miata_dash
    URI Miata.Dash
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/DashMenuOverlay.qml
        qml/components/AnalogGauge.qml
        qml/components/DigitalGauge.qml
        qml/components/WarningOverlay.qml
        qml/components/HistoryChart.qml
        qml/pages/DiagnosticsPage.qml
        qml/pages/DrivePage.qml
    RESOURCES
        qtquickcontrols2.conf
    RESOURCE_PREFIX /qt/qml
)

qt_add_resources(miata_dash dash_configuration
    PREFIX "/dash"
    BASE "../config"
    FILES
        ../config/dash.json
        ../config/logging.json
)

target_link_libraries(miata_dash PRIVATE
    dash_signal_backend
    systemd_support
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

set_target_properties(miata_dash PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
