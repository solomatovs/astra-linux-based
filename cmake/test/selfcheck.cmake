# cmake -P script: проверяет интерпретатор cmake без генератора/компилятора
# (runtime-образ cmake содержит только сам cmake).
message(STATUS "cmake version = ${CMAKE_VERSION}")

math(EXPR sum "1 + 2 + 3 + 4")
if(NOT sum EQUAL 10)
    message(FATAL_ERROR "math broken: 1+2+3+4 = ${sum}")
endif()

string(TOUPPER "astra" up)
if(NOT up STREQUAL "ASTRA")
    message(FATAL_ERROR "string(TOUPPER) broken: ${up}")
endif()

if(CMAKE_VERSION VERSION_LESS "3.20")
    message(FATAL_ERROR "cmake too old for LLVM 16+: ${CMAKE_VERSION}")
endif()

message(STATUS "selfcheck OK")
