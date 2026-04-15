// Hand domain upcaster — transforms old event versions to current during replay.
//
// The upcaster is generic: parameterized by domain name, not tied to hand
// specifically. The same pattern applies to any domain's schema evolution.
//
// Currently passthrough — register transformation functions as schema changes.

#include <grpcpp/grpcpp.h>
#include <iostream>
#include <string>

#include "angzarr/router.hpp"
#include "angzarr/types.pb.h"

namespace {

constexpr int DEFAULT_PORT = 50403;
constexpr const char* HAND_DOMAIN = "hand";

}  // namespace

int main(int argc, char** argv) {
    std::string port_str = std::to_string(DEFAULT_PORT);
    if (const char* env_port = std::getenv("PORT")) {
        port_str = env_port;
    }

    angzarr::UpcasterRouter router(HAND_DOMAIN);
    // Register transformations as schema evolves:
    // router.on("examples.CardsDealtV1", [](const google::protobuf::Any& old_event) {
    //     examples::CardsDealtV1 v1;
    //     old_event.UnpackTo(&v1);
    //     examples::CardsDealt current;
    //     // Map fields...
    //     google::protobuf::Any result;
    //     result.PackFrom(current);
    //     return result;
    // });

    std::string server_address = "0.0.0.0:" + port_str;
    std::cout << "Hand upcaster listening on " << server_address << std::endl;

    // TODO: Wire up gRPC upcaster service
    // For now this is a build target showing the pattern

    return 0;
}
