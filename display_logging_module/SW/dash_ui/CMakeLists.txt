add_library(dash_signal_backend STATIC
    src/backend/fake_signal_source.cpp
    src/backend/fake_signal_source.h
    src/backend/signal_data_source.h
    src/backend/signal_filter_model.cpp
    src/backend/signal_filter_model.h
    src/backend/signal_list_model.cpp
    src/backend/signal_list_model.h
)

target_include_directories(dash_signal_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(dash_signal_backend PUBLIC vehicle_signal_contract Qt6::Core)

qt_add_executable(miata_dash
    src/main.cpp
)

qt_add_qml_module(miata_dash
    URI Miata.Dash
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/DiagnosticsPage.qml
    RESOURCES
        qtquickcontrols2.conf
    RESOURCE_PREFIX /qt/qml
)

qt_add_resources(miata_dash dash_configuration
    PREFIX "/dash"
    BASE "../config"
    FILES
        ../config/logging.json
)

target_link_libraries(miata_dash PRIVATE
    dash_signal_backend
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

set_target_properties(miata_dash PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
