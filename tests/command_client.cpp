/// CommandClient factory implementation.
/// Always creates GrpcClient. PLAYER_URL defaults to localhost:1310.

#include "command_client.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

#include "grpc_client.hpp"

namespace tests {

/// Global command client instance.
std::unique_ptr<CommandClient> g_command_client;

/// Factory: create GrpcClient from environment.
std::unique_ptr<CommandClient> create_command_client() {
    const char* player_url = std::getenv("PLAYER_URL");
    if (!player_url || std::string(player_url).empty()) {
        player_url = "localhost:1310";
    }

    std::cout << "[CommandClient] Using GrpcClient (PLAYER_URL=" << player_url << ")"
              << std::endl;
    return GrpcClient::from_env();
}

}  // namespace tests
