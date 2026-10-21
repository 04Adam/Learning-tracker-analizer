cmake_minimum_required(VERSION 4.1)
project(LearningTimeAnalizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network)

# Na Androidzie qt_add_executable buduje bibliotekę i cel apk zamiast zwykłego programu.
qt_add_executable(LearningTimeAnalizer
        main.cpp
        AndroidBridge.cpp
        AndroidBridge.h
        ChartWidget.cpp
        ChartWidget.h
        Counter.cpp
        Counter.h
        Database.cpp
        Database.h
        Sync.cpp
        Sync.h
        TrackerWindow.cpp
        TrackerWindow.h
)

# Własny manifest i usługa z powiadomieniem stopera.
set_target_properties(LearningTimeAnalizer PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
)

# Ikona okna wbudowana w program.
qt_add_resources(LearningTimeAnalizer "icons"
        PREFIX "/"
        FILES icons/studytracker.png
)

# Na komputerze "cmake --install" dodaje program do menu aplikacji (domyślnie w ~/.local).
if (NOT ANDROID)
    if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "" FORCE)
    endif ()
    include(GNUInstallDirs)
    configure_file(studytracker.desktop.in studytracker.desktop @ONLY)
    install(TARGETS LearningTimeAnalizer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/studytracker.desktop"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES icons/studytracker.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES icons/studytracker.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
endif ()

target_link_libraries(LearningTimeAnalizer PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Sql
        Qt6::Network
)