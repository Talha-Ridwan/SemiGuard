cmake_minimum_required(VERSION 3.20)
project(SemiGuardCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

add_executable(SemiGuardTests
    tests/Test_StateMachine.cpp
    tests/Test_SafeQueue.cpp
)

target_include_directories(SemiGuardTests PRIVATE include)
target_link_libraries(SemiGuardTests PRIVATE Catch2::Catch2WithMain Threads::Threads)

include(CTest)
include(Catch)
catch_discover_tests(SemiGuardTests)
