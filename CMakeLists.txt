cmake_minimum_required(VERSION 3.21)
project(angzarr_examples_cpp VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)

# Proto files location.
#
# Point PROTO_ROOT directly at the angzarr-project submodule. protoc only
# READS from --proto_path (`PROTO_ROOT`) and writes generated .pb.{cc,h} to
# PROTO_GEN_DIR under the build tree, so the submodule's WT stays clean —
# satisfies the submodule-protection invariant from angzarr-project's
# submodule.just (no parent-tree process is allowed to mutate the submodule).
#
# The previous indirection through `angzarr-client-cpp/angzarr/proto/`
# polluted the angzarr-client-cpp submodule's worktree with copies, which
# tripped `check-submodules-clean` and required the manual cp dance in
# justfile.container's generate-proto-force recipe. That recipe now just
# drives the proto_gen target, no staging required.
set(PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/angzarr-project/proto")
set(ANGZARR_PROTO_DIR "${PROTO_ROOT}/angzarr_client/proto/angzarr/v1")
set(EXAMPLES_PROTO_DIR "${PROTO_ROOT}/angzarr_client/proto/examples/v1")
set(GOOGLE_API_PROTO_DIR "${PROTO_ROOT}/google/api")
# Phase I-Cpp-v3: angzarr/types.proto imports sererr/v1/sererr.proto since
# the 2026 sererr unification. CMake must compile sererr.proto too so
# types.pb.h can find sererr.pb.h.
set(SERERR_PROTO_DIR "${PROTO_ROOT}/sererr/v1")

# Generated proto output
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# Collect proto files
file(GLOB ANGZARR_PROTOS "${ANGZARR_PROTO_DIR}/*.proto")
file(GLOB EXAMPLES_PROTOS "${EXAMPLES_PROTO_DIR}/*.proto")
file(GLOB GOOGLE_API_PROTOS "${GOOGLE_API_PROTO_DIR}/*.proto")
file(GLOB SERERR_PROTOS "${SERERR_PROTO_DIR}/*.proto")
# ai_sidecar.proto is Python-only by design (project_ai_driver_python_only);
# excluded from the C++ build to avoid a stub-only compilation unit.
list(FILTER EXAMPLES_PROTOS EXCLUDE REGEX "/ai_sidecar\\.proto$")
set(ALL_PROTOS ${GOOGLE_API_PROTOS} ${SERERR_PROTOS} ${ANGZARR_PROTOS} ${EXAMPLES_PROTOS})

# Generate proto sources
set(PROTO_SRCS)
set(PROTO_HDRS)
set(GRPC_SRCS)
set(GRPC_HDRS)

foreach(PROTO_FILE ${ALL_PROTOS})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    get_filename_component(PROTO_DIR ${PROTO_FILE} DIRECTORY)
    file(RELATIVE_PATH PROTO_REL_DIR ${PROTO_ROOT} ${PROTO_DIR})

    set(PROTO_SRC "${PROTO_GEN_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.pb.cc")
    set(PROTO_HDR "${PROTO_GEN_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.pb.h")
    set(GRPC_SRC "${PROTO_GEN_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.grpc.pb.cc")
    set(GRPC_HDR "${PROTO_GEN_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.grpc.pb.h")

    list(APPEND PROTO_SRCS ${PROTO_SRC})
    list(APPEND PROTO_HDRS ${PROTO_HDR})
    list(APPEND GRPC_SRCS ${GRPC_SRC})
    list(APPEND GRPC_HDRS ${GRPC_HDR})

    add_custom_command(
        OUTPUT ${PROTO_SRC} ${PROTO_HDR} ${GRPC_SRC} ${GRPC_HDR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        ARGS --proto_path=${PROTO_ROOT}
             --cpp_out=${PROTO_GEN_DIR}
             --grpc_out=${PROTO_GEN_DIR}
             --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
             ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating C++ sources from ${PROTO_NAME}.proto"
    )
endforeach()

# Discrete proto-generation aggregator target.
#
# The justfile's `generate-proto` recipe drives this target so the protoc
# custom-commands can run WITHOUT compiling the proto library or any
# downstream examples. Keeping the regen entrypoint outside the main build
# graph matches the cross-language pattern (project_proto_generation_model)
# — the `add_custom_command` rules above stay as the EXECUTOR, but
# `just generate-proto` is the canonical TRIGGER.
add_custom_target(proto_gen
    DEPENDS ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
)

# Proto library
add_library(angzarr_proto STATIC ${PROTO_SRCS} ${GRPC_SRCS})
target_include_directories(angzarr_proto PUBLIC
    ${PROTO_GEN_DIR}
    # Compatibility shim for legacy `#include "angzarr/foo.pb.h"` and
    # `#include "examples/foo.pb.h"` paths — examples-cpp/main/shim/include
    # holds thin redirect headers that pull in the post-2026-04-19
    # `angzarr_client/proto/{angzarr,examples}/foo.pb.h` and the namespace
    # alias headers (`angzarr/proto_aliases.hpp` + `angzarr/examples_proto_aliases.hpp`).
    ${CMAKE_CURRENT_SOURCE_DIR}/shim/include
)
target_link_libraries(angzarr_proto PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
)

# Client library - use the canonical client from client/cpp
add_library(angzarr_client INTERFACE)
target_include_directories(angzarr_client INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/angzarr-client-cpp/include
)
target_link_libraries(angzarr_client INTERFACE
    angzarr_proto
)

# Subdirectories
add_subdirectory(player/agg)
add_subdirectory(player/upc)
add_subdirectory(player/saga-table)
add_subdirectory(table/agg)
add_subdirectory(hand/agg)
add_subdirectory(table/saga-hand-oo)
add_subdirectory(table/saga-player)
add_subdirectory(hand/saga-table)
add_subdirectory(hand/saga-player)
add_subdirectory(hand-flow-oo)
add_subdirectory(prj-output-oo)
add_subdirectory(tournament/agg)
# Phase I-Cpp-v3 — new modules.
add_subdirectory(reservation/agg)
add_subdirectory(reservation/pmg)
add_subdirectory(table/saga-tournament-h4h)
# TODO: Fix hand/upc (missing UpcasterRouter shim)
# add_subdirectory(hand/upc)

# Functional-style examples (superseded by OO variants above)
# add_subdirectory(table/saga-hand)
# add_subdirectory(hand-flow)
# add_subdirectory(prj-output)

# Tests (optional, requires cucumber-cpp)
option(BUILD_TESTS "Build cucumber-cpp tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
