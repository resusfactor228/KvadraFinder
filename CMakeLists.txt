cmake_minimum_required(VERSION 3.16)
project(KvadraFinder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release build by default if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(kvadra_finder src/main.cpp)

target_compile_options(kvadra_finder PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Release>:-O2>
)

# pthreads required for std::thread on Linux
find_package(Threads REQUIRED)
target_link_libraries(kvadra_finder PRIVATE Threads::Threads)

install(TARGETS kvadra_finder RUNTIME DESTINATION bin)

message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "Install to : ${CMAKE_INSTALL_PREFIX}")
