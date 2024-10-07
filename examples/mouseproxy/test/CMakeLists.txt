cmake_minimum_required(VERSION 3.13)
project(test)

find_package(hidapi REQUIRED CONFIG)

add_executable(test test.c)
target_link_libraries(test hidapi::hidapi)