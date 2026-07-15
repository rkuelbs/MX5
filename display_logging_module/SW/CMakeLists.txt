cmake_minimum_required(VERSION 3.21)

project(miata_display_logger VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core SerialBus SerialPort)
find_package(Threads REQUIRED)
include(cmake/MdfDependencies.cmake)

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
