#pragma once

#include "examples/player.pb.h"
#include "player_state.hpp"

namespace player {
namespace handlers {

/// Guard: check preconditions for DepositFunds.
void deposit_funds_guard(const PlayerState& state);

/// Validate: validate DepositFunds command inputs, return parsed amount.
int64_t deposit_funds_validate(const examples::DepositFunds& cmd);

/// Compute: build FundsDeposited event from validated inputs.
examples::FundsDeposited deposit_funds_compute(const examples::DepositFunds& cmd,
                                               const PlayerState& state, int64_t amount);

/// Handle DepositFunds command.
examples::FundsDeposited handle_deposit_funds(const examples::DepositFunds& cmd,
                                              const PlayerState& state);

}  // namespace handlers
}  // namespace player
