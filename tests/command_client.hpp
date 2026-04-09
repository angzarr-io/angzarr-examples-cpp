#pragma once

/// CommandClient abstraction for dispatching commands to aggregates.
///
/// Two implementations:
/// - InProcessClient: wraps direct handler calls (for unit tests)
/// - GrpcClient: connects to coordinator via gRPC (for acceptance tests)
///
/// The runner checks PLAYER_URL env var:
///   set   -> GrpcClient (connects to deployed coordinator)
///   unset -> InProcessClient (calls handlers directly)

#include <google/protobuf/any.pb.h>
#include <grpcpp/grpcpp.h>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace tests {

/// Result of a command execution.
struct CommandResult {
    /// The resulting event (if command succeeded).
    std::optional<google::protobuf::Any> event;

    /// Error message (if command was rejected).
    std::optional<std::string> error_message;

    /// gRPC status code for the error (if command was rejected).
    std::optional<grpc::StatusCode> error_code;

    bool succeeded() const { return event.has_value(); }
    bool failed() const { return error_message.has_value(); }
};

/// Abstract interface for dispatching commands to aggregates.
///
/// Commands are domain-specific protobuf messages. The client is responsible
/// for routing them to the correct handler (either in-process or via gRPC).
class CommandClient {
   public:
    virtual ~CommandClient() = default;

    // -----------------------------------------------------------------------
    // Player domain commands
    // -----------------------------------------------------------------------

    /// Register a new player.
    virtual CommandResult register_player(const std::string& name, const std::string& email,
                                          const std::string& player_type = "HUMAN",
                                          const std::string& ai_model_id = "") = 0;

    /// Deposit funds into a player's bankroll.
    virtual CommandResult deposit_funds(int64_t amount,
                                        const std::string& currency = "CHIPS") = 0;

    /// Withdraw funds from a player's bankroll.
    virtual CommandResult withdraw_funds(int64_t amount,
                                         const std::string& currency = "CHIPS") = 0;

    /// Reserve funds for a table.
    virtual CommandResult reserve_funds(int64_t amount, const std::string& table_root,
                                        const std::string& currency = "CHIPS") = 0;

    /// Release reserved funds from a table.
    virtual CommandResult release_funds(const std::string& table_root) = 0;

    // -----------------------------------------------------------------------
    // Table domain commands
    // -----------------------------------------------------------------------

    /// Create a new table.
    virtual CommandResult create_table(const std::string& table_name,
                                       const std::string& game_variant, int64_t small_blind,
                                       int64_t big_blind, int64_t min_buy_in, int64_t max_buy_in,
                                       int max_players) = 0;

    /// Join a table.
    virtual CommandResult join_table(const std::string& player_root, int seat,
                                     int64_t buy_in) = 0;

    /// Leave a table.
    virtual CommandResult leave_table(const std::string& player_root) = 0;

    /// Start a new hand at the table.
    virtual CommandResult start_hand() = 0;

    /// End a hand with results.
    virtual CommandResult end_hand(
        const std::string& hand_root,
        const std::vector<std::tuple<std::string, int64_t, std::string>>& results) = 0;

    // -----------------------------------------------------------------------
    // Hand domain commands
    // -----------------------------------------------------------------------

    /// Deal cards for a hand.
    virtual CommandResult deal_cards(const std::string& table_root, int64_t hand_number,
                                     const std::string& game_variant, int dealer_position,
                                     const std::vector<std::tuple<std::string, int, int64_t>>&
                                         players) = 0;

    /// Post a blind.
    virtual CommandResult post_blind(const std::string& player_root,
                                     const std::string& blind_type, int64_t amount) = 0;

    /// Submit a player action (fold, check, call, bet, raise, all-in).
    virtual CommandResult player_action(const std::string& player_root,
                                        const std::string& action, int64_t amount = 0) = 0;

    /// Deal community cards.
    virtual CommandResult deal_community_cards(int count) = 0;

    /// Award the pot.
    virtual CommandResult award_pot(
        const std::vector<std::tuple<std::string, int64_t, std::string>>& awards) = 0;
};

/// Factory: create the appropriate CommandClient based on environment.
/// If PLAYER_URL is set, returns GrpcClient; otherwise InProcessClient.
std::unique_ptr<CommandClient> create_command_client();

/// Global command client instance (set up before scenarios run).
extern std::unique_ptr<CommandClient> g_command_client;

}  // namespace tests
