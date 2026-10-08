cmake_minimum_required(VERSION 4.1)
project(LearningTimeAnalizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql)

add_executable(LearningTimeAnalizer
        main.cpp
        ChartWidget.cpp
        ChartWidget.h
        Counter.cpp
        Counter.h
        Database.cpp
        Database.h
        TrackerWindow.cpp
        TrackerWindow.h
)

target_link_libraries(LearningTimeAnalizer PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Sql
)