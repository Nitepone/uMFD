cmake_minimum_required(VERSION 3.12)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(subprojects/pico-sdk/pico_sdk_init.cmake)

project(pico-dinput)

pico_sdk_init()

add_subdirectory(./src)
