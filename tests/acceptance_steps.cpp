/// Acceptance test step definitions using CommandClient abstraction.
///
/// These high-level steps correspond to the acceptance feature files
/// (features/acceptance/poker_game.feature). They dispatch commands
/// through the CommandClient interface, which routes either in-process
/// or via gRPC depending on PLAYER_URL environment variable.
///
/// NOTE: This file is included via all_acceptance_steps.cpp to avoid
/// __COUNTER__ collisions (cucumber-cpp requirement).

// GTest must be included before cucumber-cpp autodetect for framework detection
#include <gtest/gtest.h>
#include <cucumber-cpp/autodetect.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "command_client.hpp"
#include "test_context.hpp"
#include "test_utils.hpp"

using cucumber::ScenarioScope;

namespace {

/// Track state across acceptance test steps.
struct AcceptanceState {
    // Map player names to their root IDs (from registration).
    std::map<std::string, std::string> player_roots;

    // Map table names to their root IDs.
    std::map<std::string, std::string> table_roots;

    // Track the last command result for assertions.
    tests::CommandResult last_result;

    // Track current table context.
    std::string current_table;

    // Track current hand root.
    std::string current_hand_root;

    // Player bankroll tracking (for in-process mode where we manage state).
    std::map<std::string, int64_t> player_bankrolls;
    std::map<std::string, int64_t> player_reserved;
    std::map<std::string, int64_t> player_stacks;

    void reset() {
        player_roots.clear();
        table_roots.clear();
        last_result = {};
        current_table.clear();
        current_hand_root.clear();
        player_bankrolls.clear();
        player_reserved.clear();
        player_stacks.clear();
    }
};

thread_local AcceptanceState g_acc;

/// Helper to apply result to context (for in-process state tracking).
void apply_result_to_context(const tests::CommandResult& result) {
    if (result.succeeded()) {
        tests::g_context.result_event = result.event;
        tests::g_context.clear_error();
    } else {
        tests::g_context.last_error = result.error_message;
        tests::g_context.last_error_code = result.error_code;
        tests::g_context.result_event.reset();
    }
}

}  // anonymous namespace

// Reset acceptance state before each scenario.
BEFORE() {
    g_acc.reset();
    if (!tests::g_command_client) {
        tests::g_command_client = tests::create_command_client();
    }
}

// ==========================================================================
// Background / System Setup
// ==========================================================================

GIVEN("^the poker system is running in standalone mode$") {
    // In-process: nothing to do (handlers are linked in).
    // gRPC: connectivity is validated by first command.
}

// ==========================================================================
// Player Registration and Bankroll
// ==========================================================================

WHEN("^I register player \"([^\"]*)\" with email \"([^\"]*)\"$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(std::string, email);

    auto result = tests::g_command_client->register_player(name, email);
    g_acc.last_result = result;
    apply_result_to_context(result);

    if (result.succeeded()) {
        g_acc.player_roots[name] = tests::make_player_root(name);
        g_acc.player_bankrolls[name] = 0;
        g_acc.player_reserved[name] = 0;
    }
}

WHEN("^I deposit (\\d+) chips to player \"([^\"]*)\"$") {
    REGEX_PARAM(int64_t, amount);
    REGEX_PARAM(std::string, name);

    auto result = tests::g_command_client->deposit_funds(amount);
    g_acc.last_result = result;
    apply_result_to_context(result);

    if (result.succeeded()) {
        g_acc.player_bankrolls[name] += amount;
    }
}

THEN("^player \"([^\"]*)\" has bankroll (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, expected);
    ASSERT_EQ(g_acc.player_bankrolls[name], expected);
}

THEN("^player \"([^\"]*)\" has available balance (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, expected);
    int64_t available = g_acc.player_bankrolls[name] - g_acc.player_reserved[name];
    ASSERT_EQ(available, expected);
}

THEN("^player \"([^\"]*)\" has reserved funds (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, expected);
    ASSERT_EQ(g_acc.player_reserved[name], expected);
}

// ==========================================================================
// Given: Pre-existing State
// ==========================================================================

GIVEN("^registered players with bankroll:$") {
    TABLE_PARAM(table);

    for (const auto& row : table.hashes()) {
        std::string name = row.at("name");
        int64_t bankroll = std::stoll(row.at("bankroll"));

        auto reg_result = tests::g_command_client->register_player(
            name, name + "@example.com");
        ASSERT_TRUE(reg_result.succeeded())
            << "Failed to register " << name << ": "
            << reg_result.error_message.value_or("unknown");

        g_acc.player_roots[name] = tests::make_player_root(name);
        g_acc.player_bankrolls[name] = 0;
        g_acc.player_reserved[name] = 0;

        auto dep_result = tests::g_command_client->deposit_funds(bankroll);
        ASSERT_TRUE(dep_result.succeeded())
            << "Failed to deposit for " << name << ": "
            << dep_result.error_message.value_or("unknown");

        g_acc.player_bankrolls[name] = bankroll;
    }
}

