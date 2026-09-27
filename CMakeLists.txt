cmake_minimum_required(VERSION 3.16)
project(PowerMeter VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql)

add_executable(powermeter WIN32
    src/main.cpp
    src/mainwindow.cpp src/mainwindow.h
    src/powermonitor.cpp src/powermonitor.h
    src/sessionstore.cpp src/sessionstore.h
    src/livegraph.cpp src/livegraph.h
    src/energycalendar.cpp src/energycalendar.h
    src/settings.cpp src/settings.h
)

target_link_libraries(powermeter PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
)

install(TARGETS powermeter RUNTIME DESTINATION bin)
