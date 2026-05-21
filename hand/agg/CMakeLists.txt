add_executable(agg-hand
    src/main.cpp
    src/hand_state.cpp
    handlers/deal_handler.cpp
    handlers/post_blind_handler.cpp
    handlers/action_handler.cpp
    handlers/deal_community_handler.cpp
    handlers/award_pot_handler.cpp
    handlers/action_clock_handlers.cpp
    # PR #12 — 13 new disaster-recovery / stud / button handlers.
    handlers/misdeal_handlers.cpp
    handlers/premature_handlers.cpp
    handlers/stud_handlers.cpp
)

target_include_directories(agg-hand PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/handlers
)

target_link_libraries(agg-hand PRIVATE
    angzarr_client
    gRPC::grpc++_reflection
)