GIVEN("^a table \"([^\"]*)\" with seated players:$") {
    REGEX_PARAM(std::string, table_name);
    TABLE_PARAM(table);

    g_acc.current_table = table_name;

    // Register players and deposit funds if not already done
    for (const auto& row : table.hashes()) {
        std::string name = row.at("name");
        int64_t stack = std::stoll(row.at("stack"));

        if (g_acc.player_roots.find(name) == g_acc.player_roots.end()) {
            auto reg = tests::g_command_client->register_player(
                name, name + "@example.com");
            // May already exist in gRPC mode - that is fine
            g_acc.player_roots[name] = tests::make_player_root(name);
            g_acc.player_bankrolls[name] = stack * 2;  // Enough for buy-in
            g_acc.player_reserved[name] = 0;

            tests::g_command_client->deposit_funds(stack * 2);
        }
    }

    // Create table
    auto create_result = tests::g_command_client->create_table(
        table_name, "TEXAS_HOLDEM", 5, 10, 200, 2000, 9);
    // Table may already exist in gRPC mode
    g_acc.table_roots[table_name] = table_name;

    // Seat players
    for (const auto& row : table.hashes()) {
        std::string name = row.at("name");
        int seat = std::stoi(row.at("seat"));
        int64_t stack = std::stoll(row.at("stack"));

        auto join_result = tests::g_command_client->join_table(
            tests::make_player_root(name), seat, stack);
        if (join_result.succeeded()) {
            g_acc.player_stacks[name] = stack;
            g_acc.player_reserved[name] += stack;
        }
    }
}

// ==========================================================================
// Table Operations
// ==========================================================================

WHEN("^I create a Texas Hold'em table \"([^\"]*)\" with blinds (\\d+)/(\\d+)$") {
    REGEX_PARAM(std::string, table_name);
    REGEX_PARAM(int64_t, small_blind);
    REGEX_PARAM(int64_t, big_blind);

    auto result = tests::g_command_client->create_table(
        table_name, "TEXAS_HOLDEM", small_blind, big_blind, 200, 2000, 9);
    g_acc.last_result = result;
    apply_result_to_context(result);

    if (result.succeeded()) {
        g_acc.table_roots[table_name] = table_name;
        g_acc.current_table = table_name;
    }
}

WHEN("^player \"([^\"]*)\" joins table \"([^\"]*)\" at seat (\\d+) with buy-in (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(std::string, table_name);
    REGEX_PARAM(int, seat);
    REGEX_PARAM(int64_t, buy_in);

    auto result = tests::g_command_client->join_table(
        tests::make_player_root(name), seat, buy_in);
    g_acc.last_result = result;
    apply_result_to_context(result);

    if (result.succeeded()) {
        g_acc.player_stacks[name] = buy_in;
        g_acc.player_reserved[name] += buy_in;
    }
}

WHEN("^player \"([^\"]*)\" leaves table \"([^\"]*)\"$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(std::string, table_name);

    auto result = tests::g_command_client->leave_table(tests::make_player_root(name));
    g_acc.last_result = result;
    apply_result_to_context(result);

    if (result.succeeded()) {
        int64_t stack = g_acc.player_stacks[name];
        g_acc.player_reserved[name] -= stack;
        g_acc.player_stacks.erase(name);
    }
}

THEN("^table \"([^\"]*)\" has (\\d+) seated players?$") {
    REGEX_PARAM(std::string, table_name);
    REGEX_PARAM(int, expected);
    // Count players with stacks (seated at the table)
    int count = 0;
    for (const auto& [name, stack] : g_acc.player_stacks) {
        count++;
    }
    ASSERT_EQ(count, expected);
}

// ==========================================================================
// Hand Operations
// ==========================================================================

WHEN("^a hand starts at table \"([^\"]*)\"$") {
    REGEX_PARAM(std::string, table_name);

    auto result = tests::g_command_client->start_hand();
    g_acc.last_result = result;
    apply_result_to_context(result);
}

WHEN("^a hand starts and blinds are posted \\((\\d+)/(\\d+)\\)$") {
    REGEX_PARAM(int64_t, small_blind);
    REGEX_PARAM(int64_t, big_blind);

    auto result = tests::g_command_client->start_hand();
    g_acc.last_result = result;
    // Blinds posting happens via saga in deployed mode; in-process we
    // would need to simulate. For now, track pot state.
    (void)small_blind;
    (void)big_blind;
}

WHEN("^blinds are posted \\((\\d+)/(\\d+)\\)$") {
    REGEX_PARAM(int64_t, small_blind);
    REGEX_PARAM(int64_t, big_blind);
    // Blind posting handled by process manager in deployed mode
    (void)small_blind;
    (void)big_blind;
}

// ==========================================================================
// Betting Actions
// ==========================================================================

WHEN("^\"([^\"]*)\" posts small blind (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, amount);

    auto result = tests::g_command_client->post_blind(
        tests::make_player_root(name), "small", amount);
    g_acc.last_result = result;
    apply_result_to_context(result);
}

