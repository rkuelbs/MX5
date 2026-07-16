qt_add_executable(wcm_simulator
    src/main.cpp
)

qt_add_qml_module(wcm_simulator
    URI Miata.WcmSimulator
    VERSION 1.0
    QML_FILES
        qml/Main.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(wcm_simulator PRIVATE
    vehicle_data_core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

set_target_properties(wcm_simulator PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
