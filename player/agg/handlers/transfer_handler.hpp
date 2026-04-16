#pragma once

#include "examples/player.pb.h"
#include "player_state.hpp"

namespace player {
namespace handlers {

/// Handle TransferFunds command.
examples::FundsTransferred handle_transfer_funds(const examples::TransferFunds& cmd,
                                           const PlayerState& state);

}  // namespace handlers
}  // namespace player
