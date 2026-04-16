#pragma once

#include "examples/player.pb.h"
#include "player_state.hpp"

namespace player {
namespace handlers {

/// Handle ReleaseFunds command.
examples::FundsReleased handle_release_funds(const examples::ReleaseFunds& cmd, const PlayerState& state);

}  // namespace handlers
}  // namespace player
