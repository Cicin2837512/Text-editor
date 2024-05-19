cmake_minimum_required(VERSION 3.5)
project(text-editor VERSION 0.1.0 LANGUAGES C)

include(CTest)
enable_testing()

add_executable(text-editor main.c)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
