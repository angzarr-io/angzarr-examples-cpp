#pragma once

/// GrpcClient: dispatches commands to coordinator via gRPC.
///
/// Used for acceptance tests (PLAYER_URL is set). Sends commands to
/// the CommandHandlerCoordinatorService and translates responses back
/// to CommandResult.
///
/// Each domain may have its own coordinator endpoint. For now we support:
///   PLAYER_URL - player aggregate coordinator
///   TABLE_URL  - table aggregate coordinator
///   HAND_URL   - hand aggregate coordinator

#include <grpcpp/grpcpp.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "angzarr/command_handler.grpc.pb.h"
#include "angzarr/command_handler.pb.h"
#include "angzarr/types.pb.h"
#include "command_client.hpp"
#include "examples/hand.pb.h"
#include "examples/player.pb.h"
#include "examples/poker_types.pb.h"
#include "examples/table.pb.h"
#include "test_utils.hpp"

namespace tests {

class GrpcClient : public CommandClient {
   public:
    /// Construct with coordinator endpoints per domain.
    GrpcClient(const std::string& player_url, const std::string& table_url,
               const std::string& hand_url)
        : player_url_(player_url), table_url_(table_url), hand_url_(hand_url) {
        // Create channels and stubs
        auto player_channel =
            grpc::CreateChannel(player_url, grpc::InsecureChannelCredentials());
        player_stub_ = angzarr::CommandHandlerCoordinatorService::NewStub(player_channel);

        auto table_channel =
            grpc::CreateChannel(table_url, grpc::InsecureChannelCredentials());
        table_stub_ = angzarr::CommandHandlerCoordinatorService::NewStub(table_channel);

        auto hand_channel =
            grpc::CreateChannel(hand_url, grpc::InsecureChannelCredentials());
        hand_stub_ = angzarr::CommandHandlerCoordinatorService::NewStub(hand_channel);
    }

    /// Construct from environment variables with defaults.
    static std::unique_ptr<GrpcClient> from_env() {
        auto get_env = [](const char* var, const char* fallback) -> std::string {
            const char* val = std::getenv(var);
            return val ? val : fallback;
        };

        return std::make_unique<GrpcClient>(get_env("PLAYER_URL", "localhost:1310"),
                                             get_env("TABLE_URL", "localhost:1310"),
                                             get_env("HAND_URL", "localhost:1310"));
    }

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
        return send_command("player", cmd, *player_stub_);
    }

    CommandResult deposit_funds(int64_t amount, const std::string& currency) override {
        examples::DepositFunds cmd;
        cmd.mutable_amount()->set_amount(amount);
        cmd.mutable_amount()->set_currency_code(currency);
        return send_command("player", cmd, *player_stub_);
    }

    CommandResult withdraw_funds(int64_t amount, const std::string& currency) override {
        examples::WithdrawFunds cmd;
        cmd.mutable_amount()->set_amount(amount);
        cmd.mutable_amount()->set_currency_code(currency);
        return send_command("player", cmd, *player_stub_);
    }

    CommandResult reserve_funds(int64_t amount, const std::string& table_root,
                                const std::string& currency) override {
        examples::ReserveFunds cmd;
        cmd.set_table_root(table_root);
        cmd.mutable_amount()->set_amount(amount);
        cmd.mutable_amount()->set_currency_code(currency);
        return send_command("player", cmd, *player_stub_);
    }

    CommandResult release_funds(const std::string& table_root) override {
        examples::ReleaseFunds cmd;
        cmd.set_table_root(table_root);
        return send_command("player", cmd, *player_stub_);
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
        return send_command("table", cmd, *table_stub_);
    }

    CommandResult join_table(const std::string& player_root, int seat,
                             int64_t buy_in) override {
        examples::JoinTable cmd;
        cmd.set_player_root(player_root);
        cmd.set_preferred_seat(seat);
        cmd.set_buy_in_amount(buy_in);
        return send_command("table", cmd, *table_stub_);
    }

