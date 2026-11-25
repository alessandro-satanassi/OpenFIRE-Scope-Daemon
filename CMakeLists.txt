cmake_minimum_required(VERSION 3.21)

project(OpenFIRE-Scope-Daemon
        VERSION 0.1.0
        DESCRIPTION "OpenFIRE scope daemon: video from the PC to the scope dongles"
        LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(OFD_VERSION="${PROJECT_VERSION}")
if(OFD_GITHASH)
    add_compile_definitions(OFD_GITHASH="${OFD_GITHASH}")
endif()

# EN: Qt 6 only (Network and SerialPort are used from phase 2-3). / IT: solo Qt 6 (Network e SerialPort servono dalle fasi 2-3).
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network SerialPort LinguistTools)

set(TS_FILES
    translation/ScopeDaemon_en_US.ts
    translation/ScopeDaemon_it_IT.ts
)

set(PROJECT_SOURCES
    protocol/OpenFIRE_Scope_Protocol.h
    src/main.cpp
    src/app/Daemon.h
    src/app/Daemon.cpp
    src/app/Settings.h
    src/app/Settings.cpp
    src/app/Log.h
    src/app/Log.cpp
    src/app/SingleInstance.h
    src/app/SingleInstance.cpp
    src/ui/TrayIcon.h
    src/ui/TrayIcon.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/PreviewWidget.h
    src/ui/PreviewWidget.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    resources/resources.qrc
)

if(WIN32)
    list(APPEND PROJECT_SOURCES resources/icon.rc)
endif()

qt_add_executable(OpenFIRE-Scope-Daemon MANUAL_FINALIZATION ${PROJECT_SOURCES})

qt_add_translations(OpenFIRE-Scope-Daemon TS_FILES ${TS_FILES})

target_include_directories(OpenFIRE-Scope-Daemon PRIVATE src protocol)

target_link_libraries(OpenFIRE-Scope-Daemon PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::SerialPort)

if(WIN32)
    # EN: libraries of the native parts (serial, UDP, timers, DXGI). / IT: librerie delle parti native (seriale, UDP, timer, DXGI).
    target_link_libraries(OpenFIRE-Scope-Daemon PRIVATE ws2_32 winmm d3d11 dxgi)
endif()

if(MSVC)
    target_compile_options(OpenFIRE-Scope-Daemon PRIVATE /W4 /utf-8)
else()
    target_compile_options(OpenFIRE-Scope-Daemon PRIVATE -Wall -Wextra)
endif()

set_target_properties(OpenFIRE-Scope-Daemon PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.TeamOpenFIRE.ScopeDaemon
    MACOSX_BUNDLE_BUNDLE_NAME "OpenFIRE Scope Daemon"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS OpenFIRE-Scope-Daemon
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(OpenFIRE-Scope-Daemon)
