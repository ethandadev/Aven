# Builds this game's native module: every .c and .cpp file in src/ goes into one library in bin/,
# which Aven loads (and reloads after each build).
#
# The Build button in Aven's Tools > Native Code runs this. By hand, from the project folder:
#   cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build native/build --config Release

cmake_minimum_required(VERSION 3.16)
project(game_native C CXX)

file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(game SHARED ${SOURCES})
target_include_directories(game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(game PROPERTIES
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
# Visual Studio and Xcode would add a Debug/Release folder; keep the library right in bin/.
foreach(config ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${config} CONFIG)
    set_target_properties(game PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${CONFIG} ${CMAKE_CURRENT_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG} ${CMAKE_CURRENT_SOURCE_DIR}/bin)
endforeach()

if(MSVC)
    target_compile_options(game PRIVATE /W3)
else()
    target_compile_options(game PRIVATE -Wall)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(game PRIVATE m) # sinf, sqrtf...
endif()
