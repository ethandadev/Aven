file(GLOB_RECURSE RYNAX_ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(rynax_engine STATIC ${RYNAX_ENGINE_SOURCES})
target_include_directories(rynax_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/sdk/include)
target_link_libraries(rynax_engine PUBLIC rynax_stb rynax_cgltf rynax_miniaudio box2d Jolt ${CMAKE_DL_LIBS})
if(EMSCRIPTEN)
    # Browser: Emscripten's GLFW and WebGL 2, no threads (works on any web host).
    target_link_options(rynax_engine PUBLIC -sUSE_GLFW=3 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1
                        -sSTACK_SIZE=1048576)
else()
    target_link_libraries(rynax_engine PUBLIC glfw rynax_glad)
endif()
target_compile_definitions(rynax_engine PUBLIC RYNAX_VERSION="${RYNAX_VERSION}")

if(MSVC)
    target_compile_options(rynax_engine PRIVATE /W4 /wd4100 /wd4201 /wd4244 /wd4267 /wd4305 /utf-8)
    target_compile_definitions(rynax_engine PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(rynax_engine PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

if(WIN32)
    target_link_libraries(rynax_engine PUBLIC ws2_32) # multiplayer on the local network
endif()

if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(rynax_engine PUBLIC Threads::Threads)
endif()

rynax_embed_files(rynax_engine FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/data/fonts/Roboto-Medium.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/data/fonts/Cousine-Regular.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/data/icon/rynax_16.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/icon/rynax_32.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/icon/rynax_48.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/icon/rynax_64.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/icon/rynax_128.png)
