cmake_minimum_required(VERSION 3.5)
project(SmartMonitor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets)

# 告诉编译器去 include 目录找头文件
include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(SmartMonitor
    src/main.cpp
    src/MainWindow.cpp
    include/MainWindow.h
)

target_link_libraries(SmartMonitor Qt5::Widgets)
