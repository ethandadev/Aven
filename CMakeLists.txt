cmake_minimum_required(VERSION 3.21)

project(Aven
    VERSION 0.1.0
    DESCRIPTION "A beginner-friendly 2D/3D game engine"
    LANGUAGES C CXX)

# The version the editor and player show. Release builds pass the tag's version (-DAVEN_VERSION=0.2.0).
set(AVEN_VERSION "${PROJECT_VERSION}" CACHE STRING "Version shown by the editor and player")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(EMSCRIPTEN)
    # A browser build is just the player (the web version of exported games).
    set(AVEN_BUILD_EDITOR OFF CACHE BOOL "" FORCE)
    set(AVEN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(AVEN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
endif()
option(AVEN_BUILD_EDITOR "Build the Aven editor" ON)
option(AVEN_BUILD_PLAYER "Build the standalone game player" ON)
option(AVEN_BUILD_TESTS "Build unit tests" ON)
option(AVEN_BUILD_EXAMPLES "Build native module examples" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(AvenDependencies)
include(AvenEmbed)

add_subdirectory(engine)

if(AVEN_BUILD_PLAYER)
    add_subdirectory(player)
endif()

if(AVEN_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

if(AVEN_BUILD_EXAMPLES)
    add_subdirectory(sdk/examples)
endif()

if(AVEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
