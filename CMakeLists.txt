cmake_minimum_required(VERSION 3.21)
project(freberg-taskmanager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)

add_library(fg_core STATIC
    core/parse.cpp
    core/smbios.cpp
    core/diskscan.cpp
    core/benchmark.cpp
    core/trace.cpp
    platform/headless.cpp
    platform/linux/cpu_memory.cpp
    platform/linux/disk_network.cpp
    platform/linux/gpu_thermal_power.cpp
    platform/linux/processes.cpp
    platform/linux/connections.cpp
    platform/linux/wifi.cpp
    platform/linux/details.cpp
    platform/linux/linux_backend.cpp
)
target_include_directories(fg_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Sql LinguistTools)
set(CMAKE_AUTOMOC ON)

add_library(fg_appearance STATIC
    app/style/scheme.cpp
    app/style/style.cpp
    app/style/style_modern.cpp
)
target_link_libraries(fg_appearance PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
target_include_directories(fg_appearance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(fg_app STATIC
    app/format.cpp
    app/hub.cpp
    app/recorder.cpp
    app/settings.cpp
    app/style/appearance.cpp
    app/popout/popout.cpp
    app/popout/skin.cpp
    app/shell/accessible.cpp
    app/shell/mainwindow.cpp
    app/shell/menubar.cpp
    app/shell/screensaver.cpp
    app/shell/tray.cpp
    app/shell/options.cpp
    app/shell/sheet.cpp
    app/shell/titlebar.cpp
    app/widgets/graph.cpp
    app/widgets/icons.cpp
    app/widgets/infogrid.cpp
    app/widgets/resultsdb.cpp
    app/system/dbus.cpp
    app/views/views.cpp
    app/views/apphistory.cpp
    app/views/benchmarks.cpp
    app/views/connections.cpp
    app/views/diskspace.cpp
    app/views/drivers.cpp
    app/views/flightrecorder.cpp
    app/views/installedapps.cpp
    app/views/powerfreq.cpp
    app/views/services.cpp
    app/views/startup.cpp
    app/views/sysinfo.cpp
    app/views/thermals.cpp
    app/views/users.cpp
    app/views/inspector.cpp
    app/views/performance.cpp
    app/views/processes.cpp
    app/views/processmodel.cpp
    app/views/summary.cpp
    app/shell/view.h
)
target_link_libraries(fg_app PUBLIC fg_core fg_appearance Qt6::Widgets Qt6::DBus Qt6::Sql)
qt_add_resources(fg_app "legal" PREFIX "/legal" FILES LICENSE)

add_executable(freberg-taskmanager app/main.cpp)
target_compile_definitions(freberg-taskmanager PRIVATE FG_VERSION="${PROJECT_VERSION}")
target_link_libraries(freberg-taskmanager PRIVATE fg_app)

qt_add_translations(freberg-taskmanager
    TS_FILES i18n/freberg-taskmanager_nb.ts
    PLURALS_TS_FILE i18n/freberg-taskmanager_en.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete
)

include(GNUInstallDirs)
install(TARGETS freberg-taskmanager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/guru.freberg.TaskManager.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/guru.freberg.TaskManager.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/freberg-taskmanager)

option(FG_BUILD_TESTS "Build the tests" ON)
if(FG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
