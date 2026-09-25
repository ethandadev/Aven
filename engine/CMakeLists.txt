file(GLOB_RECURSE AVEN_ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(aven_engine STATIC ${AVEN_ENGINE_SOURCES})
target_include_directories(aven_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/sdk/include)
target_link_libraries(aven_engine PUBLIC aven_stb aven_cgltf aven_miniaudio box2d Jolt ${CMAKE_DL_LIBS})
if(EMSCRIPTEN)
    # Browser: Emscripten's GLFW and WebGL 2, no threads (works on any web host).
    target_compile_options(aven_engine PUBLIC -sUSE_GLFW=3)
    target_link_options(aven_engine PUBLIC -sUSE_GLFW=3 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1
                        -sSTACK_SIZE=1048576)
else()
    target_link_libraries(aven_engine PUBLIC glfw aven_glad)
endif()
target_compile_definitions(aven_engine PUBLIC AVEN_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(aven_engine PRIVATE /W4 /wd4100 /wd4201 /wd4244 /wd4267 /wd4305 /utf-8)
    target_compile_definitions(aven_engine PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(aven_engine PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(aven_engine PUBLIC Threads::Threads)
endif()

aven_embed_files(aven_engine FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/data/fonts/Roboto-Medium.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/data/fonts/Cousine-Regular.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/data/icon/aven_16.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/icon/aven_32.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/icon/aven_48.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/icon/aven_64.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/icon/aven_128.png)
