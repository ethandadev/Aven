# aven-relay: the server for online multiplayer (engine/src/aven/runtime/relay.h). Runs anywhere
# with a public address, without a screen: a small cloud server is plenty.
add_executable(aven_relay main.cpp)
target_link_libraries(aven_relay PRIVATE aven_engine)
set_target_properties(aven_relay PROPERTIES OUTPUT_NAME "aven-relay")
