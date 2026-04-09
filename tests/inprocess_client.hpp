#pragma once

/// InProcessClient: dispatches commands directly to handler functions.
///
/// Used for unit tests (no PLAYER_URL set). This wraps the existing
/// handler dispatch pattern from the step definitions, maintaining
/// the same state reconstruction approach via event pages.

#include "angzarr/errors.hpp"
#include "command_client.hpp"
#include "hand_state.hpp"
#include "player_state.hpp"
#include "table_state.hpp"
#include "test_context.hpp"
#include "test_utils.hpp"

// Handler includes - player
#include "deposit_handler.hpp"
#include "register_handler.hpp"
#include "release_handler.hpp"
#include "reserve_handler.hpp"
#include "withdraw_handler.hpp"

// Handler includes - table
#include "create_handler.hpp"
#include "end_hand_handler.hpp"
#include "join_handler.hpp"
#include "leave_handler.hpp"
#include "start_hand_handler.hpp"

// Handler includes - hand
#include "action_handler.hpp"
#include "award_pot_handler.hpp"
#include "deal_community_handler.hpp"
#include "deal_handler.hpp"
#include "post_blind_handler.hpp"

// Proto includes
#include "examples/hand.pb.h"
#include "examples/player.pb.h"
#include "examples/poker_types.pb.h"
#include "examples/table.pb.h"

namespace tests {

class InProcessClient : public CommandClient {
   public:
    // -----------------------------------------------------------------------
    // Player domain
    // -----------------------------------------------------------------------

    CommandResult register_player(const std::string& name, const std::string& email,
                                  const std::string& player_type,
                                  const std::string& ai_model_id) override {
        examples::RegisterPlayer cmd;
        cmd.set_display_name(name);
        cmd.set_email(email);
        if (player_type == "AI") {
            cmd.set_player_type(examples::PlayerType::AI);
            cmd.set_ai_model_id(ai_model_id);
        } else {
            cmd.set_player_type(examples::PlayerType::HUMAN);
        }

        return execute_handler([&]() {
            return player::handlers::handle_register(cmd, g_context.player_state);
        });
    }

    CommandResult deposit_funds(int64_t amount, const std::string& currency) override {
        examples::DepositFunds cmd;
        cmd.mutable_amount()->set_amount(amount);
        cmd.mutable_amount()->set_currency_code(currency);

        return execute_handler(
            [&]() { return player::handlers::handle_deposit(cmd, g_context.player_state); });
    }

    CommandResult withdraw_funds(int64_t amount, const std::string& currency) override {
        examples::WithdrawFunds cmd;
        cmd.mutable_amount()->set_amount(amount);
        cmd.mutable_amount()->set_currency_code(currency);

        return execute_handler(
            [&]() { return player::handlers::handle_withdraw(cmd, g_context.player_state); });
    }

    CommandResult reserve_funds(int64_t amount, const std::string& table_root,
                                const std::string& currency) override {
        examples::ReserveFunds cmd;
        cmd.set_table_root(table_root);
        cmd.mutable_amount()->set_amount(amount);
        cmd.mutable_amount()->set_currency_code(currency);

        return execute_handler(
            [&]() { return player::handlers::handle_reserve(cmd, g_context.player_state); });
    }

    CommandResult release_funds(const std::string& table_root) override {
        examples::ReleaseFunds cmd;
        cmd.set_table_root(table_root);

        return execute_handler(
            [&]() { return player::handlers::handle_release(cmd, g_context.player_state); });
    }

    // -----------------------------------------------------------------------
    // Table domain
    // -----------------------------------------------------------------------

    CommandResult create_table(const std::string& table_name, const std::string& game_variant,
                               int64_t small_blind, int64_t big_blind, int64_t min_buy_in,
                               int64_t max_buy_in, int max_players) override {
        examples::CreateTable cmd;
        cmd.set_table_name(table_name);
        cmd.set_game_variant(parse_game_variant(game_variant));
        cmd.set_small_blind(small_blind);
        cmd.set_big_blind(big_blind);
        cmd.set_min_buy_in(min_buy_in);
        cmd.set_max_buy_in(max_buy_in);
        cmd.set_max_players(max_players);
        cmd.set_action_timeout_seconds(30);

        return execute_handler(
            [&]() { return table::handlers::handle_create(cmd, g_context.table_state); });
    }

