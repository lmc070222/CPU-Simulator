cmake_minimum_required(VERSION 3.10)
project(tomasulo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(main src/main.cpp)
add_executable(test src/test.cpp)
add_executable(naive_interpreter src/naive_interpreter.cpp)
