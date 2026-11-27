cmake_minimum_required(VERSION 3.21)
# Oldest macOS the .app runs on (written to Info.plist). Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
project(SleekOCPP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# The GUI is optional so the headless server can be built on machines without Qt Widgets (e.g. Linux servers).
option(SLEEKOCPP_BUILD_GUI "Build the Qt Widgets GUI" ON)
option(SLEEKOCPP_BUILD_TESTS "Build unit tests" ON)

set(QT_COMPONENTS Core Network WebSockets Sql HttpServer)
if(SLEEKOCPP_BUILD_GUI)
    list(APPEND QT_COMPONENTS Widgets)
endif()
if(SLEEKOCPP_BUILD_TESTS)
    list(APPEND QT_COMPONENTS Test)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS ${QT_COMPONENTS})

add_subdirectory(src)

if(SLEEKOCPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
