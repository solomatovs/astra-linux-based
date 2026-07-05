# NONE-проект: проверяет configure-движок cmake без компилятора
# (runtime-образ cmake не содержит gcc — только сам cmake).
cmake_minimum_required(VERSION 3.20)
project(cmake_selfcheck NONE)

message(STATUS "cmake selfcheck: version = ${CMAKE_VERSION}")
message(STATUS "cmake selfcheck: system  = ${CMAKE_SYSTEM_NAME}")
