// Standalone gtest binary for Mull mutation testing.
// Tests handler logic directly without cucumber wire protocol.

#include <gtest/gtest.h>

#include "action_clock_handlers.hpp"
#include "angzarr/errors.hpp"
#include "angzarr/helpers.hpp"
#include "angzarr/types.pb.h"
#include "deposit_handler.hpp"
#include "examples/hand.pb.h"
#include "examples/player.pb.h"
#include "hand_for_hand_handlers.hpp"
#include "hand_state.hpp"
#include "player_state.hpp"
#include "register_handler.hpp"
#include "reserve_handler.hpp"
#include "table_state.hpp"
#include "withdraw_handler.hpp"

// --- Player Registration ---

TEST(RegisterHandler, RejectsExistingPlayer) {
  player::PlayerState state;
  state.player_id = "existing";
  EXPECT_THROW(player::handlers::handle_register_player(
                   examples::RegisterPlayer(), state),
               angzarr::CommandRejectedError);
}

TEST(RegisterHandler, SetsDisplayName) {
  examples::RegisterPlayer cmd;
  cmd.set_display_name("Alice");
  cmd.set_email("alice@example.com");
  player::PlayerState state;
  auto event = player::handlers::handle_register_player(cmd, state);
  EXPECT_EQ(event.display_name(), "Alice");
}

TEST(RegisterHandler, SetsEmail) {
  examples::RegisterPlayer cmd;
  cmd.set_display_name("Alice");
  cmd.set_email("alice@example.com");
  player::PlayerState state;
  auto event = player::handlers::handle_register_player(cmd, state);
  EXPECT_EQ(event.email(), "alice@example.com");
}

// --- Deposits ---

