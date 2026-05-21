add_executable(agg-tournament
    src/main.cpp
    src/tournament_state.cpp
    handlers/create_handler.cpp
    handlers/registration_handler.cpp
    handlers/lifecycle_handler.cpp
    handlers/rebuy_handler.cpp
    handlers/advanced_handlers.cpp
)

target_include_directories(agg-tournament PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/handlers
)

target_link_libraries(agg-tournament PRIVATE
    angzarr_client
    gRPC::grpc++_reflection
)
