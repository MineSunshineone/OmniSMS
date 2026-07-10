cmake_minimum_required(VERSION 3.22.1)
project(omnisms_android LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(OMNISMS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)
add_subdirectory("${OMNISMS_ROOT}/core" "${CMAKE_CURRENT_BINARY_DIR}/omnisms_core")

add_library(omnisms_android SHARED native_bridge.cpp)
target_link_libraries(omnisms_android PRIVATE omnisms_core log)
