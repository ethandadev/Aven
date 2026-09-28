file(GLOB_RECURSE AVEN_EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
if(APPLE)
    # The real macOS menu bar (menu.h): Objective-C++, compiled as C++ with -x objective-c++ rather
    # than by enabling the OBJCXX language, which would change how GLFW's .m files are built.
    list(APPEND AVEN_EDITOR_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/menu_mac.mm)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/menu_mac.mm PROPERTIES
        LANGUAGE CXX COMPILE_OPTIONS "-xobjective-c++;-fobjc-arc")
endif()
if(WIN32)
    # Aven's icon in the .exe: Explorer, the taskbar and the installer's shortcuts show it.
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/editor_icon.rc "1 ICON \"${PROJECT_SOURCE_DIR}/resources/icon/aven.ico\"\n")
    list(APPEND AVEN_EDITOR_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/editor_icon.rc)
endif()
add_executable(aven_editor ${AVEN_EDITOR_SOURCES})
target_include_directories(aven_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(aven_editor PRIVATE aven_engine aven_imgui aven_qrcodegen aven_gifh)
if(WIN32)
    target_link_libraries(aven_editor PRIVATE ws2_32) # the "share on this network" web server
endif()
if(APPLE)
    target_link_libraries(aven_editor PRIVATE "-framework Cocoa")
endif()
# Where a locally built web player lives (tools/web/build_web_player.sh puts a copy next to the editor too).
target_compile_definitions(aven_editor PRIVATE AVEN_WEB_PLAYER_DIR="${PROJECT_SOURCE_DIR}/build-web/bin")
set_target_properties(aven_editor PROPERTIES OUTPUT_NAME "aven-editor")
target_compile_definitions(aven_editor PRIVATE AVEN_TEMPLATES_DIR="${PROJECT_SOURCE_DIR}/templates"
                           AVEN_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
if(WIN32)
    set_target_properties(aven_editor PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    # A windowed app without a console, still starting at the standard main().
    if(MSVC)
        target_link_options(aven_editor PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
endif()
# The editor exports games by copying the player next to them.
add_dependencies(aven_editor aven_player)
# Ship the starter templates next to the editor.
add_custom_command(TARGET aven_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/templates $<TARGET_FILE_DIR:aven_editor>/templates
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data $<TARGET_FILE_DIR:aven_editor>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/quests $<TARGET_FILE_DIR:aven_editor>/quests
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/sdk/include $<TARGET_FILE_DIR:aven_editor>/sdk/include
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/sdk/template $<TARGET_FILE_DIR:aven_editor>/sdk/template)
