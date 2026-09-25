# Native module examples. Building them checks that aven.h and the project template still compile,
# as C and as C++.

add_library(aven_example_template SHARED ${PROJECT_SOURCE_DIR}/sdk/template/src/behaviors.c)
add_library(aven_example_patrol SHARED ${CMAKE_CURRENT_SOURCE_DIR}/patrol.cpp)
foreach(target aven_example_template aven_example_patrol)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/sdk/include)
    set_target_properties(${target} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(${target} PRIVATE m)
    endif()
endforeach()