    CommandResult join_table(const std::string& player_root, int seat,
                             int64_t buy_in) override {
        examples::JoinTable cmd;
        cmd.set_player_root(player_root);
        cmd.set_preferred_seat(seat);
        cmd.set_buy_in_amount(buy_in);

        return execute_handler(
            [&]() { return table::handlers::handle_join(cmd, g_context.table_state); });
    }

    CommandResult leave_table(const std::string& player_root) override {
        examples::LeaveTable cmd;
        cmd.set_player_root(player_root);

        return execute_handler(
            [&]() { return table::handlers::handle_leave(cmd, g_context.table_state); });
    }

    CommandResult start_hand() override {
        examples::StartHand cmd;

        return execute_handler(
            [&]() { return table::handlers::handle_start_hand(cmd, g_context.table_state); });
    }

    CommandResult end_hand(
        const std::string& hand_root,
        const std::vector<std::tuple<std::string, int64_t, std::string>>& results) override {
        examples::EndHand cmd;
        cmd.set_hand_root(hand_root);

        for (const auto& [winner_root, amount, pot_type] : results) {
            auto* result = cmd.add_results();
            result->set_winner_root(winner_root);
            result->set_amount(amount);
            result->set_pot_type(pot_type);
        }

        return execute_handler(
            [&]() { return table::handlers::handle_end_hand(cmd, g_context.table_state); });
    }

    // -----------------------------------------------------------------------
    // Hand domain
    // -----------------------------------------------------------------------

    CommandResult deal_cards(const std::string& table_root, int64_t hand_number,
                             const std::string& game_variant, int dealer_position,
                             const std::vector<std::tuple<std::string, int, int64_t>>& players)
        override {
        examples::DealCards cmd;
        cmd.set_table_root(table_root);
        cmd.set_hand_number(hand_number);
        cmd.set_game_variant(parse_game_variant(game_variant));
        cmd.set_dealer_position(dealer_position);

        for (const auto& [player_root, position, stack] : players) {
            auto* p = cmd.add_players();
            p->set_player_root(player_root);
            p->set_position(position);
            p->set_stack(stack);
        }

        return execute_handler(
            [&]() { return hand::handlers::handle_deal(cmd, g_context.hand_state); });
    }

    CommandResult post_blind(const std::string& player_root, const std::string& blind_type,
                             int64_t amount) override {
        examples::PostBlind cmd;
        cmd.set_player_root(player_root);
        cmd.set_blind_type(blind_type);
        cmd.set_amount(amount);

        return execute_handler(
            [&]() { return hand::handlers::handle_post_blind(cmd, g_context.hand_state); });
    }

    CommandResult player_action(const std::string& player_root, const std::string& action,
                                int64_t amount) override {
        examples::PlayerAction cmd;
        cmd.set_player_root(player_root);
        cmd.set_action(parse_action_type(action));
        cmd.set_amount(amount);

        return execute_handler(
            [&]() { return hand::handlers::handle_action(cmd, g_context.hand_state); });
    }

    CommandResult deal_community_cards(int count) override {
        examples::DealCommunityCards cmd;
        cmd.set_count(count);

        return execute_handler([&]() {
            return hand::handlers::handle_deal_community(cmd, g_context.hand_state);
        });
    }

    CommandResult award_pot(
        const std::vector<std::tuple<std::string, int64_t, std::string>>& awards) override {
        examples::AwardPot cmd;

        for (const auto& [player_root, amount, pot_type] : awards) {
            auto* award = cmd.add_awards();
            award->set_player_root(player_root);
            award->set_amount(amount);
            award->set_pot_type(pot_type);
        }

        return execute_handler(
            [&]() { return hand::handlers::handle_award_pot(cmd, g_context.hand_state); });
    }

   private:
    /// Execute a handler function and translate the result/exception to CommandResult.
    template <typename HandlerFn>
    CommandResult execute_handler(HandlerFn&& fn) {
        CommandResult result;
        try {
            auto event = fn();
            result.event.emplace();
            result.event->PackFrom(event);
        } catch (const angzarr::CommandRejectedError& e) {
            result.error_message = e.what();
            result.error_code = e.status_code;
        }
        return result;
    }
};

}  // namespace tests
