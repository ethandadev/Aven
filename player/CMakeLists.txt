if(WIN32)
    add_executable(rynax_player main.cpp app.rc ${PROJECT_SOURCE_DIR}/resources/windows/rynax.manifest)
else()
    add_executable(rynax_player main.cpp)
endif()
target_link_libraries(rynax_player PRIVATE rynax_engine)
set_target_properties(rynax_player PROPERTIES OUTPUT_NAME "rynax-player")
if(WIN32)
    set_target_properties(rynax_player PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    # A windowed app without a console, still starting at the standard main().
    if(MSVC)
        target_link_options(rynax_player PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
endif()
if(EMSCRIPTEN)
    # The web player: rynax-player.js + rynax-player.wasm. The page (web/index.html) downloads the
    # game's files into /game before starting, so one build of the player runs any game.
    target_link_options(rynax_player PRIVATE -sFORCE_FILESYSTEM=1 "-sEXPORTED_RUNTIME_METHODS=['FS','addRunDependency','removeRunDependency']" -lidbfs.js -sENVIRONMENT=web
                        -sEXIT_RUNTIME=0)
    # Copied again whenever the page changes, even if the player itself doesn't need relinking.
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/web/index.html ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/index.html COPYONLY)
endif()
