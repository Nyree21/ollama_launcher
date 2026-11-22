# ============================================================================
#  Ollama Launcher - a Windows tray app that starts, stops and monitors a local
#  Ollama server.
#
#  Layout
#    include/<module>/   public headers of each module
#    src/<module>/       implementation
#    resources/          icon, .rc template, Qt style sheet, config template
#    docs/               screenshots and build notes
#
#  A single executable target compiles every module. The include directories of
#  all modules are listed below, so sources include headers by plain file name
#  (e.g. #include "app_config.h") and never use "../" path escapes.
# ============================================================================
cmake_minimum_required(VERSION 3.16)

project(ollama_launcher VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep the binary in a predictable place: <build>/bin/ollama_launcher.exe
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

# Qt 6 first, Qt 5.15 as a fallback.
find_package(Qt6 6.2 QUIET COMPONENTS Widgets Network)
if(Qt6_FOUND)
    set(OL_QT_MAJOR 6)
    set(OL_QT Qt6)
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Network)
    set(OL_QT_MAJOR 5)
    set(OL_QT Qt5)
endif()
message(STATUS "[ollama_launcher] Qt ${OL_QT_MAJOR} (${OL_QT})")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)

# A console window is handy while debugging but should stay closed for users.
option(OL_CONSOLE "Attach a console window for debugging" OFF)

# Embed the application icon. The .rc is generated with an absolute icon path so
# both windres and rc.exe resolve it without extra include flags.
set(OL_WIN_RC "")
if(WIN32)
    set(OL_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    set(OL_WIN_RC "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

# ---------------------------------------------------------------------------
# Sources, grouped by module
# ---------------------------------------------------------------------------
set(OL_SOURCES
    # entry point
    src/main.cpp

    # core: configuration, single instance, translation table
    src/core/app_config.cpp
    src/core/single_instance.cpp
    src/core/i18n.cpp

    # platform: read-only Win32 process / TCP queries
    src/platform/win_process_info.cpp

    # process: ollama serve lifecycle and log classification
    src/process/ollama_process.cpp

    # monitor: HTTP polling and TTFT probe
    src/monitor/ollama_monitor.cpp

    # proxy: token / TTFT statistics proxy
    src/proxy/stats_proxy.cpp

    # ui: main window, settings dialog, tray, log model
    src/ui/main_window.cpp
    src/ui/settings_dialog.cpp
    src/ui/tray_manager.cpp
    src/ui/log_model.cpp)

# Headers are listed explicitly so AUTOMOC sees the Q_OBJECT declarations.
set(OL_HEADERS
    include/core/app_config.h
    include/core/single_instance.h
    include/core/i18n.h
    include/platform/win_process_info.h
    include/process/ollama_process.h
    include/monitor/ollama_monitor.h
    include/proxy/stats_proxy.h
    include/ui/main_window.h
    include/ui/settings_dialog.h
    include/ui/tray_manager.h
    include/ui/log_model.h)

add_executable(ollama_launcher
    ${OL_SOURCES}
    ${OL_HEADERS}
    resources/app.qrc
    ${OL_WIN_RC})

target_include_directories(ollama_launcher PRIVATE
    include/core
    include/platform
    include/process
    include/monitor
    include/proxy
    include/ui)

target_link_libraries(ollama_launcher PRIVATE
    ${OL_QT}::Widgets
    ${OL_QT}::Network)

if(WIN32)
    # iphlpapi: TCP table, psapi: process memory, ws2_32: byte order helpers.
    target_link_libraries(ollama_launcher PRIVATE iphlpapi psapi ws2_32)
endif()

target_compile_definitions(ollama_launcher PRIVATE
    OL_APP_VERSION="${PROJECT_VERSION}")

if(WIN32 AND NOT OL_CONSOLE)
    set_target_properties(ollama_launcher PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS ollama_launcher RUNTIME DESTINATION .)
install(FILES resources/ollama_launcher.ini DESTINATION . RENAME ollama_launcher.ini)

message(STATUS "[ollama_launcher] binary  : ${CMAKE_BINARY_DIR}/bin/ollama_launcher.exe")
message(STATUS "[ollama_launcher] version : ${PROJECT_VERSION}")
