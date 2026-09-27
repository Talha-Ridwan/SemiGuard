cmake_minimum_required(VERSION 3.20)
project(SemiGuardCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

enable_testing()

add_executable(SemiGuardApp
    src/main.cpp
)

target_include_directories(SemiGuardApp PRIVATE include)
target_link_libraries(SemiGuardApp PRIVATE Threads::Threads)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
set(CMAKE_AUTOMOC ON)

add_executable(SemiGuardDashboard
    gui/main.cpp
    gui/DashboardWindow.cpp
    gui/SecsClient.cpp
    gui/WaferMapWidget.cpp
    gui/TelemetryPlotWidget.cpp
)

target_include_directories(SemiGuardDashboard PRIVATE include gui)
target_link_libraries(SemiGuardDashboard PRIVATE Qt6::Widgets Qt6::Network Threads::Threads)

add_executable(SemiGuardTests
    tests/Test_StateMachine.cpp
    tests/Test_SafeQueue.cpp
    tests/Test_Simulators.cpp
    tests/Test_SecsParser.cpp
)

target_include_directories(SemiGuardTests PRIVATE include)
target_link_libraries(SemiGuardTests PRIVATE Catch2::Catch2WithMain Threads::Threads)

include(CTest)
include(Catch)
# Blocking pop() has no timeout, so cap each test as a hang safety net.
catch_discover_tests(SemiGuardTests PROPERTIES TIMEOUT 30)
