add_executable(aven_player main.cpp)
target_link_libraries(aven_player PRIVATE aven_engine)
set_target_properties(aven_player PROPERTIES OUTPUT_NAME "aven-player")
if(WIN32)
    set_target_properties(aven_player PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()
if(EMSCRIPTEN)
    # The web player: aven-player.js + aven-player.wasm. The page (web/index.html) downloads the
    # game's files into /game before starting, so one build of the player runs any game.
    target_link_options(aven_player PRIVATE -sFORCE_FILESYSTEM=1 "-sEXPORTED_RUNTIME_METHODS=['FS']" -sENVIRONMENT=web
                        -sEXIT_RUNTIME=0)
    add_custom_command(TARGET aven_player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html $<TARGET_FILE_DIR:aven_player>/index.html)
endif()
