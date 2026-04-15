add_executable(upc-hand
    src/main.cpp
)

target_include_directories(upc-hand PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(upc-hand PRIVATE
    angzarr_client
    gRPC::grpc++_reflection
)
