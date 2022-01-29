cmake_minimum_required(VERSION 3.5)

project(static-delegation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(static-delegation main.cpp)

target_link_libraries(static-delegation -lpthread)
