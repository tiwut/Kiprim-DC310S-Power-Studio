cmake_minimum_required(VERSION 3.20)
project(kiprim-control CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local/usr")

find_package(PkgConfig REQUIRED)
set(ENV{PKG_CONFIG_PATH} "$ENV{HOME}/.local/usr/lib64/pkgconfig:$ENV{HOME}/.local/usr/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")

pkg_check_modules(DEPS REQUIRED libadwaita-1 gtk4)

include_directories(
    src
    ${DEPS_INCLUDE_DIRS}
    "$ENV{HOME}/.local/usr/include"
)

link_directories(
    ${DEPS_LIBRARY_DIRS}
    "$ENV{HOME}/.local/usr/lib64"
)

set(SRCS
    src/main.cpp
    src/serial_port.cpp
    src/kiprim_device.cpp
    src/graph_widget.cpp
    src/sequencer.cpp
    src/data_logger.cpp
    src/app_window.cpp
)

add_executable(kiprim-control ${SRCS})

target_link_libraries(kiprim-control
    ${DEPS_LIBRARIES}
    pthread
)

target_link_options(kiprim-control PRIVATE
    "-Wl,-rpath,$ENV{HOME}/.local/usr/lib64"
)
