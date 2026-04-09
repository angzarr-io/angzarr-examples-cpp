/// CommandClient factory implementation.
///
/// Checks PLAYER_URL env var to decide which client to create:
///   set   -> GrpcClient (for acceptance tests against deployed system)
///   unset -> InProcessClient (for unit tests with direct handler calls)

#include "command_client.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

#include "grpc_client.hpp"
#include "inprocess_client.hpp"

namespace tests {

/// Global command client instance.
std::unique_ptr<CommandClient> g_command_client;

/// Factory: create the appropriate CommandClient based on environment.
std::unique_ptr<CommandClient> create_command_client() {
    const char* player_url = std::getenv("PLAYER_URL");

    if (player_url && std::string(player_url).length() > 0) {
        std::cout << "[CommandClient] Using GrpcClient (PLAYER_URL=" << player_url << ")"
                  << std::endl;
        return GrpcClient::from_env();
    }

    std::cout << "[CommandClient] Using InProcessClient (no PLAYER_URL set)" << std::endl;
    return std::make_unique<InProcessClient>();
}

}  // namespace tests
