#include "player.hpp"

#include "deposit_handler.hpp"
#include "register_handler.hpp"
#include "rejected_handler.hpp"
#include "release_handler.hpp"
#include "reserve_handler.hpp"
#include "transfer_handler.hpp"
#include "withdraw_handler.hpp"

namespace player {

// docs:start:oo_handlers
Player::Player() {
    // Register command handlers
    register_handler<examples::RegisterPlayer, examples::PlayerRegistered>(
        [this](const examples::RegisterPlayer& cmd, const PlayerState&) {
            return handle_register_player(cmd);
        });
    register_handler<examples::DepositFunds, examples::FundsDeposited>(
        [this](const examples::DepositFunds& cmd, const PlayerState&) {
            return handle_deposit_funds(cmd);
        });
    register_handler<examples::WithdrawFunds, examples::FundsWithdrawn>(
        [this](const examples::WithdrawFunds& cmd, const PlayerState&) {
            return handle_withdraw_funds(cmd);
        });
    register_handler<examples::ReserveFunds, examples::FundsReserved>(
        [this](const examples::ReserveFunds& cmd, const PlayerState&) {
            return handle_reserve_funds(cmd);
        });
    register_handler<examples::ReleaseFunds, examples::FundsReleased>(
        [this](const examples::ReleaseFunds& cmd, const PlayerState&) {
            return handle_release_funds(cmd);
        });
    register_handler<examples::TransferFunds, examples::FundsTransferred>(
        [this](const examples::TransferFunds& cmd, const PlayerState&) {
            return handle_transfer_funds(cmd);
        });
}

examples::PlayerRegistered Player::handle_register_player(const examples::RegisterPlayer& cmd) {
    return handlers::handle_register_player(cmd, state_);
}

examples::FundsDeposited Player::handle_deposit_funds(const examples::DepositFunds& cmd) {
    return handlers::handle_deposit_funds(cmd, state_);
}

examples::FundsWithdrawn Player::handle_withdraw_funds(const examples::WithdrawFunds& cmd) {
    return handlers::handle_withdraw_funds(cmd, state_);
}

examples::FundsReserved Player::handle_reserve_funds(const examples::ReserveFunds& cmd) {
    return handlers::handle_reserve_funds(cmd, state_);
}

examples::FundsReleased Player::handle_release_funds(const examples::ReleaseFunds& cmd) {
    return handlers::handle_release_funds(cmd, state_);
}

examples::FundsTransferred Player::handle_transfer_funds(const examples::TransferFunds& cmd) {
    return handlers::handle_transfer_funds(cmd, state_);
}
// docs:end:oo_handlers

examples::FundsReleased Player::handle_join_rejected(const angzarr::Notification& notification) {
    return handlers::handle_join_rejected(notification, state_);
}

}  // namespace player
