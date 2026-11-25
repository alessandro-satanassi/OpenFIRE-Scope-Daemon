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
    src/app/Status.h
    src/serial/NativeSerial.h
    src/dongle/EncodedFrame.h
    src/dongle/UsbFrame.h
    src/dongle/UsbFrame.cpp
    src/dongle/DongleLink.h
    src/dongle/DongleLink.cpp
    src/dongle/DongleManager.h
    src/dongle/DongleManager.cpp
    src/clip/ClipPlayer.h
    src/clip/ClipPlayer.cpp
    src/udp/SlotInput.h
    src/udp/SlotInput.cpp
    src/udp/NativeUdp.h
    src/udp/UdpReceiver.h
    src/udp/UdpReceiver.cpp
    src/udp/UdpInput.h
    src/udp/UdpInput.cpp
    src/udp/Discovery.h
    src/udp/Discovery.cpp
    src/video/ScopeEncoder.h
    src/video/ScopeEncoder.cpp
    src/video/RateControl.h
    src/video/RateControl.cpp
    src/video/HiResTimer.h
    src/video/SlotEncoder.h
    src/video/SlotEncoder.cpp
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

# EN: native code of one system only. / IT: codice nativo di un solo sistema.
if(WIN32)
    list(APPEND PROJECT_SOURCES resources/icon.rc src/serial/NativeSerial_win.cpp src/udp/NativeUdp_win.cpp src/video/HiResTimer_win.cpp)
else()
    list(APPEND PROJECT_SOURCES src/serial/NativeSerial_posix.cpp src/udp/NativeUdp_posix.cpp src/video/HiResTimer_posix.cpp)
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

# EN: bench tool, not installed: fake game that sends test images over UDP (tools/of_test_sender).
# IT: strumento per il banco, non installato: finto gioco che manda immagini di prova in UDP (tools/of_test_sender).
option(OFD_BUILD_TOOLS "Build tools/of_test_sender" ON)
if(OFD_BUILD_TOOLS)
    qt_add_executable(of_test_sender tools/of_test_sender/main.cpp)
    target_include_directories(of_test_sender PRIVATE protocol)
    target_link_libraries(of_test_sender PRIVATE Qt6::Core Qt6::Network)
    if(WIN32)
        target_link_libraries(of_test_sender PRIVATE winmm)
    endif()
    if(MSVC)
        target_compile_options(of_test_sender PRIVATE /W4 /utf-8)
    else()
        target_compile_options(of_test_sender PRIVATE -Wall -Wextra)
    endif()
    set_target_properties(of_test_sender PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE FALSE)
endif()

include(GNUInstallDirs)
install(TARGETS OpenFIRE-Scope-Daemon
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(OpenFIRE-Scope-Daemon)
