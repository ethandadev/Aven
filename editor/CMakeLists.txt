file(GLOB_RECURSE AVEN_EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
add_executable(aven_editor ${AVEN_EDITOR_SOURCES})
target_include_directories(aven_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(aven_editor PRIVATE aven_engine aven_imgui)
set_target_properties(aven_editor PROPERTIES OUTPUT_NAME "aven-editor")
target_compile_definitions(aven_editor PRIVATE AVEN_TEMPLATES_DIR="${PROJECT_SOURCE_DIR}/templates")
if(WIN32)
    set_target_properties(aven_editor PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()
# The editor exports games by copying the player next to them.
add_dependencies(aven_editor aven_player)
# Ship the starter templates next to the editor.
add_custom_command(TARGET aven_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/templates $<TARGET_FILE_DIR:aven_editor>/templates)
