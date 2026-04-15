// Standalone gtest binary for Mull mutation testing.
// Tests handler logic directly without cucumber wire protocol.

#include <gtest/gtest.h>

#include "angzarr/errors.hpp"
#include "angzarr/helpers.hpp"
#include "angzarr/types.pb.h"
#include "deposit_handler.hpp"
#include "examples/player.pb.h"
#include "player_state.hpp"
#include "register_handler.hpp"
#include "reserve_handler.hpp"
#include "withdraw_handler.hpp"

// --- Player Registration ---

TEST(RegisterHandler, RejectsExistingPlayer) {
    player::PlayerState state;
    state.player_id = "existing";
    EXPECT_THROW(player::handlers::handle_register(
        examples::RegisterPlayer(), state), angzarr::CommandRejectedError);
}

TEST(RegisterHandler, SetsDisplayName) {
    examples::RegisterPlayer cmd;
    cmd.set_display_name("Alice");
    cmd.set_email("alice@example.com");
    player::PlayerState state;
    auto event = player::handlers::handle_register(cmd, state);
    EXPECT_EQ(event.display_name(), "Alice");
}

TEST(RegisterHandler, SetsEmail) {
    examples::RegisterPlayer cmd;
    cmd.set_display_name("Alice");
    cmd.set_email("alice@example.com");
    player::PlayerState state;
    auto event = player::handlers::handle_register(cmd, state);
    EXPECT_EQ(event.email(), "alice@example.com");
}

// --- Deposits ---

TEST(DepositHandler, RejectsNonExistentPlayer) {
    player::PlayerState state;
    examples::DepositFunds cmd;
    cmd.mutable_amount()->set_amount(500);
    EXPECT_THROW(player::handlers::handle_deposit(cmd, state),
                 angzarr::CommandRejectedError);
}

TEST(DepositHandler, IncreasesBankroll) {
    player::PlayerState state;
    state.player_id = "player1";
    state.bankroll = 1000;
    examples::DepositFunds cmd;
    cmd.mutable_amount()->set_amount(500);
    auto event = player::handlers::handle_deposit(cmd, state);
    EXPECT_EQ(event.new_balance().amount(), 1500);
}

TEST(DepositHandler, RejectsZeroAmount) {
    player::PlayerState state;
    state.player_id = "player1";
    examples::DepositFunds cmd;
    cmd.mutable_amount()->set_amount(0);
    EXPECT_THROW(player::handlers::handle_deposit(cmd, state),
                 angzarr::CommandRejectedError);
}

// --- Withdrawals ---

TEST(WithdrawHandler, RejectsInsufficientFunds) {
    player::PlayerState state;
    state.player_id = "player1";
    state.bankroll = 100;
    examples::WithdrawFunds cmd;
    cmd.mutable_amount()->set_amount(200);
    EXPECT_THROW(player::handlers::handle_withdraw(cmd, state),
                 angzarr::CommandRejectedError);
}

TEST(WithdrawHandler, DecreasesBankroll) {
    player::PlayerState state;
    state.player_id = "player1";
    state.bankroll = 1000;
    state.reserved_funds = 0;
    examples::WithdrawFunds cmd;
    cmd.mutable_amount()->set_amount(400);
    auto event = player::handlers::handle_withdraw(cmd, state);
    EXPECT_EQ(event.new_balance().amount(), 600);
}

// --- Reservations ---

TEST(ReserveHandler, RejectsInsufficientAvailable) {
    player::PlayerState state;
    state.player_id = "player1";
    state.bankroll = 500;
    state.reserved_funds = 400;
    examples::ReserveFunds cmd;
    cmd.mutable_amount()->set_amount(200);
    cmd.set_table_root("table-1");
    EXPECT_THROW(player::handlers::handle_reserve(cmd, state),
                 angzarr::CommandRejectedError);
}

TEST(ReserveHandler, LocksAmount) {
    player::PlayerState state;
    state.player_id = "player1";
    state.bankroll = 1000;
    state.reserved_funds = 0;
    examples::ReserveFunds cmd;
    cmd.mutable_amount()->set_amount(500);
    cmd.set_table_root("table-1");
    auto event = player::handlers::handle_reserve(cmd, state);
    EXPECT_EQ(event.amount().amount(), 500);
    EXPECT_EQ(event.new_reserved_balance().amount(), 500);
    EXPECT_EQ(event.new_available_balance().amount(), 500);
}
