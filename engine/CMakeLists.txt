file(GLOB_RECURSE AVEN_ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(aven_engine STATIC ${AVEN_ENGINE_SOURCES})
target_include_directories(aven_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/sdk/include)
target_link_libraries(aven_engine PUBLIC glfw aven_glad aven_stb aven_cgltf aven_miniaudio box2d ${CMAKE_DL_LIBS})
target_compile_definitions(aven_engine PUBLIC AVEN_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(aven_engine PRIVATE /W4 /wd4100 /wd4201 /wd4244 /wd4267 /wd4305 /utf-8)
    target_compile_definitions(aven_engine PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(aven_engine PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

find_package(Threads REQUIRED)
target_link_libraries(aven_engine PUBLIC Threads::Threads)

aven_embed_files(aven_engine FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/data/fonts/Roboto-Medium.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/data/fonts/Cousine-Regular.ttf)
