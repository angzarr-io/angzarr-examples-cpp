add_executable(saga-hand-table
    src/main.cpp
)

target_include_directories(saga-hand-table PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(saga-hand-table PRIVATE
    angzarr_client
    gRPC::grpc++_reflection
)