    CommandResult leave_table(const std::string& player_root) override {
        examples::LeaveTable cmd;
        cmd.set_player_root(player_root);
        return send_command("table", cmd, *table_stub_);
    }

    CommandResult start_hand() override {
        examples::StartHand cmd;
        return send_command("table", cmd, *table_stub_);
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
        return send_command("table", cmd, *table_stub_);
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
        return send_command("hand", cmd, *hand_stub_);
    }

    CommandResult post_blind(const std::string& player_root, const std::string& blind_type,
                             int64_t amount) override {
        examples::PostBlind cmd;
        cmd.set_player_root(player_root);
        cmd.set_blind_type(blind_type);
        cmd.set_amount(amount);
        return send_command("hand", cmd, *hand_stub_);
    }

    CommandResult player_action(const std::string& player_root, const std::string& action,
                                int64_t amount) override {
        examples::PlayerAction cmd;
        cmd.set_player_root(player_root);
        cmd.set_action(parse_action_type(action));
        cmd.set_amount(amount);
        return send_command("hand", cmd, *hand_stub_);
    }

    CommandResult deal_community_cards(int count) override {
        examples::DealCommunityCards cmd;
        cmd.set_count(count);
        return send_command("hand", cmd, *hand_stub_);
    }

    CommandResult request_draw(const std::string& player_root,
                               const std::vector<int32_t>& card_indices) override {
        examples::RequestDraw cmd;
        cmd.set_player_root(player_root);
        for (int32_t idx : card_indices) {
            cmd.add_card_indices(idx);
        }
        return send_command("hand", cmd, *hand_stub_);
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
        return send_command("hand", cmd, *hand_stub_);
    }

    CommandResult add_chips(const std::string& player_root, int64_t amount) override {
        examples::AddChips cmd;
        cmd.set_player_root(player_root);
        cmd.set_amount(amount);
        return send_command("table", cmd, *table_stub_);
    }

   private:
    std::string player_url_;
    std::string table_url_;
    std::string hand_url_;

    std::unique_ptr<angzarr::CommandHandlerCoordinatorService::Stub> player_stub_;
    std::unique_ptr<angzarr::CommandHandlerCoordinatorService::Stub> table_stub_;
    std::unique_ptr<angzarr::CommandHandlerCoordinatorService::Stub> hand_stub_;

    // Sequence tracking per aggregate root (for multi-command scenarios).
    int64_t next_sequence_ = 0;

    /// Build a CommandRequest and send it via the coordinator stub.
    template <typename CmdT>
    CommandResult send_command(const std::string& domain, const CmdT& cmd,
                               angzarr::CommandHandlerCoordinatorService::Stub& stub) {
        // Build CommandBook
        angzarr::CommandBook command_book;
        auto* cover = command_book.mutable_cover();
        cover->set_domain(domain);
        // Root is managed by the coordinator for new aggregates

        auto* page = command_book.add_pages();
        page->mutable_header()->set_sequence(next_sequence_++);
        page->mutable_command()->PackFrom(cmd);

        // Build CommandRequest with SYNC_MODE_SIMPLE for acceptance tests
        angzarr::CommandRequest request;
        *request.mutable_command() = command_book;
        request.set_sync_mode(angzarr::SYNC_MODE_SIMPLE);

        // Execute
        angzarr::CommandResponse response;
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));

        auto status = stub.HandleCommand(&context, request, &response);

        CommandResult result;
        if (status.ok()) {
            // Extract first event from response
            if (response.events().pages_size() > 0) {
                result.event = response.events().pages(0).event();
            }
            result.projection_count = response.projections_size();
            result.cascade_error_count = response.cascade_errors_size();
        } else {
            result.error_message = status.error_message();
            result.error_code = status.error_code();
        }
        return result;
    }
};

}  // namespace tests
