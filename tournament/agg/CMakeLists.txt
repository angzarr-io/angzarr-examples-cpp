add_executable(agg-tournament
    src/main.cpp
    src/tournament_state.cpp
)

target_include_directories(agg-tournament PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(agg-tournament PRIVATE
    angzarr_client
    gRPC::grpc++_reflection
)