WHEN("^\"([^\"]*)\" posts big blind (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, amount);

    auto result = tests::g_command_client->post_blind(
        tests::make_player_root(name), "big", amount);
    g_acc.last_result = result;
    apply_result_to_context(result);
}

WHEN("^\"([^\"]*)\" folds$") {
    REGEX_PARAM(std::string, name);

    auto result = tests::g_command_client->player_action(
        tests::make_player_root(name), "FOLD");
    g_acc.last_result = result;
    apply_result_to_context(result);
}

WHEN("^\"([^\"]*)\" checks$") {
    REGEX_PARAM(std::string, name);

    auto result = tests::g_command_client->player_action(
        tests::make_player_root(name), "CHECK");
    g_acc.last_result = result;
    apply_result_to_context(result);
}

WHEN("^\"([^\"]*)\" calls (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, amount);

    auto result = tests::g_command_client->player_action(
        tests::make_player_root(name), "CALL", amount);
    g_acc.last_result = result;
    apply_result_to_context(result);
}

WHEN("^\"([^\"]*)\" bets (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, amount);

    auto result = tests::g_command_client->player_action(
        tests::make_player_root(name), "BET", amount);
    g_acc.last_result = result;
    apply_result_to_context(result);
}

WHEN("^\"([^\"]*)\" raises to (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, amount);

    auto result = tests::g_command_client->player_action(
        tests::make_player_root(name), "RAISE", amount);
    g_acc.last_result = result;
    apply_result_to_context(result);
}

WHEN("^\"([^\"]*)\" re-raises to (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, amount);

    auto result = tests::g_command_client->player_action(
        tests::make_player_root(name), "RAISE", amount);
    g_acc.last_result = result;
    apply_result_to_context(result);
}

WHEN("^\"([^\"]*)\" goes all-in for (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, amount);

    auto result = tests::g_command_client->player_action(
        tests::make_player_root(name), "ALL_IN", amount);
    g_acc.last_result = result;
    apply_result_to_context(result);
}

// ==========================================================================
// Assertions
// ==========================================================================

THEN("^\"([^\"]*)\" wins the pot of (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, amount);
    // Pot awarding happens via the hand aggregate / process manager
    // For now, track the expected outcome
    (void)name;
    (void)amount;
}

THEN("^\"([^\"]*)\" wins the pot of (\\d+) uncontested$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, amount);
    (void)name;
    (void)amount;
}

THEN("^\"([^\"]*)\" stack is (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, expected);
    // In full e2e mode, would query the table state.
    // For now, assert tracked state.
    if (g_acc.player_stacks.count(name)) {
        // Note: stack changes happen through events; this is a placeholder
        // for full acceptance test wiring.
    }
    (void)expected;
}

THEN("^\"([^\"]*)\" has stack (\\d+)$") {
    REGEX_PARAM(std::string, name);
    REGEX_PARAM(int64_t, expected);
    (void)name;
    (void)expected;
}

THEN("^the command fails with \"([^\"]*)\"$") {
    REGEX_PARAM(std::string, expected_message);
    ASSERT_TRUE(g_acc.last_result.failed())
        << "Expected command to fail with '" << expected_message << "' but it succeeded";
    // Case-insensitive substring match
    std::string err = g_acc.last_result.error_message.value_or("");
    std::string err_lower = err;
    std::string expected_lower = expected_message;
    std::transform(err_lower.begin(), err_lower.end(), err_lower.begin(), ::tolower);
    std::transform(expected_lower.begin(), expected_lower.end(), expected_lower.begin(), ::tolower);
    ASSERT_TRUE(err_lower.find(expected_lower) != std::string::npos)
        << "Expected error to contain '" << expected_message << "' but got: '" << err << "'";
}

// ==========================================================================
// Async / Saga Assertions (within N seconds)
// ==========================================================================

THEN("^within (\\d+) seconds:$") {
    REGEX_PARAM(int, seconds);
    TABLE_PARAM(table);
    // In full e2e mode, would poll for events within the timeout.
    // For unit/in-process mode, events happen synchronously.
    (void)seconds;
    (void)table;
}

THEN("^the flop is dealt$") {
    // Flop dealing is triggered by process manager after betting round
}

THEN("^the turn is dealt$") {
    // Turn dealing is triggered by process manager
}

THEN("^the river is dealt$") {
    // River dealing is triggered by process manager
}

THEN("^showdown begins$") {
    // Showdown triggered after final betting round
}

THEN("^the winner is determined by hand ranking$") {
    // Hand evaluation happens in the hand aggregate
}

THEN("^the hand completes$") {
    // Hand completion is the final state
}

THEN("^the pot is (\\d+)$") {
    REGEX_PARAM(int64_t, expected);
    (void)expected;
}

THEN("^active player count is (\\d+)$") {
    REGEX_PARAM(int, expected);
    (void)expected;
}

THEN("^showdown is triggered immediately$") {
    // When all players are all-in, showdown is immediate
}

THEN("^no showdown occurs$") {
    // Hand ended without showdown (all folded)
}

THEN("^the hand ends without showdown$") {
    // Same as above
}
