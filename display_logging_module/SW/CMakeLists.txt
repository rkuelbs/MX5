cmake_minimum_required(VERSION 3.21)

project(miata_display_logger VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Network
    Qml
    Quick
    QuickControls2
    SerialBus
    SerialPort
)
qt_policy(SET QTP0004 NEW)
find_package(Threads REQUIRED)
include(cmake/MdfDependencies.cmake)

add_subdirectory(src)
add_subdirectory(dash_ui)
add_subdirectory(wcm_simulator_ui)
add_subdirectory(service_web)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
