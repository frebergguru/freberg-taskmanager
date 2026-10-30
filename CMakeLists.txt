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
    platform/linux/cpu_memory.cpp
    platform/linux/disk_network.cpp
    platform/linux/gpu_thermal_power.cpp
    platform/linux/processes.cpp
    platform/linux/linux_backend.cpp
)
target_include_directories(fg_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Sql)
set(CMAKE_AUTOMOC ON)

add_library(fg_appearance STATIC
    app/style/scheme.cpp
    app/style/style.cpp
)
target_link_libraries(fg_appearance PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
target_include_directories(fg_appearance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(freberg-taskmanager
    app/main.cpp
    app/format.cpp
    app/hub.cpp
    app/settings.cpp
    app/style/appearance.cpp
    app/shell/mainwindow.cpp
    app/shell/options.cpp
    app/shell/sheet.cpp
    app/shell/titlebar.cpp
    app/widgets/graph.cpp
    app/widgets/icons.cpp
    app/widgets/infogrid.cpp
    app/system/dbus.cpp
    app/views/views.cpp
    app/views/apphistory.cpp
    app/views/services.cpp
    app/views/startup.cpp
    app/views/sysinfo.cpp
    app/views/users.cpp
    app/views/inspector.cpp
    app/views/performance.cpp
    app/views/processes.cpp
    app/views/processmodel.cpp
    app/views/summary.cpp
    app/shell/view.h
    app/views/placeholder.h
)
target_compile_definitions(freberg-taskmanager PRIVATE FG_VERSION="${PROJECT_VERSION}")
target_link_libraries(freberg-taskmanager PRIVATE fg_core fg_appearance Qt6::Widgets Qt6::DBus Qt6::Sql)

option(FG_BUILD_TESTS "Build the tests" ON)
if(FG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
