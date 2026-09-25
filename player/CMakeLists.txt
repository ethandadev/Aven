add_executable(aven_player main.cpp)
target_link_libraries(aven_player PRIVATE aven_engine)
set_target_properties(aven_player PROPERTIES OUTPUT_NAME "aven-player")
if(WIN32)
    set_target_properties(aven_player PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    # A windowed app without a console, still starting at the standard main().
    if(MSVC)
        target_link_options(aven_player PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
endif()
if(EMSCRIPTEN)
    # The web player: aven-player.js + aven-player.wasm. The page (web/index.html) downloads the
    # game's files into /game before starting, so one build of the player runs any game.
    target_link_options(aven_player PRIVATE -sFORCE_FILESYSTEM=1 "-sEXPORTED_RUNTIME_METHODS=['FS']" -sENVIRONMENT=web
                        -sEXIT_RUNTIME=0)
    # Copied again whenever the page changes, even if the player itself doesn't need relinking.
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/web/index.html ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/index.html COPYONLY)
endif()
