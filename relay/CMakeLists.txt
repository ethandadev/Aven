# rynax-relay: the server for online multiplayer (engine/src/rynax/runtime/relay.h). Runs anywhere
# with a public address, without a screen: a small cloud server is plenty.
add_executable(rynax_relay main.cpp)
target_link_libraries(rynax_relay PRIVATE rynax_engine)
set_target_properties(rynax_relay PROPERTIES OUTPUT_NAME "rynax-relay")
