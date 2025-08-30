cmake_minimum_required(VERSION 3.10)
project(dtmf_compare C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-O2 -Wall")

set(SRC dtmf_compare.c)
set(TARGET dtmf_compare)

add_executable(${TARGET} ${SRC})
target_link_libraries(${TARGET} m)

# Output to build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