TEST(DepositHandler, RejectsNonExistentPlayer) {
  player::PlayerState state;
  examples::DepositFunds cmd;
  cmd.mutable_amount()->set_amount(500);
  EXPECT_THROW(player::handlers::handle_deposit_funds(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DepositHandler, IncreasesBankroll) {
  player::PlayerState state;
  state.player_id = "player1";
  state.bankroll = 1000;
  examples::DepositFunds cmd;
  cmd.mutable_amount()->set_amount(500);
  auto event = player::handlers::handle_deposit_funds(cmd, state);
  EXPECT_EQ(event.new_balance().amount(), 1500);
}

TEST(DepositHandler, RejectsZeroAmount) {
  player::PlayerState state;
  state.player_id = "player1";
  examples::DepositFunds cmd;
  cmd.mutable_amount()->set_amount(0);
  EXPECT_THROW(player::handlers::handle_deposit_funds(cmd, state),
               angzarr::CommandRejectedError);
}

// --- Withdrawals ---

TEST(WithdrawHandler, RejectsInsufficientFunds) {
  player::PlayerState state;
  state.player_id = "player1";
  state.bankroll = 100;
  examples::WithdrawFunds cmd;
  cmd.mutable_amount()->set_amount(200);
  EXPECT_THROW(player::handlers::handle_withdraw_funds(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(WithdrawHandler, DecreasesBankroll) {
  player::PlayerState state;
  state.player_id = "player1";
  state.bankroll = 1000;
  state.reserved_funds = 0;
  examples::WithdrawFunds cmd;
  cmd.mutable_amount()->set_amount(400);
  auto event = player::handlers::handle_withdraw_funds(cmd, state);
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
  cmd.set_key("table-1");
  EXPECT_THROW(player::handlers::handle_reserve_funds(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReserveHandler, LocksAmount) {
  player::PlayerState state;
  state.player_id = "player1";
  state.bankroll = 1000;
  state.reserved_funds = 0;
  examples::ReserveFunds cmd;
  cmd.mutable_amount()->set_amount(500);
  cmd.set_key("table-1");
  auto event = player::handlers::handle_reserve_funds(cmd, state);
  EXPECT_EQ(event.amount().amount(), 500);
  EXPECT_EQ(event.new_reserved_balance().amount(), 500);
  EXPECT_EQ(event.new_available_balance().amount(), 500);
}

// =============================================================================
// Phase I-Cpp-v2 — ActionClockHandlers (MED-EX-2.3.1 / HIGH-EX-PY-RS-1)
// =============================================================================

namespace {

hand::HandState make_betting_hand(const std::string& player_root = "p1",
                                  int64_t current_bet = 100,
                                  int64_t min_raise = 50) {
  hand::HandState s;
  s.status = "betting";
  s.current_bet = current_bet;
  s.min_raise = min_raise;
  hand::PlayerHandInfo p;
  p.player_root = player_root;
  p.position = 0;
  p.stack = 1000;
  p.bet_this_round = 0;
  s.players[0] = p;
  return s;
}

}  // namespace

TEST(StartActionClock, RejectsMissingHand) {
  hand::HandState state;  // empty / status == ""
  examples::StartActionClock cmd;
  cmd.set_player_root("p1");
  cmd.set_seconds(25);
  EXPECT_THROW(hand::handlers::handle_start_action_clock(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(StartActionClock, RejectsNonPositiveSeconds) {
  auto state = make_betting_hand();
  examples::StartActionClock cmd;
  cmd.set_player_root("p1");
  cmd.set_seconds(0);
  EXPECT_THROW(hand::handlers::handle_start_action_clock(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(StartActionClock, RejectsFoldedPlayer) {
  auto state = make_betting_hand();
  state.players[0].has_folded = true;
  examples::StartActionClock cmd;
  cmd.set_player_root("p1");
  cmd.set_seconds(25);
  EXPECT_THROW(hand::handlers::handle_start_action_clock(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(StartActionClock, EmitsActionClockStarted) {
  auto state = make_betting_hand();
  examples::StartActionClock cmd;
  cmd.set_player_root("p1");
  cmd.set_seconds(25);
  auto event = hand::handlers::handle_start_action_clock(cmd, state);
  EXPECT_EQ(event.player_root(), "p1");
  EXPECT_EQ(event.seconds(), 25u);
  EXPECT_TRUE(event.has_started_at());
}

TEST(ResolveDeclaredAmount, FoldYieldsZero) {
  hand::PlayerHandInfo p;
  p.stack = 1000;
  EXPECT_EQ(
      hand::handlers::resolve_declared_amount(examples::FOLD, 0, p, 50, 25), 0);
}

TEST(ResolveDeclaredAmount, CheckYieldsZero) {
  hand::PlayerHandInfo p;
  p.stack = 1000;
  EXPECT_EQ(
      hand::handlers::resolve_declared_amount(examples::CHECK, 0, p, 50, 25),
      0);
}

TEST(ResolveDeclaredAmount, CallSubtractsBetThisRoundCappedAtStack) {
  hand::PlayerHandInfo p;
  p.stack = 1000;
  p.bet_this_round = 20;
  EXPECT_EQ(
      hand::handlers::resolve_declared_amount(examples::CALL, 0, p, 100, 25),
      80);
}

TEST(ResolveDeclaredAmount, CallCappedAtPlayerStack) {
  hand::PlayerHandInfo p;
  p.stack = 30;
  p.bet_this_round = 20;
  EXPECT_EQ(
      hand::handlers::resolve_declared_amount(examples::CALL, 0, p, 100, 25),
      30);
}

TEST(ResolveDeclaredAmount, BetWithoutDeclaredUsesMinRaise) {
  hand::PlayerHandInfo p;
  p.stack = 1000;
  EXPECT_EQ(hand::handlers::resolve_declared_amount(examples::BET, 0, p, 0, 50),
            50);
}

TEST(ResolveDeclaredAmount, BetWithDeclaredOverridesMinRaise) {
  hand::PlayerHandInfo p;
  p.stack = 1000;
  EXPECT_EQ(
      hand::handlers::resolve_declared_amount(examples::BET, 200, p, 0, 50),
      200);
}

TEST(ResolveDeclaredAmount, RaiseWithDeclaredUsesDeclared) {
  hand::PlayerHandInfo p;
  p.stack = 1000;
  EXPECT_EQ(
      hand::handlers::resolve_declared_amount(examples::RAISE, 250, p, 100, 50),
      250);
}

TEST(ResolveDeclaredAmount, RaiseWithoutDeclaredAddsMaxMinRaiseOrCurrentBet) {
  hand::PlayerHandInfo p;
  p.stack = 1000;
  // current_bet=100, min_raise=50 -> max(50,100)=100 -> 100+100=200
  EXPECT_EQ(
      hand::handlers::resolve_declared_amount(examples::RAISE, 0, p, 100, 50),
      200);
}

TEST(ResolveDeclaredAmount, AllInUsesEntireStack) {
  hand::PlayerHandInfo p;
  p.stack = 850;
  EXPECT_EQ(
      hand::handlers::resolve_declared_amount(examples::ALL_IN, 0, p, 50, 25),
      850);
}

TEST(DeclareAction, RejectsUnspecifiedAction) {
  auto state = make_betting_hand();
  examples::DeclareAction cmd;
  cmd.set_player_root("p1");
  cmd.set_action(examples::ACTION_UNSPECIFIED);
  EXPECT_THROW(hand::handlers::handle_declare_action(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DeclareAction, RejectsFoldedPlayer) {
  auto state = make_betting_hand();
  state.players[0].has_folded = true;
  examples::DeclareAction cmd;
  cmd.set_player_root("p1");
  cmd.set_action(examples::CALL);
  EXPECT_THROW(hand::handlers::handle_declare_action(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DeclareAction, EmitsActionTakenForVerbalCall) {
  auto state = make_betting_hand("p1", 100, 50);
  examples::DeclareAction cmd;
  cmd.set_player_root("p1");
  cmd.set_action(examples::CALL);
  auto event = hand::handlers::handle_declare_action(cmd, state);
  EXPECT_EQ(event.amount(), 100);
  EXPECT_EQ(event.action(), examples::CALL);
}

TEST(DeclareAction, NewStackIsPlayerStackMinusAmount) {
  auto state = make_betting_hand("p1", 100, 50);
  state.players[0].stack = 800;
  examples::DeclareAction cmd;
  cmd.set_player_root("p1");
  cmd.set_action(examples::CALL);
  auto event = hand::handlers::handle_declare_action(cmd, state);
  EXPECT_EQ(event.player_stack(), 700);
}

TEST(DeclareAction, PotTotalAddsAmount) {
  auto state = make_betting_hand("p1", 100, 50);
  // get_pot_total() is computed from state.pots; default empty -> 0
  examples::DeclareAction cmd;
  cmd.set_player_root("p1");
  cmd.set_action(examples::CALL);
  auto event = hand::handlers::handle_declare_action(cmd, state);
  EXPECT_EQ(event.pot_total(), 100);  // 0 + 100 (call amount)
}

TEST(DeclareAction, AmountToCallIsMaxOfCurrentBetAndPlayerBetPlusAmount) {
  auto state = make_betting_hand("p1", 100, 50);
  state.players[0].bet_this_round = 60;
  examples::DeclareAction cmd;
  cmd.set_player_root("p1");
  cmd.set_action(examples::RAISE);
  cmd.set_amount(200);
  auto event = hand::handlers::handle_declare_action(cmd, state);
  // amount=200, bet_this_round+amount = 60+200=260, current_bet=100, max=260
  EXPECT_EQ(event.amount_to_call(), 260);
}

TEST(PullBackPriorChip, AcceptsExactlyAtThreshold) {
  // current_bet=100, bet_this_round=99 means there IS a facing raise
  auto state = make_betting_hand("p1", 100, 50);
  state.players[0].bet_this_round = 99;
  examples::PullBackPriorChip cmd;
  cmd.set_player_root("p1");
  cmd.set_chips_pulled(1);
  auto event = hand::handlers::handle_pull_back_prior_chip(cmd, state);
  EXPECT_EQ(event.chips_pulled(), 1);
}

TEST(PullBackPriorChip, RejectsExactBetMatch) {
  // current_bet == bet_this_round means NO facing raise; pull-back rejected.
  // Mutating <= to < would still allow this case to be rejected; mutating < to
  // <= would invert. Test the boundary value.
  auto state = make_betting_hand("p1", 100, 50);
  state.players[0].bet_this_round = 100;  // exactly equal
  examples::PullBackPriorChip cmd;
  cmd.set_player_root("p1");
  cmd.set_chips_pulled(1);
  EXPECT_THROW(hand::handlers::handle_pull_back_prior_chip(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(StartActionClock, AcceptsExactlyOneSecond) {
  auto state = make_betting_hand();
  examples::StartActionClock cmd;
  cmd.set_player_root("p1");
  cmd.set_seconds(1);
  auto event = hand::handlers::handle_start_action_clock(cmd, state);
  EXPECT_EQ(event.seconds(), 1u);
}

TEST(WithdrawHandler, RejectsZeroAmount) {
  player::PlayerState state;
  state.player_id = "p1";
  state.bankroll = 1000;
  examples::WithdrawFunds cmd;
  cmd.mutable_amount()->set_amount(0);
  EXPECT_THROW(player::handlers::handle_withdraw_funds(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(WithdrawHandler, AcceptsExactlyAtBalance) {
  player::PlayerState state;
  state.player_id = "p1";
  state.bankroll = 500;
  state.reserved_funds = 0;
  examples::WithdrawFunds cmd;
  cmd.mutable_amount()->set_amount(500);
  auto event = player::handlers::handle_withdraw_funds(cmd, state);
  EXPECT_EQ(event.new_balance().amount(), 0);
}

TEST(ReserveHandler, RejectsZeroAmount) {
  player::PlayerState state;
  state.player_id = "p1";
  state.bankroll = 1000;
  examples::ReserveFunds cmd;
  cmd.mutable_amount()->set_amount(0);
  cmd.set_key("t-1");
  EXPECT_THROW(player::handlers::handle_reserve_funds(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReserveHandler, AcceptsExactlyAtAvailable) {
  player::PlayerState state;
  state.player_id = "p1";
  state.bankroll = 500;
  state.reserved_funds = 0;
  examples::ReserveFunds cmd;
  cmd.mutable_amount()->set_amount(500);
  cmd.set_key("t-1");
  auto event = player::handlers::handle_reserve_funds(cmd, state);
  EXPECT_EQ(event.amount().amount(), 500);
  EXPECT_EQ(event.new_available_balance().amount(), 0);
}

TEST(PullBackPriorChip, RejectsNonPositiveChips) {
  auto state = make_betting_hand("p1", 100, 50);
  state.players[0].bet_this_round = 10;
  examples::PullBackPriorChip cmd;
  cmd.set_player_root("p1");
  cmd.set_chips_pulled(0);
  EXPECT_THROW(hand::handlers::handle_pull_back_prior_chip(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(PullBackPriorChip, RejectsWhenNoFacingRaise) {
  auto state = make_betting_hand("p1", 100, 50);
  state.players[0].bet_this_round = 100;  // no facing raise
  examples::PullBackPriorChip cmd;
  cmd.set_player_root("p1");
  cmd.set_chips_pulled(10);
  EXPECT_THROW(hand::handlers::handle_pull_back_prior_chip(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(PullBackPriorChip, EmitsPriorChipPulledBack) {
  auto state = make_betting_hand("p1", 100, 50);
  state.players[0].bet_this_round = 10;
  examples::PullBackPriorChip cmd;
  cmd.set_player_root("p1");
  cmd.set_chips_pulled(5);
  auto event = hand::handlers::handle_pull_back_prior_chip(cmd, state);
  EXPECT_EQ(event.player_root(), "p1");
  EXPECT_EQ(event.chips_pulled(), 5);
}

TEST(AdjustmentForPlayer, RefundIsPriorMinusCap) {
  hand::PlayerHandInfo p;
  p.player_root = "p1";
  p.bet_this_round = 200;
  auto adj = hand::handlers::adjustment_for_player(p, 100);
  EXPECT_EQ(adj.prior_contribution(), 200);
  EXPECT_EQ(adj.new_contribution(), 100);
  EXPECT_EQ(adj.refund_to_stack(), 100);
}

TEST(AdjustmentForPlayer, BelowCapMeansNoRefund) {
  hand::PlayerHandInfo p;
  p.player_root = "p2";
  p.bet_this_round = 50;
  auto adj = hand::handlers::adjustment_for_player(p, 100);
  EXPECT_EQ(adj.prior_contribution(), 50);
  EXPECT_EQ(adj.new_contribution(), 50);
  EXPECT_EQ(adj.refund_to_stack(), 0);
}

TEST(CorrectIllegalBet, RejectsMissingReason) {
  auto state = make_betting_hand();
  examples::CorrectIllegalBet cmd;
  cmd.set_corrected_amount(50);
  EXPECT_THROW(hand::handlers::handle_correct_illegal_bet(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CorrectIllegalBet, RejectsNegativeCorrectedAmount) {
  auto state = make_betting_hand();
  examples::CorrectIllegalBet cmd;
  cmd.set_reason("PL_ILLEGAL_OVERBET");
  cmd.set_corrected_amount(-1);
  EXPECT_THROW(hand::handlers::handle_correct_illegal_bet(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CorrectIllegalBet, AcceptsZeroCorrectedAmount) {
  // Boundary test — corrected_amount=0 is valid (refunds all chips).
  auto state = make_betting_hand();
  examples::CorrectIllegalBet cmd;
  cmd.set_reason("PL_ILLEGAL_OVERBET");
  cmd.set_corrected_amount(0);
  // Should NOT throw — kills `< 0` -> `<= 0` mutation.
  auto event = hand::handlers::handle_correct_illegal_bet(cmd, state);
  EXPECT_EQ(event.corrected_amount(), 0);
}

TEST(CorrectIllegalBet, EmitsUnderbetCorrectedWithPerPlayerAdjustments) {
  auto state = make_betting_hand("p1", 200, 50);
  state.players[0].bet_this_round = 200;
  hand::PlayerHandInfo p2;
  p2.player_root = "p2";
  p2.position = 1;
  p2.bet_this_round = 30;
  state.players[1] = p2;
  examples::CorrectIllegalBet cmd;
  cmd.set_reason("PL_ILLEGAL_OVERBET");
  cmd.set_corrected_amount(100);
  auto event = hand::handlers::handle_correct_illegal_bet(cmd, state);
  EXPECT_EQ(event.reason(), "PL_ILLEGAL_OVERBET");
  EXPECT_EQ(event.corrected_amount(), 100);
  EXPECT_EQ(event.adjustments_size(), 2);
}

// =============================================================================
// Phase I-Cpp-v2 — Table HandForHand handlers (HIGH-EX-2.2.3)
// =============================================================================

namespace {
table::TableState make_existing_table() {
  table::TableState s;
  s.table_id = "table-1";
  s.status = "open";
  return s;
}
}  // namespace

TEST(EnterTableHandForHand, RejectsMissingTable) {
  table::TableState state;
  examples::EnterTableHandForHand cmd;
  cmd.set_tournament_root("t1");
  EXPECT_THROW(table::handlers::handle_enter_table_hand_for_hand(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(EnterTableHandForHand, RejectsAlreadyInH4H) {
  auto state = make_existing_table();
  state.hand_for_hand_status = table::HandForHandStatus::WAITING;
  examples::EnterTableHandForHand cmd;
  cmd.set_tournament_root("t1");
  EXPECT_THROW(table::handlers::handle_enter_table_hand_for_hand(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(EnterTableHandForHand, EmitsWaitingWithTournamentRoot) {
  auto state = make_existing_table();
  examples::EnterTableHandForHand cmd;
  cmd.set_tournament_root("t1");
  auto event = table::handlers::handle_enter_table_hand_for_hand(cmd, state);
  EXPECT_EQ(event.tournament_root(), "t1");
  EXPECT_TRUE(event.has_entered_at());
}

TEST(MarkTableHandForHandHandComplete, RejectsNotWaiting) {
  auto state = make_existing_table();  // NONE
  examples::MarkTableHandForHandHandComplete cmd;
  cmd.set_hand_root("h1");
  EXPECT_THROW(table::handlers::handle_mark_table_hand_for_hand_hand_complete(
                   cmd, state),
               angzarr::CommandRejectedError);
}

TEST(MarkTableHandForHandHandComplete,
     EmitsRoundCompleteCarryingTournamentRoot) {
  auto state = make_existing_table();
  state.hand_for_hand_status = table::HandForHandStatus::WAITING;
  state.hand_for_hand_tournament_root = "t1";
  examples::MarkTableHandForHandHandComplete cmd;
  cmd.set_hand_root("h1");
  auto event = table::handlers::handle_mark_table_hand_for_hand_hand_complete(
      cmd, state);
  EXPECT_EQ(event.hand_root(), "h1");
  EXPECT_EQ(event.tournament_root(), "t1");
}

TEST(EndTableHandForHand, RejectsNotInH4H) {
  auto state = make_existing_table();
  examples::EndTableHandForHand cmd;
  EXPECT_THROW(table::handlers::handle_end_table_hand_for_hand(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(EndTableHandForHand, EmitsEnded) {
  auto state = make_existing_table();
  state.hand_for_hand_status = table::HandForHandStatus::COMPLETE;
  examples::EndTableHandForHand cmd;
  auto event = table::handlers::handle_end_table_hand_for_hand(cmd, state);
  EXPECT_TRUE(event.has_ended_at());
}

// =============================================================================
// Phase I-Cpp-v3 — Tournament advanced handlers (HIGH-EX-2.4.1 + HIGH-EX-2.4.2)
// =============================================================================

#include "advanced_handlers.hpp"
#include "lifecycle_handler.hpp"
#include "rebuy_handler.hpp"
#include "registration_handler.hpp"
#include "tournament_state.hpp"
// Tournament create_handler.hpp shares its basename with
// table/agg/handlers/create_handler.hpp; explicit relative path avoids the
// #pragma once collapsing the wrong file (table/agg/handlers/ is earlier in
// the include path).
#include "../tournament/agg/handlers/create_handler.hpp"

namespace {

tournament::TournamentState make_existing_tournament() {
  tournament::TournamentState s;
  s.name = "WSOP Main";
  s.buy_in = 10000;
  s.starting_stack = 60000;
  s.max_players = 100;
  s.min_players = 10;
  s.status = examples::TOURNAMENT_RUNNING;
  return s;
}

tournament::TournamentState make_h4h_tournament() {
  auto s = make_existing_tournament();
  s.hand_for_hand = true;
  s.hand_for_hand_active_tables.insert("aabb");
  s.hand_for_hand_active_tables.insert("ccdd");
  return s;
}

}  // namespace

// --- CreateHandler ---

TEST(CreateTournamentHandler, RejectsExisting) {
  auto state = make_existing_tournament();
  examples::CreateTournament cmd;
  cmd.set_name("X");
  cmd.set_buy_in(1);
  cmd.set_starting_stack(1);
  cmd.set_max_players(2);
  EXPECT_THROW(tournament::handlers::handle_create_tournament(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CreateTournamentHandler, RejectsEmptyName) {
  tournament::TournamentState state;
  examples::CreateTournament cmd;
  cmd.set_buy_in(1);
  cmd.set_starting_stack(1);
  cmd.set_max_players(2);
  EXPECT_THROW(tournament::handlers::handle_create_tournament(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CreateTournamentHandler, RejectsZeroBuyIn) {
  tournament::TournamentState state;
  examples::CreateTournament cmd;
  cmd.set_name("X");
  cmd.set_buy_in(0);
  cmd.set_starting_stack(1);
  cmd.set_max_players(2);
  EXPECT_THROW(tournament::handlers::handle_create_tournament(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CreateTournamentHandler, RejectsBelowMinPlayers) {
  tournament::TournamentState state;
  examples::CreateTournament cmd;
  cmd.set_name("X");
  cmd.set_buy_in(1);
  cmd.set_starting_stack(1);
  cmd.set_max_players(1);
  EXPECT_THROW(tournament::handlers::handle_create_tournament(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CreateTournamentHandler, EmitsCreated) {
  tournament::TournamentState state;
  examples::CreateTournament cmd;
  cmd.set_name("WSOP");
  cmd.set_buy_in(10000);
  cmd.set_starting_stack(60000);
  cmd.set_max_players(100);
  cmd.set_min_players(10);
  auto event = tournament::handlers::handle_create_tournament(cmd, state);
  EXPECT_EQ(event.name(), "WSOP");
  EXPECT_EQ(event.buy_in(), 10000);
  EXPECT_EQ(event.starting_stack(), 60000);
  EXPECT_EQ(event.max_players(), 100);
  EXPECT_TRUE(event.has_created_at());
}

// --- RegistrationHandler ---

TEST(OpenRegistration, RejectsNonExistent) {
  tournament::TournamentState state;
  examples::OpenRegistration cmd;
  EXPECT_THROW(tournament::handlers::handle_open_registration(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(OpenRegistration, RejectsAlreadyOpen) {
  auto state = make_existing_tournament();
  state.status = examples::TOURNAMENT_REGISTRATION_OPEN;
  examples::OpenRegistration cmd;
  EXPECT_THROW(tournament::handlers::handle_open_registration(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(OpenRegistration, RejectsRunning) {
  auto state = make_existing_tournament();
  state.status = examples::TOURNAMENT_RUNNING;
  examples::OpenRegistration cmd;
  EXPECT_THROW(tournament::handlers::handle_open_registration(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(OpenRegistration, EmitsOpened) {
  auto state = make_existing_tournament();
  state.status = examples::TOURNAMENT_CREATED;
  examples::OpenRegistration cmd;
  auto event = tournament::handlers::handle_open_registration(cmd, state);
  EXPECT_TRUE(event.has_opened_at());
}

TEST(CloseRegistration, RejectsNotOpen) {
  auto state = make_existing_tournament();
  examples::CloseRegistration cmd;
  EXPECT_THROW(tournament::handlers::handle_close_registration(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CloseRegistration, EmitsTotal) {
  auto state = make_existing_tournament();
  state.status = examples::TOURNAMENT_REGISTRATION_OPEN;
  examples::PlayerRegistration r;
  state.registered_players["aa"] = r;
  state.registered_players["bb"] = r;
  examples::CloseRegistration cmd;
  auto event = tournament::handlers::handle_close_registration(cmd, state);
  EXPECT_EQ(event.total_registrations(), 2);
}

TEST(EnrollPlayer, RejectsClosed) {
  auto state = make_existing_tournament();
  state.status = examples::TOURNAMENT_CREATED;
  examples::EnrollPlayer cmd;
  cmd.set_player_root(std::string("\x01\x02", 2));
  auto result = tournament::handlers::handle_enroll_player(cmd, state);
  auto* rejected = std::get_if<examples::TournamentEnrollmentRejected>(&result);
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(rejected->reason(), "closed");
}

TEST(EnrollPlayer, RejectsFull) {
  auto state = make_existing_tournament();
  state.status = examples::TOURNAMENT_REGISTRATION_OPEN;
  state.max_players = 1;
  examples::PlayerRegistration r;
  state.registered_players["aa"] = r;
  examples::EnrollPlayer cmd;
  cmd.set_player_root(std::string("\x01", 1));
  auto result = tournament::handlers::handle_enroll_player(cmd, state);
  auto* rejected = std::get_if<examples::TournamentEnrollmentRejected>(&result);
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(rejected->reason(), "full");
}

TEST(EnrollPlayer, RejectsAlreadyRegistered) {
  auto state = make_existing_tournament();
  state.status = examples::TOURNAMENT_REGISTRATION_OPEN;
  examples::PlayerRegistration r;
  state.registered_players["01"] = r;
  examples::EnrollPlayer cmd;
  cmd.set_player_root(std::string("\x01", 1));
  auto result = tournament::handlers::handle_enroll_player(cmd, state);
  auto* rejected = std::get_if<examples::TournamentEnrollmentRejected>(&result);
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(rejected->reason(), "already_registered");
}

TEST(EnrollPlayer, EmitsEnrolled) {
  auto state = make_existing_tournament();
  state.status = examples::TOURNAMENT_REGISTRATION_OPEN;
  examples::EnrollPlayer cmd;
  cmd.set_player_root(std::string("\x01", 1));
  auto result = tournament::handlers::handle_enroll_player(cmd, state);
  auto* enrolled = std::get_if<examples::TournamentPlayerEnrolled>(&result);
  ASSERT_NE(enrolled, nullptr);
  EXPECT_EQ(enrolled->fee_paid(), state.buy_in);
  EXPECT_EQ(enrolled->starting_stack(), state.starting_stack);
  EXPECT_EQ(enrolled->registration_number(), 1);
}

// --- LifecycleHandler ---

TEST(AdvanceBlindLevel, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::AdvanceBlindLevel cmd;
  EXPECT_THROW(tournament::handlers::handle_advance_blind_level(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(AdvanceBlindLevel, AdvancesLevel) {
  auto state = make_existing_tournament();
  examples::BlindLevel b;
  b.set_small_blind(100);
  b.set_big_blind(200);
  b.set_ante(0);
  state.blind_structure.push_back(b);
  examples::AdvanceBlindLevel cmd;
  auto event = tournament::handlers::handle_advance_blind_level(cmd, state);
  EXPECT_EQ(event.level(), 1);
  EXPECT_EQ(event.small_blind(), 100);
  EXPECT_EQ(event.big_blind(), 200);
}

TEST(AdvanceBlindLevel, CapsAtLastLevel) {
  auto state = make_existing_tournament();
  state.current_level = 5;
  examples::BlindLevel b;
  b.set_small_blind(50);
  state.blind_structure.push_back(b);
  examples::AdvanceBlindLevel cmd;
  auto event = tournament::handlers::handle_advance_blind_level(cmd, state);
  EXPECT_EQ(event.level(), 6);
  EXPECT_EQ(event.small_blind(), 50);  // capped at last level
}

TEST(EliminatePlayer, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::EliminatePlayer cmd;
  EXPECT_THROW(tournament::handlers::handle_eliminate_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(EliminatePlayer, RejectsUnregistered) {
  auto state = make_existing_tournament();
  examples::EliminatePlayer cmd;
  cmd.set_player_root(std::string("\xFF", 1));
  EXPECT_THROW(tournament::handlers::handle_eliminate_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(EliminatePlayer, RecordsFinishPosition) {
  auto state = make_existing_tournament();
  examples::PlayerRegistration r;
  state.registered_players["01"] = r;
  state.players_remaining = 3;
  examples::EliminatePlayer cmd;
  cmd.set_player_root(std::string("\x01", 1));
  auto event = tournament::handlers::handle_eliminate_player(cmd, state);
  EXPECT_EQ(event.finish_position(), 3);
}

TEST(PauseTournament, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::PauseTournament cmd;
  EXPECT_THROW(tournament::handlers::handle_pause_tournament(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(PauseTournament, CarriesReason) {
  auto state = make_existing_tournament();
  examples::PauseTournament cmd;
  cmd.set_reason("color-up break");
  auto event = tournament::handlers::handle_pause_tournament(cmd, state);
  EXPECT_EQ(event.reason(), "color-up break");
}

TEST(ResumeTournament, RejectsNotPaused) {
  auto state = make_existing_tournament();
  examples::ResumeTournament cmd;
  EXPECT_THROW(tournament::handlers::handle_resume_tournament(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ResumeTournament, Resumes) {
  auto state = make_existing_tournament();
  state.status = examples::TOURNAMENT_PAUSED;
  examples::ResumeTournament cmd;
  auto event = tournament::handlers::handle_resume_tournament(cmd, state);
  EXPECT_TRUE(event.has_resumed_at());
}

// --- RebuyHandler ---

TEST(ProcessRebuy, RejectsNonExistent) {
  tournament::TournamentState state;
  examples::ProcessRebuy cmd;
  EXPECT_THROW(tournament::handlers::handle_process_rebuy(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ProcessRebuy, RejectsNotRunning) {
  auto state = make_existing_tournament();
  state.status = examples::TOURNAMENT_CREATED;
  examples::ProcessRebuy cmd;
  EXPECT_THROW(tournament::handlers::handle_process_rebuy(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ProcessRebuy, DenialNoRebuysConfig) {
  auto state = make_existing_tournament();
  examples::PlayerRegistration r;
  state.registered_players["01"] = r;
  examples::ProcessRebuy cmd;
  cmd.set_player_root(std::string("\x01", 1));
  auto result = tournament::handlers::handle_process_rebuy(cmd, state);
  auto* denied = std::get_if<examples::RebuyDenied>(&result);
  ASSERT_NE(denied, nullptr);
  EXPECT_EQ(denied->reason(), "rebuys_disabled");
}

TEST(ProcessRebuy, DenialWindowClosed) {
  auto state = make_existing_tournament();
  state.has_rebuy_config = true;
  state.rebuy_config.set_enabled(true);
  state.rebuy_config.set_rebuy_level_cutoff(3);
  state.current_level = 5;
  examples::PlayerRegistration r;
  state.registered_players["01"] = r;
  examples::ProcessRebuy cmd;
  cmd.set_player_root(std::string("\x01", 1));
  auto result = tournament::handlers::handle_process_rebuy(cmd, state);
  auto* denied = std::get_if<examples::RebuyDenied>(&result);
  ASSERT_NE(denied, nullptr);
  EXPECT_EQ(denied->reason(), "window_closed");
}

TEST(ProcessRebuy, DenialMaxReached) {
  auto state = make_existing_tournament();
  state.has_rebuy_config = true;
  state.rebuy_config.set_enabled(true);
  state.rebuy_config.set_max_rebuys(2);
  state.rebuy_config.set_rebuy_level_cutoff(10);
  examples::PlayerRegistration r;
  r.set_rebuys_used(2);
  state.registered_players["01"] = r;
  examples::ProcessRebuy cmd;
  cmd.set_player_root(std::string("\x01", 1));
  auto result = tournament::handlers::handle_process_rebuy(cmd, state);
  auto* denied = std::get_if<examples::RebuyDenied>(&result);
  ASSERT_NE(denied, nullptr);
  EXPECT_EQ(denied->reason(), "max_reached");
}

TEST(ProcessRebuy, EmitsProcessed) {
  auto state = make_existing_tournament();
  state.has_rebuy_config = true;
  state.rebuy_config.set_enabled(true);
  state.rebuy_config.set_max_rebuys(0);  // unlimited
  state.rebuy_config.set_rebuy_level_cutoff(10);
  state.rebuy_config.set_rebuy_cost(5000);
  state.rebuy_config.set_rebuy_chips(20000);
  examples::PlayerRegistration r;
  r.set_rebuys_used(1);
  state.registered_players["01"] = r;
  examples::ProcessRebuy cmd;
  cmd.set_player_root(std::string("\x01", 1));
  auto result = tournament::handlers::handle_process_rebuy(cmd, state);
  auto* ok = std::get_if<examples::RebuyProcessed>(&result);
  ASSERT_NE(ok, nullptr);
  EXPECT_EQ(ok->rebuy_cost(), 5000);
  EXPECT_EQ(ok->chips_added(), 20000);
  EXPECT_EQ(ok->rebuy_count(), 2);
}

// --- AdvancedHandlers: hand_for_hand ---

TEST(EnterHandForHand, RejectsNonExistent) {
  tournament::TournamentState state;
  examples::EnterHandForHand cmd;
  EXPECT_THROW(tournament::handlers::handle_enter_hand_for_hand(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(EnterHandForHand, RejectsAlreadyInH4H) {
  auto state = make_h4h_tournament();
  examples::EnterHandForHand cmd;
  EXPECT_THROW(tournament::handlers::handle_enter_hand_for_hand(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(EnterHandForHand, EmitsStartedWithActiveRoots) {
  auto state = make_existing_tournament();
  examples::EnterHandForHand cmd;
  cmd.add_active_table_roots(std::string("\xAA\xBB", 2));
  cmd.add_active_table_roots(std::string("\xCC\xDD", 2));
  auto event = tournament::handlers::handle_enter_hand_for_hand(cmd, state);
  EXPECT_EQ(event.active_table_roots_size(), 2);
  EXPECT_TRUE(event.has_started_at());
}

TEST(RecordTableHandComplete, RejectsNotInH4H) {
  auto state = make_existing_tournament();
  examples::RecordTableHandComplete cmd;
  cmd.set_table_root("\xAA\xBB");
  EXPECT_THROW(
      tournament::handlers::handle_record_table_hand_complete(cmd, state),
      angzarr::CommandRejectedError);
}

TEST(RecordTableHandComplete, RejectsTableNotActive) {
  auto state = make_h4h_tournament();
  examples::RecordTableHandComplete cmd;
  cmd.set_table_root(std::string("\xFF\xFF", 2));
  EXPECT_THROW(
      tournament::handlers::handle_record_table_hand_complete(cmd, state),
      angzarr::CommandRejectedError);
}

TEST(RecordTableHandComplete, EmitsMarker) {
  auto state = make_h4h_tournament();
  examples::RecordTableHandComplete cmd;
  cmd.set_table_root(std::string("\xAA\xBB", 2));
  auto event =
      tournament::handlers::handle_record_table_hand_complete(cmd, state);
  EXPECT_EQ(event.from_table_root(), std::string("\xAA\xBB", 2));
}

TEST(RecordHandForHandRoundComplete, RejectsNotInH4H) {
  auto state = make_existing_tournament();
  examples::RecordHandForHandRoundComplete cmd;
  EXPECT_THROW(tournament::handlers::handle_record_hand_for_hand_round_complete(
                   cmd, state),
               angzarr::CommandRejectedError);
}

TEST(RecordHandForHandRoundComplete, IncrementsRound) {
  auto state = make_h4h_tournament();
  state.hand_for_hand_round = 4;
  examples::RecordHandForHandRoundComplete cmd;
  auto event = tournament::handlers::handle_record_hand_for_hand_round_complete(
      cmd, state);
  EXPECT_EQ(event.round_number(), 5);
}

TEST(ClockDeduction, ZeroOrNegativeYieldsZero) {
  EXPECT_EQ(tournament::handlers::clock_deduction(0), 0);
  EXPECT_EQ(tournament::handlers::clock_deduction(-1), 0);
  EXPECT_EQ(tournament::handlers::clock_deduction(-300), 0);
}

TEST(ClockDeduction, BelowCapIsIdentity) {
  EXPECT_EQ(tournament::handlers::clock_deduction(1), 1);
  EXPECT_EQ(tournament::handlers::clock_deduction(120), 120);
  EXPECT_EQ(tournament::handlers::clock_deduction(299), 299);
}

TEST(ClockDeduction, AtCapIsCap) {
  EXPECT_EQ(tournament::handlers::clock_deduction(300), 300);
}

TEST(ClockDeduction, AboveCapIsCap) {
  EXPECT_EQ(tournament::handlers::clock_deduction(301), 300);
  EXPECT_EQ(tournament::handlers::clock_deduction(1000), 300);
}

TEST(RecordHandForHandHand, RejectsNotInH4H) {
  auto state = make_existing_tournament();
  examples::RecordHandForHandHand cmd;
  cmd.set_real_seconds(100);
  EXPECT_THROW(
      tournament::handlers::handle_record_hand_for_hand_hand(cmd, state),
      angzarr::CommandRejectedError);
}

TEST(RecordHandForHandHand, RejectsNegativeSeconds) {
  auto state = make_h4h_tournament();
  examples::RecordHandForHandHand cmd;
  cmd.set_real_seconds(-1);
  EXPECT_THROW(
      tournament::handlers::handle_record_hand_for_hand_hand(cmd, state),
      angzarr::CommandRejectedError);
}

TEST(RecordHandForHandHand, EmitsRecordedWithDeducted) {
  auto state = make_h4h_tournament();
  examples::RecordHandForHandHand cmd;
  cmd.set_real_seconds(250);
  auto event =
      tournament::handlers::handle_record_hand_for_hand_hand(cmd, state);
  EXPECT_EQ(event.real_seconds(), 250);
  EXPECT_EQ(event.clock_seconds_deducted(), 250);
}

TEST(RecordHandForHandHand, AppliesCap) {
  auto state = make_h4h_tournament();
  examples::RecordHandForHandHand cmd;
  cmd.set_real_seconds(500);
  auto event =
      tournament::handlers::handle_record_hand_for_hand_hand(cmd, state);
  EXPECT_EQ(event.clock_seconds_deducted(), 300);
}

// --- AdvancedHandlers: chip_economy ---

TEST(ColorUp, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::ColorUp cmd;
  cmd.set_retire_denomination(25);
  cmd.set_new_denomination(100);
  EXPECT_THROW(tournament::handlers::handle_color_up(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ColorUp, RejectsZeroDenominations) {
  auto state = make_existing_tournament();
  examples::ColorUp cmd;
  cmd.set_retire_denomination(0);
  cmd.set_new_denomination(100);
  EXPECT_THROW(tournament::handlers::handle_color_up(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ColorUp, RejectsSameDenominations) {
  auto state = make_existing_tournament();
  examples::ColorUp cmd;
  cmd.set_retire_denomination(100);
  cmd.set_new_denomination(100);
  EXPECT_THROW(tournament::handlers::handle_color_up(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ColorUp, EmitsCompleted) {
  auto state = make_existing_tournament();
  examples::ColorUp cmd;
  cmd.set_retire_denomination(25);
  cmd.set_new_denomination(100);
  auto event = tournament::handlers::handle_color_up(cmd, state);
  EXPECT_EQ(event.retired_denomination(), 25);
  EXPECT_EQ(event.new_denomination(), 100);
}

TEST(RebalanceTables, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::RebalanceTables cmd;
  cmd.set_player_root("p");
  cmd.set_source_table_root("a");
  cmd.set_destination_table_root("b");
  EXPECT_THROW(tournament::handlers::handle_rebalance_tables(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(RebalanceTables, RejectsEmptyPlayer) {
  auto state = make_existing_tournament();
  examples::RebalanceTables cmd;
  cmd.set_source_table_root("a");
  cmd.set_destination_table_root("b");
  EXPECT_THROW(tournament::handlers::handle_rebalance_tables(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(RebalanceTables, RejectsSameSourceDestination) {
  auto state = make_existing_tournament();
  examples::RebalanceTables cmd;
  cmd.set_player_root("p");
  cmd.set_source_table_root("a");
  cmd.set_destination_table_root("a");
  EXPECT_THROW(tournament::handlers::handle_rebalance_tables(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(RebalanceTables, EmitsMove) {
  auto state = make_existing_tournament();
  examples::RebalanceTables cmd;
  cmd.set_player_root("p");
  cmd.set_source_table_root("a");
  cmd.set_destination_table_root("b");
  cmd.set_destination_seat(3);
  cmd.set_stack(50000);
  auto event = tournament::handlers::handle_rebalance_tables(cmd, state);
  EXPECT_EQ(event.player_root(), "p");
  EXPECT_EQ(event.destination_seat(), 3);
  EXPECT_EQ(event.stack(), 50000);
}

// --- AdvancedHandlers: player_lifecycle ---

TEST(RecordSimultaneousBusts, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::RecordSimultaneousBusts cmd;
  EXPECT_THROW(
      tournament::handlers::handle_record_simultaneous_busts(cmd, state),
      angzarr::CommandRejectedError);
}

TEST(RecordSimultaneousBusts, RejectsSingleBust) {
  auto state = make_existing_tournament();
  examples::RecordSimultaneousBusts cmd;
  cmd.add_player_roots("p1");
  EXPECT_THROW(
      tournament::handlers::handle_record_simultaneous_busts(cmd, state),
      angzarr::CommandRejectedError);
}

TEST(RecordSimultaneousBusts, EmitsForTwoOrMore) {
  auto state = make_existing_tournament();
  examples::RecordSimultaneousBusts cmd;
  cmd.add_player_roots("p1");
  cmd.add_player_roots("p2");
  cmd.set_same_table(true);
  (*cmd.mutable_pre_hand_stacks())["aa"] = 1000;
  auto event =
      tournament::handlers::handle_record_simultaneous_busts(cmd, state);
  EXPECT_EQ(event.player_roots_size(), 2);
  EXPECT_TRUE(event.same_table());
  EXPECT_EQ(event.pre_hand_stacks().at("aa"), 1000);
}

TEST(SeatRedrawTrigger, FinalTableForOne) {
  EXPECT_EQ(tournament::handlers::seat_redraw_trigger(1), "FINAL_TABLE");
  EXPECT_EQ(tournament::handlers::seat_redraw_trigger(0), "FINAL_TABLE");
  EXPECT_EQ(tournament::handlers::seat_redraw_trigger(-5), "FINAL_TABLE");
}

TEST(SeatRedrawTrigger, TwoTablesForTwo) {
  EXPECT_EQ(tournament::handlers::seat_redraw_trigger(2), "TWO_TABLES");
}

TEST(SeatRedrawTrigger, ThreeTablesForThree) {
  EXPECT_EQ(tournament::handlers::seat_redraw_trigger(3), "THREE_TABLES");
}

TEST(SeatRedrawTrigger, GeneralOtherwise) {
  EXPECT_EQ(tournament::handlers::seat_redraw_trigger(4), "GENERAL");
  EXPECT_EQ(tournament::handlers::seat_redraw_trigger(100), "GENERAL");
}

TEST(TriggerSeatRedraw, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::TriggerSeatRedraw cmd;
  cmd.set_tables_remaining(3);
  EXPECT_THROW(tournament::handlers::handle_trigger_seat_redraw(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(TriggerSeatRedraw, RejectsZeroTables) {
  auto state = make_existing_tournament();
  examples::TriggerSeatRedraw cmd;
  cmd.set_tables_remaining(0);
  EXPECT_THROW(tournament::handlers::handle_trigger_seat_redraw(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(TriggerSeatRedraw, EmitsTriggered) {
  auto state = make_existing_tournament();
  examples::TriggerSeatRedraw cmd;
  cmd.set_tables_remaining(2);
  cmd.set_original_field(500);
  auto event = tournament::handlers::handle_trigger_seat_redraw(cmd, state);
  EXPECT_EQ(event.trigger(), "TWO_TABLES");
  EXPECT_EQ(event.tables_remaining(), 2);
  EXPECT_EQ(event.original_field(), 500);
}

TEST(ReseatAbsentPlayer, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::ReseatAbsentPlayer cmd;
  cmd.set_player_root("p");
  EXPECT_THROW(tournament::handlers::handle_reseat_absent_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReseatAbsentPlayer, RejectsEmptyPlayer) {
  auto state = make_existing_tournament();
  examples::ReseatAbsentPlayer cmd;
  EXPECT_THROW(tournament::handlers::handle_reseat_absent_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReseatAbsentPlayer, EmitsAbsentMove) {
  auto state = make_existing_tournament();
  examples::ReseatAbsentPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_to_seat(5);
  cmd.set_stack(40000);
  auto event = tournament::handlers::handle_reseat_absent_player(cmd, state);
  EXPECT_TRUE(event.absent_at_move());
  EXPECT_EQ(event.to_seat(), 5);
  EXPECT_EQ(event.stack(), 40000);
}

TEST(ReEntryPlayer, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::ReEntryPlayer cmd;
  cmd.set_player_root("p");
  EXPECT_THROW(tournament::handlers::handle_re_entry_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReEntryPlayer, RejectsEmptyPlayer) {
  auto state = make_existing_tournament();
  examples::ReEntryPlayer cmd;
  cmd.set_chips_forfeited(100);
  EXPECT_THROW(tournament::handlers::handle_re_entry_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReEntryPlayer, RejectsNegativeForfeited) {
  auto state = make_existing_tournament();
  examples::ReEntryPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_chips_forfeited(-5);
  EXPECT_THROW(tournament::handlers::handle_re_entry_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReEntryPlayer, EmitsChipsFromState) {
  auto state = make_existing_tournament();
  state.starting_stack = 60000;
  examples::ReEntryPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_chips_forfeited(0);
  auto event = tournament::handlers::handle_re_entry_player(cmd, state);
  EXPECT_EQ(event.chips_added(), 60000);
}

TEST(StackDelta, SumsBlinds) {
  EXPECT_EQ(tournament::handlers::stack_delta(50, 100), 150);
  EXPECT_EQ(tournament::handlers::stack_delta(0, 0), 0);
}

TEST(AdvanceAbsentBlind, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::AdvanceAbsentBlind cmd;
  cmd.set_small_blind(50);
  cmd.set_big_blind(100);
  EXPECT_THROW(tournament::handlers::handle_advance_absent_blind(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(AdvanceAbsentBlind, RejectsZeroBlinds) {
  auto state = make_existing_tournament();
  examples::AdvanceAbsentBlind cmd;
  cmd.set_small_blind(0);
  cmd.set_big_blind(100);
  EXPECT_THROW(tournament::handlers::handle_advance_absent_blind(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(AdvanceAbsentBlind, EmitsStackDelta) {
  auto state = make_existing_tournament();
  examples::AdvanceAbsentBlind cmd;
  cmd.set_small_blind(50);
  cmd.set_big_blind(100);
  auto event = tournament::handlers::handle_advance_absent_blind(cmd, state);
  EXPECT_EQ(event.stack_delta(), 150);
}

TEST(AwardBounty, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::AwardBounty cmd;
  cmd.set_amount(500);
  cmd.set_eliminator_root("e");
  cmd.set_knocked_out_root("k");
  EXPECT_THROW(tournament::handlers::handle_award_bounty(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(AwardBounty, RejectsZeroAmount) {
  auto state = make_existing_tournament();
  examples::AwardBounty cmd;
  cmd.set_amount(0);
  cmd.set_eliminator_root("e");
  cmd.set_knocked_out_root("k");
  EXPECT_THROW(tournament::handlers::handle_award_bounty(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(AwardBounty, RejectsMissingEliminator) {
  auto state = make_existing_tournament();
  examples::AwardBounty cmd;
  cmd.set_amount(500);
  cmd.set_knocked_out_root("k");
  EXPECT_THROW(tournament::handlers::handle_award_bounty(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(AwardBounty, EmitsAwarded) {
  auto state = make_existing_tournament();
  examples::AwardBounty cmd;
  cmd.set_amount(500);
  cmd.set_eliminator_root("e");
  cmd.set_knocked_out_root("k");
  cmd.set_tiebreak_reason("wsop-126b");
  auto event = tournament::handlers::handle_award_bounty(cmd, state);
  EXPECT_EQ(event.amount(), 500);
  EXPECT_EQ(event.tiebreak_reason(), "wsop-126b");
}

TEST(DetectNoShow, RejectsNonRunningOrOpenStates) {
  tournament::TournamentState state;
  state.status = examples::TOURNAMENT_PAUSED;
  examples::DetectNoShow cmd;
  cmd.set_player_root("p");
  EXPECT_THROW(tournament::handlers::handle_detect_no_show(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DetectNoShow, AcceptsRegistrationOpenState) {
  tournament::TournamentState state;
  state.name = "X";
  state.status = examples::TOURNAMENT_REGISTRATION_OPEN;
  state.starting_stack = 50000;
  state.buy_in = 1000;
  examples::DetectNoShow cmd;
  cmd.set_player_root("p");
  auto event = tournament::handlers::handle_detect_no_show(cmd, state);
  EXPECT_EQ(event.chips_removed(), 50000);
  EXPECT_EQ(event.buy_in_held(), 1000);
}

TEST(DetectNoShow, RejectsEmptyPlayer) {
  auto state = make_existing_tournament();
  examples::DetectNoShow cmd;
  EXPECT_THROW(tournament::handlers::handle_detect_no_show(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DetectNoShow, RejectsAlreadyDetected) {
  auto state = make_existing_tournament();
  state.no_show_players.insert("70");  // hex("p") = 70
  examples::DetectNoShow cmd;
  cmd.set_player_root("p");
  EXPECT_THROW(tournament::handlers::handle_detect_no_show(cmd, state),
               angzarr::CommandRejectedError);
}

// --- AdvancedHandlers: penalty ---

TEST(MissedHandsFor, VerbalWarningZero) {
  EXPECT_EQ(tournament::handlers::missed_hands_for("VERBAL_WARNING", 5, 8), 0);
}

TEST(MissedHandsFor, MissedHandOne) {
  EXPECT_EQ(tournament::handlers::missed_hands_for("MISSED_HAND", 5, 8), 1);
}

TEST(MissedHandsFor, MissedRoundComputed) {
  EXPECT_EQ(tournament::handlers::missed_hands_for("MISSED_ROUND", 2, 8), 16);
  EXPECT_EQ(tournament::handlers::missed_hands_for("MISSED_ROUND", 0, 8), 0);
  EXPECT_EQ(tournament::handlers::missed_hands_for("MISSED_ROUND", 3, 0),
            3);  // table>=1
}

TEST(MissedHandsFor, DqIsMax) {
  EXPECT_EQ(tournament::handlers::missed_hands_for("DISQUALIFIED", 0, 0),
            std::numeric_limits<int32_t>::max());
}

TEST(MissedHandsFor, EmptyOrUnknownReturnsZero) {
  EXPECT_EQ(tournament::handlers::missed_hands_for("", 1, 1), 0);
  EXPECT_EQ(tournament::handlers::missed_hands_for("FOOBAR", 1, 1), 0);
}

TEST(IssuePenalty, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::IssuePenalty cmd;
  cmd.set_player_root("p");
  cmd.set_type("MISSED_HAND");
  EXPECT_THROW(tournament::handlers::handle_issue_penalty(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(IssuePenalty, RejectsEmptyPlayer) {
  auto state = make_existing_tournament();
  examples::IssuePenalty cmd;
  cmd.set_type("MISSED_HAND");
  EXPECT_THROW(tournament::handlers::handle_issue_penalty(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(IssuePenalty, RejectsInvalidType) {
  auto state = make_existing_tournament();
  examples::IssuePenalty cmd;
  cmd.set_player_root("p");
  cmd.set_type("INVALID");
  EXPECT_THROW(tournament::handlers::handle_issue_penalty(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(IssuePenalty, EmitsMissedRoundCalculation) {
  auto state = make_existing_tournament();
  examples::IssuePenalty cmd;
  cmd.set_player_root("p");
  cmd.set_type("MISSED_ROUND");
  cmd.set_rounds(2);
  cmd.set_table_size(9);
  auto event = tournament::handlers::handle_issue_penalty(cmd, state);
  EXPECT_EQ(event.type(), "MISSED_ROUND");
  EXPECT_EQ(event.missed_hands(), 18);
  EXPECT_EQ(event.rounds(), 2);
}

TEST(DecrementPenalty, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::DecrementPenalty cmd;
  cmd.set_player_root("p");
  EXPECT_THROW(tournament::handlers::handle_decrement_penalty(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DecrementPenalty, RejectsNotOnPenalty) {
  auto state = make_existing_tournament();
  examples::DecrementPenalty cmd;
  cmd.set_player_root("p");
  EXPECT_THROW(tournament::handlers::handle_decrement_penalty(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DecrementPenalty, DecrementsRemaining) {
  auto state = make_existing_tournament();
  state.active_penalties["70"] =
      tournament::ActivePenalty("MISSED_ROUND", 3);  // hex("p")=70
  examples::DecrementPenalty cmd;
  cmd.set_player_root("p");
  auto event = tournament::handlers::handle_decrement_penalty(cmd, state);
  EXPECT_EQ(event.rounds_remaining(), 2);
}

TEST(DecrementPenalty, FloorsAtZero) {
  auto state = make_existing_tournament();
  state.active_penalties["70"] = tournament::ActivePenalty("MISSED_HAND", 1);
  examples::DecrementPenalty cmd;
  cmd.set_player_root("p");
  auto event = tournament::handlers::handle_decrement_penalty(cmd, state);
  EXPECT_EQ(event.rounds_remaining(), 0);
}

TEST(DisqualifyPlayer, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::DisqualifyPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_reason("cheating");
  EXPECT_THROW(tournament::handlers::handle_disqualify_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DisqualifyPlayer, RejectsEmptyReason) {
  auto state = make_existing_tournament();
  examples::DisqualifyPlayer cmd;
  cmd.set_player_root("p");
  EXPECT_THROW(tournament::handlers::handle_disqualify_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DisqualifyPlayer, EmitsRemovedChips) {
  auto state = make_existing_tournament();
  state.starting_stack = 75000;
  examples::DisqualifyPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_reason("cheating");
  auto event = tournament::handlers::handle_disqualify_player(cmd, state);
  EXPECT_EQ(event.reason(), "cheating");
  EXPECT_EQ(event.chips_removed(), 75000);
}

// --- AdvancedHandlers: day_end ---

TEST(StopNewHands, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::StopNewHands cmd;
  EXPECT_THROW(tournament::handlers::handle_stop_new_hands(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(StopNewHands, RejectsAlreadyHalted) {
  auto state = make_existing_tournament();
  state.new_hands_halted = true;
  examples::StopNewHands cmd;
  EXPECT_THROW(tournament::handlers::handle_stop_new_hands(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(StopNewHands, EmitsHalted) {
  auto state = make_existing_tournament();
  examples::StopNewHands cmd;
  cmd.set_reason("END_OF_DAY");
  auto event = tournament::handlers::handle_stop_new_hands(cmd, state);
  EXPECT_EQ(event.effective_at(), "AFTER_CURRENT_HAND");
  EXPECT_EQ(event.reason(), "END_OF_DAY");
}

TEST(BagAndTag, RejectsNotHalted) {
  auto state = make_existing_tournament();
  examples::BagAndTag cmd;
  EXPECT_THROW(tournament::handlers::handle_bag_and_tag(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(BagAndTag, EmitsComplete) {
  auto state = make_existing_tournament();
  state.new_hands_halted = true;
  examples::BagAndTag cmd;
  auto event = tournament::handlers::handle_bag_and_tag(cmd, state);
  EXPECT_TRUE(event.has_completed_at());
}

TEST(NextHorseVariant, CyclesThroughFive) {
  EXPECT_EQ(tournament::handlers::next_horse_variant(0),
            examples::TEXAS_HOLDEM);
  EXPECT_EQ(tournament::handlers::next_horse_variant(1), examples::OMAHA);
  EXPECT_EQ(tournament::handlers::next_horse_variant(2), examples::RAZZ);
  EXPECT_EQ(tournament::handlers::next_horse_variant(3),
            examples::SEVEN_CARD_STUD);
  EXPECT_EQ(tournament::handlers::next_horse_variant(4),
            examples::STUD_HI_LO_8B);
  EXPECT_EQ(tournament::handlers::next_horse_variant(5),
            examples::TEXAS_HOLDEM);
  EXPECT_EQ(tournament::handlers::next_horse_variant(6), examples::OMAHA);
}

TEST(NextHorseVariant, NegativeIndexWraps) {
  EXPECT_EQ(tournament::handlers::next_horse_variant(-1),
            examples::STUD_HI_LO_8B);
  EXPECT_EQ(tournament::handlers::next_horse_variant(-5),
            examples::TEXAS_HOLDEM);
}

TEST(RotateMixedGameVariant, RejectsNotRunning) {
  tournament::TournamentState state;
  examples::RotateMixedGameVariant cmd;
  EXPECT_THROW(
      tournament::handlers::handle_rotate_mixed_game_variant(cmd, state),
      angzarr::CommandRejectedError);
}

TEST(RotateMixedGameVariant, EmitsFromToFromState) {
  auto state = make_existing_tournament();
  state.mixed_game_index = 2;  // RAZZ → SEVEN_CARD_STUD
  examples::RotateMixedGameVariant cmd;
  auto event =
      tournament::handlers::handle_rotate_mixed_game_variant(cmd, state);
  EXPECT_EQ(event.from_variant(), examples::RAZZ);
  EXPECT_EQ(event.to_variant(), examples::SEVEN_CARD_STUD);
}

// =============================================================================
// Phase I-Cpp-v3 — Reservation aggregate (HIGH-EX-2.5.1)
// =============================================================================

#include "reservation_handlers.hpp"
#include "reservation_state.hpp"

TEST(ReservationHelpers, ToHexLowerCase) {
  std::string bytes;
  bytes.push_back(static_cast<char>(0xAB));
  bytes.push_back(static_cast<char>(0xCD));
  bytes.push_back(static_cast<char>(0x12));
  EXPECT_EQ(reservation::handlers::to_hex(bytes), "abcd12");
}

TEST(ReservationHelpers, NewReservationIdIs32Chars) {
  auto id = reservation::handlers::new_reservation_id_hex();
  EXPECT_EQ(id.length(), 32u);
}

TEST(ReservationHelpers, NewReservationIdsAreUnique) {
  auto a = reservation::handlers::new_reservation_id_hex();
  auto b = reservation::handlers::new_reservation_id_hex();
  EXPECT_NE(a, b);
}

// --- Buy-in ---

TEST(InitiateBuyIn, RejectsEmptyPlayer) {
  reservation::ReservationState state;
  examples::InitiateBuyIn cmd;
  cmd.set_table_root("t");
  cmd.mutable_amount()->set_amount(100);
  EXPECT_THROW(reservation::handlers::handle_initiate_buy_in(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(InitiateBuyIn, RejectsEmptyTable) {
  reservation::ReservationState state;
  examples::InitiateBuyIn cmd;
  cmd.set_player_root("p");
  cmd.mutable_amount()->set_amount(100);
  EXPECT_THROW(reservation::handlers::handle_initiate_buy_in(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(InitiateBuyIn, RejectsZeroAmount) {
  reservation::ReservationState state;
  examples::InitiateBuyIn cmd;
  cmd.set_player_root("p");
  cmd.set_table_root("t");
  cmd.mutable_amount()->set_amount(0);
  EXPECT_THROW(reservation::handlers::handle_initiate_buy_in(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(InitiateBuyIn, EmitsRequested) {
  reservation::ReservationState state;
  examples::InitiateBuyIn cmd;
  cmd.set_player_root("p");
  cmd.set_table_root("t");
  cmd.set_seat(5);
  cmd.mutable_amount()->set_amount(1000);
  auto event = reservation::handlers::handle_initiate_buy_in(cmd, state);
  EXPECT_EQ(event.player_root(), "p");
  EXPECT_EQ(event.table_root(), "t");
  EXPECT_EQ(event.seat(), 5);
  EXPECT_EQ(event.amount().amount(), 1000);
  EXPECT_EQ(event.reservation_id().length(), 16u);
}

TEST(ConfirmBuyIn, RejectsMissingPending) {
  reservation::ReservationState state;
  examples::ConfirmBuyIn cmd;
  cmd.set_reservation_id("x");
  EXPECT_THROW(reservation::handlers::handle_confirm_buy_in(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ConfirmBuyIn, EmitsConfirmedWithPendingData) {
  reservation::ReservationState state;
  reservation::PendingBuyIn p;
  p.player_root = "p1";
  p.table_root = "t1";
  p.seat = 2;
  p.amount = 5000;
  state.pending_buy_ins[reservation::handlers::to_hex("rid")] = p;
  examples::ConfirmBuyIn cmd;
  cmd.set_reservation_id("rid");
  auto event = reservation::handlers::handle_confirm_buy_in(cmd, state);
  EXPECT_EQ(event.player_root(), "p1");
  EXPECT_EQ(event.table_root(), "t1");
  EXPECT_EQ(event.seat(), 2);
  EXPECT_EQ(event.amount().amount(), 5000);
}

TEST(ReleaseBuyIn, RejectsMissingPending) {
  reservation::ReservationState state;
  examples::ReleaseBuyIn cmd;
  cmd.set_reservation_id("x");
  EXPECT_THROW(reservation::handlers::handle_release_buy_in(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReleaseBuyIn, EmitsReleasedWithReason) {
  reservation::ReservationState state;
  reservation::PendingBuyIn p;
  p.player_root = "p1";
  p.amount = 5000;
  state.pending_buy_ins[reservation::handlers::to_hex("rid")] = p;
  examples::ReleaseBuyIn cmd;
  cmd.set_reservation_id("rid");
  cmd.set_reason("seat-unavailable");
  auto event = reservation::handlers::handle_release_buy_in(cmd, state);
  EXPECT_EQ(event.reason(), "seat-unavailable");
  EXPECT_EQ(event.amount().amount(), 5000);
}

// --- Rebuy ---

TEST(InitiateRebuy, RejectsEmptyPlayer) {
  reservation::ReservationState state;
  examples::InitiateRebuy cmd;
  cmd.set_tournament_root("t");
  EXPECT_THROW(reservation::handlers::handle_initiate_rebuy(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(InitiateRebuy, RejectsEmptyTournament) {
  reservation::ReservationState state;
  examples::InitiateRebuy cmd;
  cmd.set_player_root("p");
  EXPECT_THROW(reservation::handlers::handle_initiate_rebuy(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(InitiateRebuy, EmitsRequestedWithDefaultFee) {
  reservation::ReservationState state;
  examples::InitiateRebuy cmd;
  cmd.set_player_root("p");
  cmd.set_tournament_root("t");
  cmd.set_table_root("tbl");
  cmd.set_seat(3);
  auto event = reservation::handlers::handle_initiate_rebuy(cmd, state);
  EXPECT_EQ(event.player_root(), "p");
  EXPECT_EQ(event.tournament_root(), "t");
  EXPECT_EQ(event.fee().amount(), reservation::handlers::kDefaultRebuyFee);
}

TEST(ConfirmRebuyFee, RejectsMissingPending) {
  reservation::ReservationState state;
  examples::ConfirmRebuyFee cmd;
  cmd.set_reservation_id("x");
  EXPECT_THROW(reservation::handlers::handle_confirm_rebuy_fee(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ConfirmRebuyFee, EmitsConfirmedWithPending) {
  reservation::ReservationState state;
  reservation::PendingRebuy p;
  p.player_root = "p1";
  p.tournament_root = "T";
  p.fee = 1000;
  state.pending_rebuys[reservation::handlers::to_hex("rid")] = p;
  examples::ConfirmRebuyFee cmd;
  cmd.set_reservation_id("rid");
  auto event = reservation::handlers::handle_confirm_rebuy_fee(cmd, state);
  EXPECT_EQ(event.player_root(), "p1");
  EXPECT_EQ(event.tournament_root(), "T");
  EXPECT_EQ(event.fee().amount(), 1000);
}

TEST(ReleaseRebuyFee, RejectsMissingPending) {
  reservation::ReservationState state;
  examples::ReleaseRebuyFee cmd;
  cmd.set_reservation_id("x");
  EXPECT_THROW(reservation::handlers::handle_release_rebuy_fee(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReleaseRebuyFee, EmitsReleased) {
  reservation::ReservationState state;
  reservation::PendingRebuy p;
  p.player_root = "p1";
  p.fee = 500;
  state.pending_rebuys[reservation::handlers::to_hex("rid")] = p;
  examples::ReleaseRebuyFee cmd;
  cmd.set_reservation_id("rid");
  cmd.set_reason("denied");
  auto event = reservation::handlers::handle_release_rebuy_fee(cmd, state);
  EXPECT_EQ(event.reason(), "denied");
  EXPECT_EQ(event.fee().amount(), 500);
}

// --- Registration ---

TEST(InitiateRegistration, RejectsEmptyPlayer) {
  reservation::ReservationState state;
  examples::InitiateTournamentRegistration cmd;
  cmd.set_tournament_root("t");
  EXPECT_THROW(reservation::handlers::handle_initiate_registration(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(InitiateRegistration, RejectsEmptyTournament) {
  reservation::ReservationState state;
  examples::InitiateTournamentRegistration cmd;
  cmd.set_player_root("p");
  EXPECT_THROW(reservation::handlers::handle_initiate_registration(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(InitiateRegistration, EmitsRequestedWithDefaultFee) {
  reservation::ReservationState state;
  examples::InitiateTournamentRegistration cmd;
  cmd.set_player_root("p");
  cmd.set_tournament_root("t");
  auto event = reservation::handlers::handle_initiate_registration(cmd, state);
  EXPECT_EQ(event.fee().amount(),
            reservation::handlers::kDefaultRegistrationFee);
  EXPECT_EQ(event.tournament_root(), "t");
}

TEST(ConfirmRegistration, RejectsMissingPending) {
  reservation::ReservationState state;
  examples::ConfirmRegistrationFee cmd;
  cmd.set_reservation_id("x");
  EXPECT_THROW(reservation::handlers::handle_confirm_registration(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ConfirmRegistration, EmitsConfirmed) {
  reservation::ReservationState state;
  reservation::PendingRegistration p;
  p.player_root = "p1";
  p.tournament_root = "T";
  p.fee = 1000;
  state.pending_registrations[reservation::handlers::to_hex("rid")] = p;
  examples::ConfirmRegistrationFee cmd;
  cmd.set_reservation_id("rid");
  auto event = reservation::handlers::handle_confirm_registration(cmd, state);
  EXPECT_EQ(event.player_root(), "p1");
  EXPECT_EQ(event.fee().amount(), 1000);
}

TEST(ReleaseRegistration, RejectsMissingPending) {
  reservation::ReservationState state;
  examples::ReleaseRegistrationFee cmd;
  cmd.set_reservation_id("x");
  EXPECT_THROW(reservation::handlers::handle_release_registration(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReleaseRegistration, EmitsReleased) {
  reservation::ReservationState state;
  reservation::PendingRegistration p;
  p.player_root = "p1";
  p.fee = 1000;
  state.pending_registrations[reservation::handlers::to_hex("rid")] = p;
  examples::ReleaseRegistrationFee cmd;
  cmd.set_reservation_id("rid");
  cmd.set_reason("closed");
  auto event = reservation::handlers::handle_release_registration(cmd, state);
  EXPECT_EQ(event.reason(), "closed");
  EXPECT_EQ(event.fee().amount(), 1000);
}

// =============================================================================
// Phase I-Cpp-v3 — Reservation PM (HIGH-EX-4.3)
// =============================================================================

#include "reservation_pm.hpp"

TEST(ReservationPM, InitialPhaseIsInitial) {
  reservation::pmg::ReservationPM pm;
  EXPECT_EQ(pm.phase_of("anything"),
            reservation::pmg::ReservationPhase::INITIAL);
}

TEST(ReservationPM, BuyInRequestedAdvancesToPending) {
  reservation::pmg::ReservationPM pm;
  examples::BuyInRequested e;
  e.set_reservation_id("rid");
  pm.on_buy_in_requested(e);
  EXPECT_EQ(pm.phase_of(reservation::pmg::ReservationPM::hex("rid")),
            reservation::pmg::ReservationPhase::BUY_IN_PENDING);
}

TEST(ReservationPM, BuyInConfirmedAdvancesToConfirmed) {
  reservation::pmg::ReservationPM pm;
  examples::BuyInConfirmed e;
  e.set_reservation_id("rid");
  pm.on_buy_in_confirmed(e);
  EXPECT_EQ(pm.phase_of(reservation::pmg::ReservationPM::hex("rid")),
            reservation::pmg::ReservationPhase::BUY_IN_CONFIRMED);
}

TEST(ReservationPM, BuyInReleasedAdvancesToReleased) {
  reservation::pmg::ReservationPM pm;
  examples::BuyInReservationReleased e;
  e.set_reservation_id("rid");
  pm.on_buy_in_released(e);
  EXPECT_EQ(pm.phase_of(reservation::pmg::ReservationPM::hex("rid")),
            reservation::pmg::ReservationPhase::RELEASED);
}

TEST(ReservationPM, RebuyRequestedAdvancesToPending) {
  reservation::pmg::ReservationPM pm;
  examples::RebuyRequested e;
  e.set_reservation_id("rid");
  pm.on_rebuy_requested(e);
  EXPECT_EQ(pm.phase_of(reservation::pmg::ReservationPM::hex("rid")),
            reservation::pmg::ReservationPhase::REBUY_PENDING);
}

TEST(ReservationPM, RebuyConfirmedAdvancesToConfirmed) {
  reservation::pmg::ReservationPM pm;
  examples::RebuyFeeConfirmed e;
  e.set_reservation_id("rid");
  pm.on_rebuy_confirmed(e);
  EXPECT_EQ(pm.phase_of(reservation::pmg::ReservationPM::hex("rid")),
            reservation::pmg::ReservationPhase::REBUY_CONFIRMED);
}

TEST(ReservationPM, RebuyReleasedAdvancesToReleased) {
  reservation::pmg::ReservationPM pm;
  examples::RebuyFeeReleased e;
  e.set_reservation_id("rid");
  pm.on_rebuy_released(e);
  EXPECT_EQ(pm.phase_of(reservation::pmg::ReservationPM::hex("rid")),
            reservation::pmg::ReservationPhase::RELEASED);
}

TEST(ReservationPM, RegistrationRequestedAdvancesToPending) {
  reservation::pmg::ReservationPM pm;
  examples::RegistrationRequested e;
  e.set_reservation_id("rid");
  pm.on_registration_requested(e);
  EXPECT_EQ(pm.phase_of(reservation::pmg::ReservationPM::hex("rid")),
            reservation::pmg::ReservationPhase::REGISTRATION_PENDING);
}

TEST(ReservationPM, RegistrationConfirmedAdvancesToConfirmed) {
  reservation::pmg::ReservationPM pm;
  examples::RegistrationFeeConfirmed e;
  e.set_reservation_id("rid");
  pm.on_registration_confirmed(e);
  EXPECT_EQ(pm.phase_of(reservation::pmg::ReservationPM::hex("rid")),
            reservation::pmg::ReservationPhase::REGISTRATION_CONFIRMED);
}

TEST(ReservationPM, RegistrationReleasedAdvancesToReleased) {
  reservation::pmg::ReservationPM pm;
  examples::RegistrationFeeReleased e;
  e.set_reservation_id("rid");
  pm.on_registration_released(e);
  EXPECT_EQ(pm.phase_of(reservation::pmg::ReservationPM::hex("rid")),
            reservation::pmg::ReservationPhase::RELEASED);
}

TEST(ReservationPM, PhasesPerReservationAreIndependent) {
  reservation::pmg::ReservationPM pm;
  examples::BuyInRequested a;
  a.set_reservation_id("A");
  examples::RebuyRequested b;
  b.set_reservation_id("B");
  pm.on_buy_in_requested(a);
  pm.on_rebuy_requested(b);
  EXPECT_EQ(pm.phase_of(reservation::pmg::ReservationPM::hex("A")),
            reservation::pmg::ReservationPhase::BUY_IN_PENDING);
  EXPECT_EQ(pm.phase_of(reservation::pmg::ReservationPM::hex("B")),
            reservation::pmg::ReservationPhase::REBUY_PENDING);
  EXPECT_EQ(pm.phases().size(), 2u);
}

// =============================================================================
// Phase I-Cpp-v3 — Hand flow PM 9-phase transition table (HIGH-EX-4.1)
// =============================================================================

#include "hand_flow_pm.hpp"

TEST(HandFlowNextPhase, HandStartedFromWaiting) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::WAITING_FOR_START,
                                     "HandStarted", 0, 0),
            hand_flow_oo::HandPhase::DEALING);
}

TEST(HandFlowNextPhase, CardsDealtDealingToPostingBlinds) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::DEALING,
                                     "CardsDealt", 0, 0),
            hand_flow_oo::HandPhase::POSTING_BLINDS);
}

TEST(HandFlowNextPhase, CardsDealtFromOtherStaysSame) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::BETTING,
                                     "CardsDealt", 0, 0),
            hand_flow_oo::HandPhase::BETTING);
}

TEST(HandFlowNextPhase, BlindPostedPostingToBetting) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::POSTING_BLINDS,
                                     "BlindPosted", 0, 0),
            hand_flow_oo::HandPhase::BETTING);
}

TEST(HandFlowNextPhase, CommunityCardsDealtFromDealingCommunity) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::DEALING_COMMUNITY,
                                     "CommunityCardsDealt", examples::FLOP, 0),
            hand_flow_oo::HandPhase::BETTING);
}

TEST(HandFlowNextPhase, CommunityCardsDealtFromDraw) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::DRAW,
                                     "CommunityCardsDealt", examples::DRAW, 0),
            hand_flow_oo::HandPhase::BETTING);
}

TEST(HandFlowNextPhase, ShowdownStartedFromBetting) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::BETTING,
                                     "ShowdownStarted", 0, 0),
            hand_flow_oo::HandPhase::SHOWDOWN);
}

TEST(HandFlowNextPhase, ShowdownStartedFromCompleteStays) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::COMPLETE,
                                     "ShowdownStarted", 0, 0),
            hand_flow_oo::HandPhase::COMPLETE);
}

TEST(HandFlowNextPhase, PotAwardedFirstToAwarding) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::SHOWDOWN,
                                     "PotAwarded", 0, 0),
            hand_flow_oo::HandPhase::AWARDING_POT);
}

TEST(HandFlowNextPhase, PotAwardedFromAwardingToComplete) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::AWARDING_POT,
                                     "PotAwarded", 0, 0),
            hand_flow_oo::HandPhase::COMPLETE);
}

TEST(HandFlowNextPhase, BettingRoundClosedHoldemPreflop) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::BETTING,
                                     "BettingRoundClosed", examples::PREFLOP,
                                     examples::TEXAS_HOLDEM),
            hand_flow_oo::HandPhase::DEALING_COMMUNITY);
}

TEST(HandFlowNextPhase, BettingRoundClosedHoldemRiverToShowdown) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::BETTING,
                                     "BettingRoundClosed", examples::RIVER,
                                     examples::TEXAS_HOLDEM),
            hand_flow_oo::HandPhase::SHOWDOWN);
}

TEST(HandFlowNextPhase, BettingRoundClosedDrawPreflopToDraw) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::BETTING,
                                     "BettingRoundClosed", examples::PREFLOP,
                                     examples::FIVE_CARD_DRAW),
            hand_flow_oo::HandPhase::DRAW);
}

TEST(HandFlowNextPhase, BettingRoundClosedDrawFromDrawPhaseToBetting) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::DRAW,
                                     "BettingRoundClosed", examples::DRAW,
                                     examples::FIVE_CARD_DRAW),
            hand_flow_oo::HandPhase::BETTING);
}

TEST(HandFlowNextPhase, BettingRoundClosedDrawSecondBettingToShowdown) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::BETTING,
                                     "BettingRoundClosed", examples::DRAW,
                                     examples::FIVE_CARD_DRAW),
            hand_flow_oo::HandPhase::SHOWDOWN);
}

TEST(HandFlowNextPhase, UnknownEventLeavesPhase) {
  EXPECT_EQ(hand_flow_oo::next_phase(hand_flow_oo::HandPhase::BETTING,
                                     "Unknown", 0, 0),
            hand_flow_oo::HandPhase::BETTING);
}

TEST(AdvancePhaseHoldem, FlopAdvancesToDealingCommunity) {
  EXPECT_EQ(hand_flow_oo::advance_phase_holdem(examples::FLOP),
            hand_flow_oo::HandPhase::DEALING_COMMUNITY);
}

TEST(AdvancePhaseHoldem, TurnAdvancesToDealingCommunity) {
  EXPECT_EQ(hand_flow_oo::advance_phase_holdem(examples::TURN),
            hand_flow_oo::HandPhase::DEALING_COMMUNITY);
}

TEST(AdvancePhaseHoldem, UnknownStaysBetting) {
  EXPECT_EQ(hand_flow_oo::advance_phase_holdem(examples::SHOWDOWN),
            hand_flow_oo::HandPhase::BETTING);
}

TEST(AdvancePhaseDraw, FromDrawPhaseInDrawStateToBetting) {
  EXPECT_EQ(hand_flow_oo::advance_phase_draw(examples::DRAW,
                                             hand_flow_oo::HandPhase::DRAW),
            hand_flow_oo::HandPhase::BETTING);
}

TEST(AdvancePhaseDraw, FromDrawPhaseInBettingStateToShowdown) {
  EXPECT_EQ(hand_flow_oo::advance_phase_draw(examples::DRAW,
                                             hand_flow_oo::HandPhase::BETTING),
            hand_flow_oo::HandPhase::SHOWDOWN);
}

// =============================================================================
// Phase I-Cpp-v3 — DeductReservedFunds / SeatPlayer / AddRebuyChips
// =============================================================================

#include "add_rebuy_chips_handler.hpp"
#include "deduct_reserved_handler.hpp"
#include "seat_player_handler.hpp"

namespace {

player::PlayerState make_player_with_reservation(int64_t bankroll,
                                                 int64_t reserved,
                                                 const std::string& key_hex,
                                                 int64_t key_amount) {
  player::PlayerState s;
  s.player_id = "p1";
  s.bankroll = bankroll;
  s.reserved_funds = reserved;
  s.table_reservations[key_hex] = key_amount;
  return s;
}

table::TableState make_existing_table_with_seat(int seat_pos,
                                                const std::string& player_root,
                                                int64_t stack) {
  table::TableState s;
  s.table_id = "t1";
  s.max_players = 9;
  if (!player_root.empty()) {
    table::SeatState seat;
    seat.position = seat_pos;
    seat.player_root = player_root;
    seat.stack = stack;
    s.seats[seat_pos] = seat;
  }
  return s;
}

}  // namespace

// --- DeductReservedFunds ---

TEST(DeductReservedFunds, RejectsNonExistentPlayer) {
  player::PlayerState state;
  examples::DeductReservedFunds cmd;
  cmd.mutable_amount()->set_amount(100);
  cmd.set_key("k");
  EXPECT_THROW(player::handlers::handle_deduct_reserved_funds(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DeductReservedFunds, RejectsZeroAmount) {
  auto state =
      make_player_with_reservation(1000, 500, "6b", 500);  // hex("k")=6b
  examples::DeductReservedFunds cmd;
  cmd.mutable_amount()->set_amount(0);
  cmd.set_key("k");
  EXPECT_THROW(player::handlers::handle_deduct_reserved_funds(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DeductReservedFunds, RejectsMissingKey) {
  player::PlayerState state;
  state.player_id = "p";
  state.bankroll = 1000;
  examples::DeductReservedFunds cmd;
  cmd.mutable_amount()->set_amount(100);
  cmd.set_key("k");
  EXPECT_THROW(player::handlers::handle_deduct_reserved_funds(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DeductReservedFunds, RejectsOverDeduction) {
  auto state = make_player_with_reservation(1000, 200, "6b", 200);
  examples::DeductReservedFunds cmd;
  cmd.mutable_amount()->set_amount(500);
  cmd.set_key("k");
  EXPECT_THROW(player::handlers::handle_deduct_reserved_funds(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DeductReservedFunds, EmitsBothBankrollAndReservedDecrease) {
  auto state = make_player_with_reservation(1000, 500, "6b", 500);
  examples::DeductReservedFunds cmd;
  cmd.mutable_amount()->set_amount(300);
  cmd.set_key("k");
  auto event = player::handlers::handle_deduct_reserved_funds(cmd, state);
  EXPECT_EQ(event.new_balance().amount(), 700);
  EXPECT_EQ(event.new_reserved_balance().amount(), 200);
}

TEST(DeductReservedFunds, ExactReservationAmountAccepted) {
  auto state = make_player_with_reservation(1000, 500, "6b", 500);
  examples::DeductReservedFunds cmd;
  cmd.mutable_amount()->set_amount(500);
  cmd.set_key("k");
  auto event = player::handlers::handle_deduct_reserved_funds(cmd, state);
  EXPECT_EQ(event.new_balance().amount(), 500);
  EXPECT_EQ(event.new_reserved_balance().amount(), 0);
}

// --- SeatPlayer ---

TEST(SeatPlayer, RejectsNonExistentTable) {
  table::TableState state;
  examples::SeatPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_amount(100);
  EXPECT_THROW(table::handlers::handle_seat_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(SeatPlayer, RejectsEmptyPlayer) {
  auto state = make_existing_table_with_seat(0, "", 0);
  examples::SeatPlayer cmd;
  cmd.set_amount(100);
  EXPECT_THROW(table::handlers::handle_seat_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(SeatPlayer, RejectsZeroAmount) {
  auto state = make_existing_table_with_seat(0, "", 0);
  examples::SeatPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_amount(0);
  EXPECT_THROW(table::handlers::handle_seat_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(SeatPlayer, RejectsOccupiedSeat) {
  auto state = make_existing_table_with_seat(3, "other", 100);
  examples::SeatPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_amount(100);
  cmd.set_seat(3);
  EXPECT_THROW(table::handlers::handle_seat_player(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(SeatPlayer, AutoAssignsSeatWhenNegative) {
  auto state = make_existing_table_with_seat(0, "", 0);
  examples::SeatPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_amount(500);
  cmd.set_seat(-1);
  auto event = table::handlers::handle_seat_player(cmd, state);
  EXPECT_GE(event.seat_position(), 0);
  EXPECT_EQ(event.stack(), 500);
}

TEST(SeatPlayer, ExplicitSeatPreserved) {
  auto state = make_existing_table_with_seat(0, "", 0);
  examples::SeatPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_amount(500);
  cmd.set_seat(4);
  auto event = table::handlers::handle_seat_player(cmd, state);
  EXPECT_EQ(event.seat_position(), 4);
}

// --- AddRebuyChips ---

TEST(AddRebuyChips, RejectsNonExistentTable) {
  table::TableState state;
  examples::AddRebuyChips cmd;
  cmd.set_player_root("p");
  cmd.set_amount(100);
  EXPECT_THROW(table::handlers::handle_add_rebuy_chips(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(AddRebuyChips, RejectsUnseatedPlayer) {
  auto state = make_existing_table_with_seat(0, "", 0);
  examples::AddRebuyChips cmd;
  cmd.set_player_root("p");
  cmd.set_amount(100);
  EXPECT_THROW(table::handlers::handle_add_rebuy_chips(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(AddRebuyChips, RejectsZeroAmount) {
  auto state = make_existing_table_with_seat(3, "p", 500);
  examples::AddRebuyChips cmd;
  cmd.set_player_root("p");
  cmd.set_amount(0);
  EXPECT_THROW(table::handlers::handle_add_rebuy_chips(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(AddRebuyChips, EmitsAddedWithNewStack) {
  auto state = make_existing_table_with_seat(3, "p", 500);
  examples::AddRebuyChips cmd;
  cmd.set_player_root("p");
  cmd.set_seat(3);
  cmd.set_amount(1000);
  auto event = table::handlers::handle_add_rebuy_chips(cmd, state);
  EXPECT_EQ(event.amount(), 1000);
  EXPECT_EQ(event.new_stack(), 1500);
  EXPECT_EQ(event.seat(), 3);
}

// =============================================================================
// Phase I-Cpp-v3 boundary kills — drive surviving mull mutants to dead
// =============================================================================

// seat_player_handler:39 — `seat_position >= 0` survives `>` because no test
// asks for seat=0 explicitly.
TEST(SeatPlayer, ExplicitSeatZeroAccepted) {
  auto state = make_existing_table_with_seat(99, "", 0);  // seat 0 empty
  examples::SeatPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_amount(500);
  cmd.set_seat(0);
  auto event = table::handlers::handle_seat_player(cmd, state);
  EXPECT_EQ(event.seat_position(), 0);
}

// advanced_handlers:131 — `real_seconds < 0` accepts exactly 0; without an
// exact-zero acceptance test the mutation `< => <=` survives.
TEST(RecordHandForHandHand, AcceptsExactlyZeroSeconds) {
  auto state = make_h4h_tournament();
  examples::RecordHandForHandHand cmd;
  cmd.set_real_seconds(0);
  auto event =
      tournament::handlers::handle_record_hand_for_hand_hand(cmd, state);
  EXPECT_EQ(event.real_seconds(), 0);
  EXPECT_EQ(event.clock_seconds_deducted(), 0);
}

// advanced_handlers:150 — `denominations <= 0` rejects 0; pin exactly the
// boundary in both directions.
TEST(ColorUp, RejectsZeroNewDenomination) {
  auto state = make_existing_tournament();
  examples::ColorUp cmd;
  cmd.set_retire_denomination(100);
  cmd.set_new_denomination(0);
  EXPECT_THROW(tournament::handlers::handle_color_up(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ColorUp, AcceptsBothPositive) {
  auto state = make_existing_tournament();
  examples::ColorUp cmd;
  cmd.set_retire_denomination(1);
  cmd.set_new_denomination(2);
  auto event = tournament::handlers::handle_color_up(cmd, state);
  EXPECT_EQ(event.retired_denomination(), 1);
}

// advanced_handlers:213 — `tables_remaining < 1` accepts exactly 1.
TEST(TriggerSeatRedraw, AcceptsExactlyOne) {
  auto state = make_existing_tournament();
  examples::TriggerSeatRedraw cmd;
  cmd.set_tables_remaining(1);
  auto event = tournament::handlers::handle_trigger_seat_redraw(cmd, state);
  EXPECT_EQ(event.trigger(), "FINAL_TABLE");
}

// advanced_handlers:269 — `<= 0` blinds boundary.
TEST(AdvanceAbsentBlind, RejectsZeroBigBlind) {
  auto state = make_existing_tournament();
  examples::AdvanceAbsentBlind cmd;
  cmd.set_small_blind(50);
  cmd.set_big_blind(0);
  EXPECT_THROW(tournament::handlers::handle_advance_absent_blind(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(AdvanceAbsentBlind, AcceptsExactlyOne) {
  auto state = make_existing_tournament();
  examples::AdvanceAbsentBlind cmd;
  cmd.set_small_blind(1);
  cmd.set_big_blind(1);
  auto event = tournament::handlers::handle_advance_absent_blind(cmd, state);
  EXPECT_EQ(event.stack_delta(), 2);
}

// create_handler:40 — `starting_stack <= 0` boundary.
TEST(CreateTournamentHandler, RejectsZeroStartingStack) {
  tournament::TournamentState state;
  examples::CreateTournament cmd;
  cmd.set_name("X");
  cmd.set_buy_in(1);
  cmd.set_starting_stack(0);
  cmd.set_max_players(2);
  EXPECT_THROW(tournament::handlers::handle_create_tournament(cmd, state),
               angzarr::CommandRejectedError);
}

// create_handler:43 — `max_players < 2` accepts exactly 2.
TEST(CreateTournamentHandler, AcceptsExactlyTwoMaxPlayers) {
  tournament::TournamentState state;
  examples::CreateTournament cmd;
  cmd.set_name("X");
  cmd.set_buy_in(1);
  cmd.set_starting_stack(1);
  cmd.set_max_players(2);
  auto event = tournament::handlers::handle_create_tournament(cmd, state);
  EXPECT_EQ(event.max_players(), 2);
}

// lifecycle_handler:45,46 — idx = next - 1 + `idx >= size` capping. Add tests
// that pin the indexing math.
TEST(AdvanceBlindLevel, FirstLevelUsesFirstBlind) {
  auto state = make_existing_tournament();
  examples::BlindLevel b1;
  b1.set_small_blind(25);
  b1.set_big_blind(50);
  examples::BlindLevel b2;
  b2.set_small_blind(50);
  b2.set_big_blind(100);
  state.blind_structure.push_back(b1);
  state.blind_structure.push_back(b2);
  state.current_level = 0;
  examples::AdvanceBlindLevel cmd;
  auto event = tournament::handlers::handle_advance_blind_level(cmd, state);
  EXPECT_EQ(event.level(), 1);
  EXPECT_EQ(event.small_blind(), 25);  // first blind (idx 0 = next 1 - 1)
}

TEST(AdvanceBlindLevel, SecondLevelUsesSecondBlind) {
  auto state = make_existing_tournament();
  examples::BlindLevel b1;
  b1.set_small_blind(25);
  examples::BlindLevel b2;
  b2.set_small_blind(50);
  state.blind_structure.push_back(b1);
  state.blind_structure.push_back(b2);
  state.current_level = 1;
  examples::AdvanceBlindLevel cmd;
  auto event = tournament::handlers::handle_advance_blind_level(cmd, state);
  EXPECT_EQ(event.level(), 2);
  EXPECT_EQ(event.small_blind(), 50);
}

TEST(AdvanceBlindLevel, AtExactSizeCapsAtLast) {
  auto state = make_existing_tournament();
  examples::BlindLevel b1;
  b1.set_small_blind(100);
  state.blind_structure.push_back(b1);
  state.current_level = 1;  // next = 2, idx = 1, size = 1 → cap
  examples::AdvanceBlindLevel cmd;
  auto event = tournament::handlers::handle_advance_blind_level(cmd, state);
  EXPECT_EQ(event.small_blind(), 100);
}

// rebuy_handler:56 — `current_level > cutoff` accepts equal-to-cutoff.
TEST(ProcessRebuy, AcceptsAtExactCutoff) {
  auto state = make_existing_tournament();
  state.has_rebuy_config = true;
  state.rebuy_config.set_enabled(true);
  state.rebuy_config.set_rebuy_level_cutoff(5);
  state.rebuy_config.set_rebuy_cost(100);
  state.rebuy_config.set_rebuy_chips(1000);
  state.current_level = 5;  // exactly cutoff → still allowed
  examples::PlayerRegistration r;
  state.registered_players["01"] = r;
  examples::ProcessRebuy cmd;
  cmd.set_player_root(std::string("\x01", 1));
  auto result = tournament::handlers::handle_process_rebuy(cmd, state);
  auto* ok = std::get_if<examples::RebuyProcessed>(&result);
  ASSERT_NE(ok, nullptr);
}

// Reservation: kill the `i < 8` mutant by asserting the byte ID length and
// that the second 8 bytes are populated (lo word is used). The for-loop bug
// would zero one of the halves.
TEST(ReservationHelpers, BothHighAndLowBytesPopulated) {
  // Run 30 generations and check at least one byte in [0..7] and [8..15]
  // ranges is non-zero across them. Vanishing chance both halves are all-zero.
  bool any_high_nonzero = false;
  bool any_low_nonzero = false;
  for (int trial = 0; trial < 30; ++trial) {
    auto id = reservation::handlers::new_reservation_id_hex();
    ASSERT_EQ(id.length(), 32u);
    // hex of bytes 0..7 → chars 0..15; bytes 8..15 → chars 16..31.
    for (int i = 0; i < 16; ++i) {
      if (id[i] != '0') any_high_nonzero = true;
    }
    for (int i = 16; i < 32; ++i) {
      if (id[i] != '0') any_low_nonzero = true;
    }
  }
  EXPECT_TRUE(any_high_nonzero);
  EXPECT_TRUE(any_low_nonzero);
}

// seat_player_handler:39 — `seat_position >= 0` test where seat 0 is occupied
// and we request seat 0 explicitly: must throw (occupied). With `>` → 0
// mutation the handler would take auto-assign path and succeed (silent bug).
TEST(SeatPlayer, ExplicitSeatZeroRejectedWhenOccupied) {
  auto state = make_existing_table_with_seat(0, "other", 100);
  examples::SeatPlayer cmd;
  cmd.set_player_root("p");
  cmd.set_amount(500);
  cmd.set_seat(0);
  EXPECT_THROW(table::handlers::handle_seat_player(cmd, state),
               angzarr::CommandRejectedError);
}

// =============================================================================
// PR #12 cascade — 15 new command handlers (14 hand + 1 table).
// =============================================================================
//
// Tests below target the new handlers as a TDD cascade. Each handler gets
// at least: happy-path emit + every reject branch. Pure helpers
// (select_door_card_index, move_skips_a_blind) get truth-table tests so
// mull mutations on those functions are killable directly.

#include "change_seats_handler.hpp"
#include "misdeal_handlers.hpp"
#include "premature_handlers.hpp"
#include "stud_handlers.hpp"

namespace {

// Build a minimal HandState in "betting / PREFLOP" status with one player.
hand::HandState make_hand_state_with_player(const std::string& root,
                                            int position = 0) {
  hand::HandState state;
  state.hand_id = "h1";
  state.table_root = "t1";
  state.hand_number = 1;
  state.status = "betting";
  state.current_phase = examples::PREFLOP;
  hand::PlayerHandInfo p;
  p.player_root = root;
  p.position = position;
  p.stack = 1000;
  p.has_acted = false;
  p.has_folded = false;
  state.players[position] = p;
  state.pots.push_back(hand::PotInfo{0, {root}, "main"});
  return state;
}

hand::HandState make_completed_hand() {
  auto s = make_hand_state_with_player("p1");
  s.status = "complete";
  return s;
}

examples::Card make_card(examples::Suit suit, int rank) {
  examples::Card c;
  c.set_suit(suit);
  c.set_rank(static_cast<examples::Rank>(rank));
  return c;
}

table::TableState make_table_with_seat(int seat, const std::string& root,
                                       int max_players = 9,
                                       int dealer_position = 0, int64_t sb = 5,
                                       int64_t bb = 10) {
  table::TableState s;
  s.table_id = "t1";
  s.max_players = max_players;
  s.dealer_position = dealer_position;
  s.small_blind = sb;
  s.big_blind = bb;
  table::SeatState seat_state;
  seat_state.position = seat;
  seat_state.player_root = root;
  seat_state.stack = 1000;
  s.seats[seat] = seat_state;
  return s;
}

}  // namespace

// --- DeclareMisdeal -------------------------------------------------------

TEST(DeclareMisdeal, RejectsWhenHandNotDealt) {
  hand::HandState empty;
  examples::DeclareMisdeal cmd;
  cmd.set_reason("BUTTON_CARD_EXPOSED");
  EXPECT_THROW(hand::handlers::handle_declare_misdeal(cmd, empty),
               angzarr::CommandRejectedError);
}

TEST(DeclareMisdeal, RejectsWhenComplete) {
  auto state = make_completed_hand();
  examples::DeclareMisdeal cmd;
  cmd.set_reason("X");
  EXPECT_THROW(hand::handlers::handle_declare_misdeal(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DeclareMisdeal, RejectsEmptyReason) {
  auto state = make_hand_state_with_player("p1");
  examples::DeclareMisdeal cmd;
  EXPECT_THROW(hand::handlers::handle_declare_misdeal(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DeclareMisdeal, RejectsPostSubstantialAction) {
  auto state = make_hand_state_with_player("p1");
  state.current_phase = examples::FLOP;
  examples::DeclareMisdeal cmd;
  cmd.set_reason("X");
  EXPECT_THROW(hand::handlers::handle_declare_misdeal(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DeclareMisdeal, RejectsWhenAnyPlayerActed) {
  auto state = make_hand_state_with_player("p1");
  state.players[0].has_acted = true;
  examples::DeclareMisdeal cmd;
  cmd.set_reason("X");
  EXPECT_THROW(hand::handlers::handle_declare_misdeal(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DeclareMisdeal, EmitsReasonAndPreservedFlag) {
  auto state = make_hand_state_with_player("p1");
  examples::DeclareMisdeal cmd;
  cmd.set_reason("EXPOSED_STUD_DOWNCARD");
  cmd.set_dealer_button_preserved(true);
  auto event = hand::handlers::handle_declare_misdeal(cmd, state);
  EXPECT_EQ(event.reason(), "EXPOSED_STUD_DOWNCARD");
  EXPECT_TRUE(event.dealer_button_preserved());
  EXPECT_TRUE(event.has_declared_at());
}

TEST(DeclareMisdeal, ButtonPreservedDefaultsFalse) {
  auto state = make_hand_state_with_player("p1");
  examples::DeclareMisdeal cmd;
  cmd.set_reason("X");
  auto event = hand::handlers::handle_declare_misdeal(cmd, state);
  EXPECT_FALSE(event.dealer_button_preserved());
}

// --- ReportFouledDeck -----------------------------------------------------

TEST(ReportFouledDeck, RejectsWhenHandMissing) {
  hand::HandState empty;
  examples::ReportFouledDeck cmd;
  cmd.set_duplicate_card("Ah");
  EXPECT_THROW(hand::handlers::handle_report_fouled_deck(cmd, empty),
               angzarr::CommandRejectedError);
}

TEST(ReportFouledDeck, RejectsWhenComplete) {
  auto state = make_completed_hand();
  examples::ReportFouledDeck cmd;
  cmd.set_duplicate_card("Ah");
  EXPECT_THROW(hand::handlers::handle_report_fouled_deck(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReportFouledDeck, RejectsEmptyDuplicateCard) {
  auto state = make_hand_state_with_player("p1");
  examples::ReportFouledDeck cmd;
  EXPECT_THROW(hand::handlers::handle_report_fouled_deck(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReportFouledDeck, EmitsDuplicateCardVerbatim) {
  auto state = make_hand_state_with_player("p1");
  examples::ReportFouledDeck cmd;
  cmd.set_duplicate_card("Ks");
  auto event = hand::handlers::handle_report_fouled_deck(cmd, state);
  EXPECT_EQ(event.duplicate_card(), "Ks");
  EXPECT_TRUE(event.has_detected_at());
}

// --- RedealHand -----------------------------------------------------------

TEST(RedealHand, RejectsWhenHandMissing) {
  hand::HandState empty;
  examples::RedealHand cmd;
  cmd.set_table_root("t1");
  cmd.set_hand_number(1);
  EXPECT_THROW(hand::handlers::handle_redeal_hand(cmd, empty),
               angzarr::CommandRejectedError);
}

TEST(RedealHand, RejectsEmptyTableRoot) {
  auto state = make_hand_state_with_player("p1");
  examples::RedealHand cmd;
  cmd.set_hand_number(1);
  EXPECT_THROW(hand::handlers::handle_redeal_hand(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(RedealHand, RejectsZeroHandNumber) {
  auto state = make_hand_state_with_player("p1");
  examples::RedealHand cmd;
  cmd.set_table_root("t1");
  EXPECT_THROW(hand::handlers::handle_redeal_hand(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(RedealHand, RejectsNegativeBlind) {
  auto state = make_hand_state_with_player("p1");
  examples::RedealHand cmd;
  cmd.set_table_root("t1");
  cmd.set_hand_number(1);
  cmd.set_small_blind(-1);
  EXPECT_THROW(hand::handlers::handle_redeal_hand(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(RedealHand, RejectsBigSmallerThanSmall) {
  auto state = make_hand_state_with_player("p1");
  examples::RedealHand cmd;
  cmd.set_table_root("t1");
  cmd.set_hand_number(1);
  cmd.set_small_blind(10);
  cmd.set_big_blind(5);
  EXPECT_THROW(hand::handlers::handle_redeal_hand(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(RedealHand, RejectsNegativeLevel) {
  auto state = make_hand_state_with_player("p1");
  examples::RedealHand cmd;
  cmd.set_table_root("t1");
  cmd.set_hand_number(1);
  cmd.set_level(-1);
  EXPECT_THROW(hand::handlers::handle_redeal_hand(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(RedealHand, EmitsAllFields) {
  auto state = make_hand_state_with_player("p1");
  examples::RedealHand cmd;
  cmd.set_table_root("t1");
  cmd.set_hand_number(7);
  cmd.set_dealer_position(3);
  cmd.set_small_blind(25);
  cmd.set_big_blind(50);
  cmd.set_level(4);
  auto event = hand::handlers::handle_redeal_hand(cmd, state);
  EXPECT_EQ(event.table_root(), "t1");
  EXPECT_EQ(event.hand_number(), 7);
  EXPECT_EQ(event.dealer_position(), 3);
  EXPECT_EQ(event.small_blind(), 25);
  EXPECT_EQ(event.big_blind(), 50);
  EXPECT_EQ(event.level(), 4);
  EXPECT_TRUE(event.has_redealt_at());
}

// --- Premature handlers ---------------------------------------------------

TEST(ReportPrematureFlop, RejectsWhenHandMissing) {
  hand::HandState empty;
  examples::ReportPrematureFlop cmd;
  EXPECT_THROW(hand::handlers::handle_report_premature_flop(cmd, empty),
               angzarr::CommandRejectedError);
}

TEST(ReportPrematureFlop, RejectsWhenAlreadyOnTurn) {
  auto state = make_hand_state_with_player("p1");
  state.current_phase = examples::TURN;
  examples::ReportPrematureFlop cmd;
  EXPECT_THROW(hand::handlers::handle_report_premature_flop(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReportPrematureFlop, EmitsTimestamp) {
  auto state = make_hand_state_with_player("p1");
  auto event = hand::handlers::handle_report_premature_flop(
      examples::ReportPrematureFlop{}, state);
  EXPECT_TRUE(event.has_detected_at());
}

TEST(ReportPrematureTurn, RejectsWhenAlreadyOnRiver) {
  auto state = make_hand_state_with_player("p1");
  state.current_phase = examples::RIVER;
  EXPECT_THROW(hand::handlers::handle_report_premature_turn(
                   examples::ReportPrematureTurn{}, state),
               angzarr::CommandRejectedError);
}

TEST(ReportPrematureTurn, EmitsTimestamp) {
  auto state = make_hand_state_with_player("p1");
  state.current_phase = examples::FLOP;
  auto event = hand::handlers::handle_report_premature_turn(
      examples::ReportPrematureTurn{}, state);
  EXPECT_TRUE(event.has_detected_at());
}

TEST(ReportPrematureRiver, RejectsAfterRiver) {
  auto state = make_hand_state_with_player("p1");
  state.current_phase = examples::RIVER;
  EXPECT_THROW(hand::handlers::handle_report_premature_river(
                   examples::ReportPrematureRiver{}, state),
               angzarr::CommandRejectedError);
}

TEST(ReportPrematureRiver, EmitsTimestamp) {
  auto state = make_hand_state_with_player("p1");
  state.current_phase = examples::TURN;
  auto event = hand::handlers::handle_report_premature_river(
      examples::ReportPrematureRiver{}, state);
  EXPECT_TRUE(event.has_detected_at());
}

TEST(ReportPrematureStudCard, RejectsUnspecifiedStreet) {
  auto state = make_hand_state_with_player("p1");
  examples::ReportPrematureStudCard cmd;
  EXPECT_THROW(hand::handlers::handle_report_premature_stud_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReportPrematureStudCard, EmitsStreet) {
  auto state = make_hand_state_with_player("p1");
  examples::ReportPrematureStudCard cmd;
  cmd.set_attempted_street(examples::FIFTH_STREET);
  auto event = hand::handlers::handle_report_premature_stud_card(cmd, state);
  EXPECT_EQ(event.attempted_street(), examples::FIFTH_STREET);
  EXPECT_TRUE(event.has_detected_at());
}

// --- ReplaceButtonCard ----------------------------------------------------

TEST(ReplaceButtonCard, RejectsEmptyPlayerRoot) {
  auto state = make_hand_state_with_player("p1");
  examples::ReplaceButtonCard cmd;
  *cmd.mutable_replacement_card() = make_card(examples::HEARTS, 5);
  EXPECT_THROW(hand::handlers::handle_replace_button_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReplaceButtonCard, RejectsUnknownPlayer) {
  auto state = make_hand_state_with_player("p1");
  examples::ReplaceButtonCard cmd;
  cmd.set_player_root("stranger");
  *cmd.mutable_replacement_card() = make_card(examples::HEARTS, 5);
  EXPECT_THROW(hand::handlers::handle_replace_button_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReplaceButtonCard, RejectsAfterPlayerActed) {
  auto state = make_hand_state_with_player("p1");
  state.players[0].has_acted = true;
  examples::ReplaceButtonCard cmd;
  cmd.set_player_root("p1");
  *cmd.mutable_replacement_card() = make_card(examples::HEARTS, 5);
  EXPECT_THROW(hand::handlers::handle_replace_button_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReplaceButtonCard, RejectsInvalidCard) {
  auto state = make_hand_state_with_player("p1");
  examples::ReplaceButtonCard cmd;
  cmd.set_player_root("p1");
  // suit unspecified
  EXPECT_THROW(hand::handlers::handle_replace_button_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReplaceButtonCard, EmitsReplacement) {
  auto state = make_hand_state_with_player("p1");
  examples::ReplaceButtonCard cmd;
  cmd.set_player_root("p1");
  *cmd.mutable_replacement_card() = make_card(examples::SPADES, 11);
  auto event = hand::handlers::handle_replace_button_card(cmd, state);
  EXPECT_EQ(event.player_root(), "p1");
  EXPECT_EQ(event.replacement_card().suit(), examples::SPADES);
  EXPECT_EQ(event.replacement_card().rank(), 11);
  EXPECT_TRUE(event.has_replaced_at());
}

// --- DealStudStreet -------------------------------------------------------

TEST(DealStudStreet, RejectsUnspecifiedStreet) {
  auto state = make_hand_state_with_player("p1");
  examples::DealStudStreet cmd;
  auto* up = cmd.add_up_cards();
  up->set_player_root("p1");
  EXPECT_THROW(hand::handlers::handle_deal_stud_street(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DealStudStreet, RejectsEmptyUpCards) {
  auto state = make_hand_state_with_player("p1");
  examples::DealStudStreet cmd;
  cmd.set_street(examples::FOURTH_STREET);
  EXPECT_THROW(hand::handlers::handle_deal_stud_street(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DealStudStreet, RejectsEntryMissingPlayerRoot) {
  auto state = make_hand_state_with_player("p1");
  examples::DealStudStreet cmd;
  cmd.set_street(examples::FOURTH_STREET);
  cmd.add_up_cards();  // empty player_root
  EXPECT_THROW(hand::handlers::handle_deal_stud_street(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DealStudStreet, EmitsEachEntry) {
  auto state = make_hand_state_with_player("p1");
  examples::DealStudStreet cmd;
  cmd.set_street(examples::FOURTH_STREET);
  auto* up = cmd.add_up_cards();
  up->set_player_root("p1");
  *up->add_up_cards() = make_card(examples::CLUBS, 9);
  auto event = hand::handlers::handle_deal_stud_street(cmd, state);
  EXPECT_EQ(event.street(), examples::FOURTH_STREET);
  ASSERT_EQ(event.up_cards_size(), 1);
  EXPECT_EQ(event.up_cards(0).player_root(), "p1");
  ASSERT_EQ(event.up_cards(0).up_cards_size(), 1);
  EXPECT_EQ(event.up_cards(0).up_cards(0).rank(), 9);
  EXPECT_TRUE(event.has_dealt_at());
}

// --- DealStudCommunityCard ------------------------------------------------

TEST(DealStudCommunityCard, RejectsUnspecifiedStreet) {
  auto state = make_hand_state_with_player("p1");
  state.remaining_deck.push_back(hand::Card{examples::HEARTS, 10});
  examples::DealStudCommunityCard cmd;
  cmd.add_shared_with("p1");
  EXPECT_THROW(hand::handlers::handle_deal_stud_community_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DealStudCommunityCard, RejectsEmptySharedWith) {
  auto state = make_hand_state_with_player("p1");
  state.remaining_deck.push_back(hand::Card{examples::HEARTS, 10});
  examples::DealStudCommunityCard cmd;
  cmd.set_street(examples::SEVENTH_STREET);
  EXPECT_THROW(hand::handlers::handle_deal_stud_community_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DealStudCommunityCard, RejectsUnknownPlayer) {
  auto state = make_hand_state_with_player("p1");
  state.remaining_deck.push_back(hand::Card{examples::HEARTS, 10});
  examples::DealStudCommunityCard cmd;
  cmd.set_street(examples::SEVENTH_STREET);
  cmd.add_shared_with("stranger");
  EXPECT_THROW(hand::handlers::handle_deal_stud_community_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DealStudCommunityCard, RejectsFoldedPlayer) {
  auto state = make_hand_state_with_player("p1");
  state.players[0].has_folded = true;
  state.remaining_deck.push_back(hand::Card{examples::HEARTS, 10});
  examples::DealStudCommunityCard cmd;
  cmd.set_street(examples::SEVENTH_STREET);
  cmd.add_shared_with("p1");
  EXPECT_THROW(hand::handlers::handle_deal_stud_community_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DealStudCommunityCard, RejectsEmptyStub) {
  auto state = make_hand_state_with_player("p1");
  examples::DealStudCommunityCard cmd;
  cmd.set_street(examples::SEVENTH_STREET);
  cmd.add_shared_with("p1");
  EXPECT_THROW(hand::handlers::handle_deal_stud_community_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(DealStudCommunityCard, EmitsTopOfStub) {
  auto state = make_hand_state_with_player("p1");
  state.remaining_deck.push_back(hand::Card{examples::DIAMONDS, 14});
  examples::DealStudCommunityCard cmd;
  cmd.set_street(examples::SEVENTH_STREET);
  cmd.add_shared_with("p1");
  auto event = hand::handlers::handle_deal_stud_community_card(cmd, state);
  EXPECT_EQ(event.card().rank(), 14);
  EXPECT_EQ(event.card().suit(), examples::DIAMONDS);
  ASSERT_EQ(event.shared_with_size(), 1);
  EXPECT_EQ(event.shared_with(0), "p1");
  EXPECT_TRUE(event.has_dealt_at());
}

// --- ScrambleAllDownCards / select_door_card_index ------------------------

TEST(SelectDoorCardIndex, ZeroHoleReturnsZero) {
  EXPECT_EQ(hand::handlers::select_door_card_index("seed", 0), 0u);
}

TEST(SelectDoorCardIndex, DifferentSeedsProduceDifferentIndices) {
  auto a = hand::handlers::select_door_card_index(std::string("seed-a"), 3);
  auto b = hand::handlers::select_door_card_index(std::string("seed-b"), 3);
  // Not strictly guaranteed but extremely likely with FNV-1a on differing
  // inputs.
  EXPECT_NE(a, b);
}

TEST(SelectDoorCardIndex, BoundedByHoleSize) {
  for (int i = 0; i < 100; ++i) {
    auto idx = hand::handlers::select_door_card_index(std::to_string(i), 3);
    EXPECT_LT(idx, 3u);
  }
}

TEST(ScrambleAllDownCards, RejectsEmptyPlayerRoot) {
  auto state = make_hand_state_with_player("p1");
  examples::ScrambleAllDownCards cmd;
  cmd.set_rng_seed("x");
  EXPECT_THROW(hand::handlers::handle_scramble_all_down_cards(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ScrambleAllDownCards, RejectsEmptyRngSeed) {
  auto state = make_hand_state_with_player("p1");
  state.players[0].hole_cards.push_back(hand::Card{examples::CLUBS, 2});
  examples::ScrambleAllDownCards cmd;
  cmd.set_player_root("p1");
  EXPECT_THROW(hand::handlers::handle_scramble_all_down_cards(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ScrambleAllDownCards, RejectsUnknownPlayer) {
  auto state = make_hand_state_with_player("p1");
  examples::ScrambleAllDownCards cmd;
  cmd.set_player_root("stranger");
  cmd.set_rng_seed("xx");
  EXPECT_THROW(hand::handlers::handle_scramble_all_down_cards(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ScrambleAllDownCards, RejectsEmptyHole) {
  auto state = make_hand_state_with_player("p1");
  examples::ScrambleAllDownCards cmd;
  cmd.set_player_root("p1");
  cmd.set_rng_seed("xx");
  EXPECT_THROW(hand::handlers::handle_scramble_all_down_cards(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ScrambleAllDownCards, EmitsSelectedCardAndEcoSeed) {
  auto state = make_hand_state_with_player("p1");
  state.players[0].hole_cards.push_back(hand::Card{examples::CLUBS, 2});
  state.players[0].hole_cards.push_back(hand::Card{examples::HEARTS, 7});
  state.players[0].hole_cards.push_back(hand::Card{examples::SPADES, 11});
  examples::ScrambleAllDownCards cmd;
  cmd.set_player_root("p1");
  cmd.set_rng_seed("supervisor-token");
  auto event = hand::handlers::handle_scramble_all_down_cards(cmd, state);
  EXPECT_EQ(event.player_root(), "p1");
  EXPECT_EQ(event.rng_seed(), "supervisor-token");
  // Door card must be one of the three hole cards.
  int rank = event.door_card().rank();
  EXPECT_TRUE(rank == 2 || rank == 7 || rank == 11);
  EXPECT_TRUE(event.has_selected_at());
}

// --- ReportExposedStudDowncard --------------------------------------------

TEST(ReportExposedStudDowncard, RejectsEmptyPlayerRoot) {
  auto state = make_hand_state_with_player("p1");
  examples::ReportExposedStudDowncard cmd;
  *cmd.mutable_exposed_card() = make_card(examples::CLUBS, 5);
  EXPECT_THROW(hand::handlers::handle_report_exposed_stud_downcard(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReportExposedStudDowncard, RejectsUnknownPlayer) {
  auto state = make_hand_state_with_player("p1");
  examples::ReportExposedStudDowncard cmd;
  cmd.set_player_root("stranger");
  *cmd.mutable_exposed_card() = make_card(examples::CLUBS, 5);
  EXPECT_THROW(hand::handlers::handle_report_exposed_stud_downcard(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReportExposedStudDowncard, RejectsInvalidCard) {
  auto state = make_hand_state_with_player("p1");
  examples::ReportExposedStudDowncard cmd;
  cmd.set_player_root("p1");
  EXPECT_THROW(hand::handlers::handle_report_exposed_stud_downcard(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReportExposedStudDowncard, EmitsExposedCard) {
  auto state = make_hand_state_with_player("p1");
  examples::ReportExposedStudDowncard cmd;
  cmd.set_player_root("p1");
  *cmd.mutable_exposed_card() = make_card(examples::CLUBS, 5);
  auto event = hand::handlers::handle_report_exposed_stud_downcard(cmd, state);
  EXPECT_EQ(event.player_root(), "p1");
  EXPECT_EQ(event.exposed_card().rank(), 5);
  EXPECT_TRUE(event.has_converted_at());
}

// --- ReplaceSeventhStreetCard ---------------------------------------------

TEST(ReplaceSeventhStreetCard, RejectsEmptyPlayerRoot) {
  auto state = make_hand_state_with_player("p1");
  examples::ReplaceSeventhStreetCard cmd;
  *cmd.mutable_original_card() = make_card(examples::CLUBS, 3);
  *cmd.mutable_replacement_card() = make_card(examples::HEARTS, 4);
  EXPECT_THROW(hand::handlers::handle_replace_seventh_street_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReplaceSeventhStreetCard, RejectsFoldedPlayer) {
  auto state = make_hand_state_with_player("p1");
  state.players[0].has_folded = true;
  examples::ReplaceSeventhStreetCard cmd;
  cmd.set_player_root("p1");
  *cmd.mutable_original_card() = make_card(examples::CLUBS, 3);
  *cmd.mutable_replacement_card() = make_card(examples::HEARTS, 4);
  EXPECT_THROW(hand::handlers::handle_replace_seventh_street_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReplaceSeventhStreetCard, RejectsIdenticalCards) {
  auto state = make_hand_state_with_player("p1");
  examples::ReplaceSeventhStreetCard cmd;
  cmd.set_player_root("p1");
  *cmd.mutable_original_card() = make_card(examples::CLUBS, 3);
  *cmd.mutable_replacement_card() = make_card(examples::CLUBS, 3);
  EXPECT_THROW(hand::handlers::handle_replace_seventh_street_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReplaceSeventhStreetCard, RejectsAtShowdown) {
  auto state = make_hand_state_with_player("p1");
  state.status = "showdown";
  examples::ReplaceSeventhStreetCard cmd;
  cmd.set_player_root("p1");
  *cmd.mutable_original_card() = make_card(examples::CLUBS, 3);
  *cmd.mutable_replacement_card() = make_card(examples::HEARTS, 4);
  EXPECT_THROW(hand::handlers::handle_replace_seventh_street_card(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ReplaceSeventhStreetCard, EmitsCards) {
  auto state = make_hand_state_with_player("p1");
  examples::ReplaceSeventhStreetCard cmd;
  cmd.set_player_root("p1");
  *cmd.mutable_original_card() = make_card(examples::CLUBS, 3);
  *cmd.mutable_replacement_card() = make_card(examples::HEARTS, 4);
  auto event = hand::handlers::handle_replace_seventh_street_card(cmd, state);
  EXPECT_EQ(event.player_root(), "p1");
  EXPECT_EQ(event.original_card().suit(), examples::CLUBS);
  EXPECT_EQ(event.replacement_card().suit(), examples::HEARTS);
  EXPECT_TRUE(event.has_replaced_at());
}

// --- CorrectBringIn -------------------------------------------------------

TEST(CorrectBringIn, RejectsEmptyRoots) {
  auto state = make_hand_state_with_player("p1");
  examples::CorrectBringIn cmd;
  EXPECT_THROW(hand::handlers::handle_correct_bring_in(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CorrectBringIn, RejectsIdenticalRoots) {
  auto state = make_hand_state_with_player("p1");
  examples::CorrectBringIn cmd;
  cmd.set_incorrect_root("p1");
  cmd.set_correct_root("p1");
  EXPECT_THROW(hand::handlers::handle_correct_bring_in(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CorrectBringIn, RejectsNegativeReturnedAmount) {
  auto state = make_hand_state_with_player("p1");
  hand::PlayerHandInfo p2;
  p2.player_root = "p2";
  p2.position = 1;
  state.players[1] = p2;
  examples::CorrectBringIn cmd;
  cmd.set_incorrect_root("p1");
  cmd.set_correct_root("p2");
  cmd.set_returned_amount(-1);
  EXPECT_THROW(hand::handlers::handle_correct_bring_in(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CorrectBringIn, RejectsUnknownPlayer) {
  auto state = make_hand_state_with_player("p1");
  examples::CorrectBringIn cmd;
  cmd.set_incorrect_root("p1");
  cmd.set_correct_root("stranger");
  EXPECT_THROW(hand::handlers::handle_correct_bring_in(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CorrectBringIn, RejectsAfterNextPlayerActed) {
  auto state = make_hand_state_with_player("p1");
  hand::PlayerHandInfo p2;
  p2.player_root = "p2";
  p2.position = 1;
  p2.has_acted = true;
  state.players[1] = p2;
  examples::CorrectBringIn cmd;
  cmd.set_incorrect_root("p1");
  cmd.set_correct_root("p2");
  EXPECT_THROW(hand::handlers::handle_correct_bring_in(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(CorrectBringIn, EmitsFields) {
  auto state = make_hand_state_with_player("p1");
  hand::PlayerHandInfo p2;
  p2.player_root = "p2";
  p2.position = 1;
  state.players[1] = p2;
  examples::CorrectBringIn cmd;
  cmd.set_incorrect_root("p1");
  cmd.set_correct_root("p2");
  cmd.set_returned_amount(15);
  auto event = hand::handlers::handle_correct_bring_in(cmd, state);
  EXPECT_EQ(event.incorrect_root(), "p1");
  EXPECT_EQ(event.correct_root(), "p2");
  EXPECT_EQ(event.returned_amount(), 15);
  EXPECT_TRUE(event.has_corrected_at());
}

// --- ChangeSeats / move_skips_a_blind -------------------------------------

TEST(MoveSkipsBlind, ReturnsFalseWhenMaxPlayersZero) {
  table::TableState s;
  EXPECT_FALSE(table::handlers::move_skips_a_blind(s, 0, 1));
}

TEST(MoveSkipsBlind, FalseForNegativeSeats) {
  auto s = make_table_with_seat(0, "p1");
  EXPECT_FALSE(table::handlers::move_skips_a_blind(s, -1, 1));
  EXPECT_FALSE(table::handlers::move_skips_a_blind(s, 0, -1));
}

TEST(MoveSkipsBlind, TrueWhenSkippingSmallBlind) {
  // dealer=0, sb=1, bb=2. Moving from 0 to 3 walks past sb (pos 1) AND
  // bb (pos 2) — 2 blinds crossed.
  auto s = make_table_with_seat(0, "p1");
  EXPECT_TRUE(table::handlers::move_skips_a_blind(s, 0, 3));
}

TEST(MoveSkipsBlind, FalseForAdjacentMove) {
  // Moving from 3 to 4 doesn't cross dealer/SB/BB at all (dealer=0).
  auto s = make_table_with_seat(0, "p1");
  EXPECT_FALSE(table::handlers::move_skips_a_blind(s, 3, 4));
}

TEST(ChangeSeats, RejectsMissingTable) {
  table::TableState empty;
  examples::ChangeSeats cmd;
  cmd.set_player_root("p1");
  cmd.set_current_seat(0);
  cmd.set_requested_seat(1);
  EXPECT_THROW(table::handlers::handle_change_seats(cmd, empty),
               angzarr::CommandRejectedError);
}

TEST(ChangeSeats, RejectsEmptyPlayerRoot) {
  auto state = make_table_with_seat(0, "p1");
  examples::ChangeSeats cmd;
  cmd.set_current_seat(0);
  cmd.set_requested_seat(1);
  EXPECT_THROW(table::handlers::handle_change_seats(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ChangeSeats, RejectsNegativeCurrentSeat) {
  auto state = make_table_with_seat(0, "p1");
  examples::ChangeSeats cmd;
  cmd.set_player_root("p1");
  cmd.set_current_seat(-1);
  cmd.set_requested_seat(1);
  EXPECT_THROW(table::handlers::handle_change_seats(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ChangeSeats, RejectsNegativeRequestedSeat) {
  auto state = make_table_with_seat(0, "p1");
  examples::ChangeSeats cmd;
  cmd.set_player_root("p1");
  cmd.set_current_seat(0);
  cmd.set_requested_seat(-1);
  EXPECT_THROW(table::handlers::handle_change_seats(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ChangeSeats, RejectsIdenticalSeatsWithSeatsIdenticalCode) {
  auto state = make_table_with_seat(0, "p1");
  examples::ChangeSeats cmd;
  cmd.set_player_root("p1");
  cmd.set_current_seat(0);
  cmd.set_requested_seat(0);
  try {
    table::handlers::handle_change_seats(cmd, state);
    FAIL() << "Expected SEATS_IDENTICAL rejection";
  } catch (const angzarr::CommandRejectedError& e) {
    EXPECT_EQ(e.code(), std::string("SEATS_IDENTICAL"));
  }
}

TEST(ChangeSeats, RejectsPlayerNotAtCurrentSeat) {
  auto state = make_table_with_seat(0, "p1");
  examples::ChangeSeats cmd;
  cmd.set_player_root("p2");
  cmd.set_current_seat(0);
  cmd.set_requested_seat(1);
  EXPECT_THROW(table::handlers::handle_change_seats(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ChangeSeats, RejectsOccupiedRequestedSeat) {
  auto state = make_table_with_seat(0, "p1");
  table::SeatState s2;
  s2.position = 1;
  s2.player_root = "other";
  s2.stack = 100;
  state.seats[1] = s2;
  examples::ChangeSeats cmd;
  cmd.set_player_root("p1");
  cmd.set_current_seat(0);
  cmd.set_requested_seat(1);
  EXPECT_THROW(table::handlers::handle_change_seats(cmd, state),
               angzarr::CommandRejectedError);
}

TEST(ChangeSeats, EmitsZeroForfeitWhenNoBlindCrossed) {
  // dealer=5, so sb=6, bb=7 with max_players=9. Move from seat 0 -> seat 1
  // crosses no blind.
  auto state = make_table_with_seat(0, "p1", 9, 5, 5, 10);
  examples::ChangeSeats cmd;
  cmd.set_player_root("p1");
  cmd.set_current_seat(0);
  cmd.set_requested_seat(1);
  auto event = table::handlers::handle_change_seats(cmd, state);
  EXPECT_EQ(event.player_root(), "p1");
  EXPECT_EQ(event.chips_forfeited(), 0);
  EXPECT_EQ(event.missed_round_count(), 0);
  EXPECT_TRUE(event.has_assessed_at());
}

TEST(ChangeSeats, EmitsForfeitWhenBlindSkipped) {
  // dealer=0, sb=1, bb=2 with max_players=9. Moving 0 -> 3 crosses sb+bb.
  auto state = make_table_with_seat(0, "p1", 9, 0, 5, 10);
  examples::ChangeSeats cmd;
  cmd.set_player_root("p1");
  cmd.set_current_seat(0);
  cmd.set_requested_seat(3);
  auto event = table::handlers::handle_change_seats(cmd, state);
  EXPECT_EQ(event.chips_forfeited(), 15);  // sb + bb
  EXPECT_EQ(event.missed_round_count(), 2);
}
