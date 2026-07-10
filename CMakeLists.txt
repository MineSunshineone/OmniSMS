cmake_minimum_required(VERSION 3.16)
project(omnisms CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(OMNISMS_BUILD_TESTS "Build host unit tests" ON)
option(OMNISMS_BUILD_LINUX_PORT "Build the Linux port daemon" OFF)

add_subdirectory(core)

if(OMNISMS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(OMNISMS_BUILD_LINUX_PORT)
    add_subdirectory(ports/linux)
endif()
