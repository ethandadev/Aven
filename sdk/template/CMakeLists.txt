# Builds this game's native code. Every .c or .cpp file in src/ becomes its own library in bin/
# (so each can have its own AVEN_MODULE line), and Aven loads them all, reloading after each build.
# Code shared by several files can go in src/common/: it's built into every library.
#
# The Build button in Aven's Tools > Native Code runs this. By hand, from the project folder:
#   cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build native/build --config Release

cmake_minimum_required(VERSION 3.16)
project(game_native C CXX)

file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB COMMON CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.c ${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp)

foreach(source ${SOURCES})
    get_filename_component(name ${source} NAME_WE)
    add_library(${name} SHARED ${source} ${COMMON})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src/common)
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
    # Visual Studio and Xcode would add a Debug/Release folder; keep the library right in bin/.
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} CONFIG)
        set_target_properties(${name} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${CONFIG} ${CMAKE_CURRENT_SOURCE_DIR}/bin
            RUNTIME_OUTPUT_DIRECTORY_${CONFIG} ${CMAKE_CURRENT_SOURCE_DIR}/bin)
    endforeach()
    if(MSVC)
        target_compile_options(${name} PRIVATE /W3)
    else()
        target_compile_options(${name} PRIVATE -Wall)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(${name} PRIVATE m) # sinf, sqrtf...
    endif()
endforeach()
