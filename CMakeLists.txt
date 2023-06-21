cmake_minimum_required(VERSION 3.10)
project(dnmd
    LANGUAGES C CXX)

option(INCLUDE_VENDORED_LIBS "Include vendored libraries (submodules) instead of discovering dependencies through packages." ON)

set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR})

if (INCLUDE_VENDORED_LIBS)
    add_subdirectory(external/dncp)
else()
    find_package(dncp REQUIRED)
endif()

add_subdirectory(src/)
add_subdirectory(test/)

