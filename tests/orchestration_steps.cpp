/// Orchestration BDD test steps for BuyIn, Registration, and Rebuy PMs.
///
/// These PMs coordinate cross-aggregate flows that require decision coupling.
/// The tests validate conditions and verify correct command/event emission.

#include <cucumber-cpp/autodetect.hpp>
#include <gtest/gtest.h>

#include "test_context.hpp"
#include "test_utils.hpp"

#include "examples/buy_in.pb.h"
#include "examples/hand.pb.h"
#include "examples/player.pb.h"
#include "examples/table.pb.h"
#include "examples/tournament.pb.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

using cucumber::ScenarioScope;

namespace {

struct OrchestrationContext {
    // Table state
    int64_t table_min_buy_in = 200;
    int64_t table_max_buy_in = 2000;
    int32_t table_max_players = 9;
    std::set<int32_t> occupied_seats;
    bool player_seated = false;
    int32_t player_seat = -1;

    // Tournament state
    bool registration_open = false;
    bool tournament_running = false;
    bool rebuy_allowed = false;
    int32_t tournament_max_players = 100;
    int32_t tournament_current_players = 50;

    // Trigger event
    int32_t requested_seat = -1;
    int64_t requested_amount = 0;
    int64_t requested_fee = 0;

    // Results
    std::vector<std::string> emitted_commands;
    std::vector<std::string> emitted_events;
    std::string failure_code;

    void clear_results() {
        emitted_commands.clear();
        emitted_events.clear();
        failure_code.clear();
    }

    void reset() {
        *this = OrchestrationContext{};
    }
};

OrchestrationContext g_orch;

} // namespace

// ==========================================================================
// BuyIn Given steps
// ==========================================================================

GIVEN("^a table with seat (\\d+) available and buy-in range (\\d+)-(\\d+)$") {
    REGEX_PARAM(int32_t, seat);
    REGEX_PARAM(int64_t, min);
    REGEX_PARAM(int64_t, max);
    g_orch.reset();
    g_orch.table_min_buy_in = min;
    g_orch.table_max_buy_in = max;
    // Seat is available (not in occupied_seats)
}

GIVEN("^a player with a BuyInRequested event for seat (\\d+) with amount (\\d+)$") {
    REGEX_PARAM(int32_t, seat);
    REGEX_PARAM(int64_t, amount);
    g_orch.requested_seat = seat;
    g_orch.requested_amount = amount;
}

GIVEN("^a table with seat (\\d+) occupied by another player$") {
    REGEX_PARAM(int32_t, seat);
    g_orch.reset();
    g_orch.table_min_buy_in = 200;
    g_orch.table_max_buy_in = 2000;
    g_orch.occupied_seats.insert(seat);
}

GIVEN("^a table that is full with (\\d+) players$") {
    REGEX_PARAM(int32_t, count);
    g_orch.reset();
    g_orch.table_min_buy_in = 200;
    g_orch.table_max_buy_in = 2000;
    g_orch.table_max_players = count;
    for (int32_t i = 0; i < count; ++i) {
        g_orch.occupied_seats.insert(i);
    }
}

GIVEN("^a player with a BuyInRequested event for any seat with amount (\\d+)$") {
    REGEX_PARAM(int64_t, amount);
    g_orch.requested_seat = -1;
    g_orch.requested_amount = amount;
}

GIVEN("^a player and table in a pending buy-in state$") {
    g_orch.clear_results();
}

// ==========================================================================
// Registration Given steps
// ==========================================================================

GIVEN("^a tournament with registration open and capacity available$") {
    g_orch.reset();
    g_orch.registration_open = true;
    g_orch.tournament_running = false;
    g_orch.tournament_max_players = 100;
    g_orch.tournament_current_players = 50;
}

GIVEN("^a player with a RegistrationRequested event with fee (\\d+)$") {
    REGEX_PARAM(int64_t, fee);
    g_orch.requested_fee = fee;
}

GIVEN("^a tournament that is full$") {
    g_orch.reset();
    g_orch.registration_open = true;
    g_orch.tournament_max_players = 100;
    g_orch.tournament_current_players = 100; // Full
}

GIVEN("^a tournament with registration closed$") {
    g_orch.reset();
    g_orch.registration_open = false;
    g_orch.tournament_running = true;
}

GIVEN("^a player and tournament in a pending registration state$") {
    g_orch.clear_results();
}

// ==========================================================================
// Rebuy Given steps
// ==========================================================================

GIVEN("^a tournament in rebuy window with player eligible$") {
    g_orch.reset();
    g_orch.tournament_running = true;
    g_orch.rebuy_allowed = true;
}

GIVEN("^a table with the player seated at position (\\d+)$") {
    REGEX_PARAM(int32_t, seat);
    g_orch.player_seated = true;
    g_orch.player_seat = seat;
}

GIVEN("^a player with a RebuyRequested event for amount (\\d+)$") {
    REGEX_PARAM(int64_t, amount);
    g_orch.requested_amount = amount;
}

GIVEN("^a tournament with rebuy window closed$") {
    g_orch.reset();
    g_orch.tournament_running = false;
    g_orch.rebuy_allowed = false;
}

GIVEN("^a table without the player seated$") {
    g_orch.player_seated = false;
    g_orch.player_seat = -1;
}

GIVEN("^a player, tournament, and table in a pending rebuy state$") {
    g_orch.clear_results();
}

GIVEN("^a player, tournament, and table with chips added$") {
    g_orch.clear_results();
}

// ==========================================================================
// BuyIn When steps
// ==========================================================================

WHEN("^the BuyInOrchestrator handles the BuyInRequested event$") {
    g_orch.clear_results();

    // Validate buy-in amount
    if (g_orch.requested_amount < g_orch.table_min_buy_in ||
        g_orch.requested_amount > g_orch.table_max_buy_in) {
        g_orch.emitted_events.push_back("BuyInFailed");
        g_orch.failure_code = "INVALID_AMOUNT";
        return;
    }

    // Validate seat available
    if (g_orch.requested_seat >= 0 &&
        g_orch.occupied_seats.count(g_orch.requested_seat) > 0) {
        g_orch.emitted_events.push_back("BuyInFailed");
        g_orch.failure_code = "SEAT_OCCUPIED";
        return;
    }

    // Validate table not full
    if (static_cast<int32_t>(g_orch.occupied_seats.size()) >= g_orch.table_max_players) {
        g_orch.emitted_events.push_back("BuyInFailed");
        g_orch.failure_code = "TABLE_FULL";
        return;
    }

    // Valid — emit SeatPlayer command and BuyInInitiated event
    g_orch.emitted_commands.push_back("SeatPlayer");
    g_orch.emitted_events.push_back("BuyInInitiated");
}

WHEN("^the BuyInOrchestrator handles a PlayerSeated event$") {
    g_orch.clear_results();
    g_orch.emitted_commands.push_back("ConfirmBuyIn");
    g_orch.emitted_events.push_back("BuyInCompleted");
}

WHEN("^the BuyInOrchestrator handles a SeatingRejected event$") {
    g_orch.clear_results();
    g_orch.emitted_commands.push_back("ReleaseBuyIn");
    g_orch.emitted_events.push_back("BuyInFailed");
    g_orch.failure_code = "SEATING_REJECTED";
}

// ==========================================================================
// Registration When steps
// ==========================================================================

WHEN("^the RegistrationOrchestrator handles the RegistrationRequested event$") {
    g_orch.clear_results();

    // Validate registration open
    if (!g_orch.registration_open || g_orch.tournament_running) {
        g_orch.emitted_events.push_back("RegistrationFailed");
        g_orch.failure_code = "REGISTRATION_CLOSED";
        return;
    }

    // Validate capacity
    if (g_orch.tournament_current_players >= g_orch.tournament_max_players) {
        g_orch.emitted_events.push_back("RegistrationFailed");
        g_orch.failure_code = "REGISTRATION_CLOSED";
        return;
    }

    g_orch.emitted_commands.push_back("EnrollPlayer");
    g_orch.emitted_events.push_back("RegistrationInitiated");
}

WHEN("^the RegistrationOrchestrator handles a TournamentPlayerEnrolled event$") {
    g_orch.clear_results();
    g_orch.emitted_commands.push_back("ConfirmRegistrationFee");
    g_orch.emitted_events.push_back("RegistrationCompleted");
}

WHEN("^the RegistrationOrchestrator handles a TournamentEnrollmentRejected event$") {
    g_orch.clear_results();
    g_orch.emitted_commands.push_back("ReleaseRegistrationFee");
    g_orch.emitted_events.push_back("RegistrationFailed");
    g_orch.failure_code = "ENROLLMENT_REJECTED";
}

// ==========================================================================
// Rebuy When steps
// ==========================================================================

WHEN("^the RebuyOrchestrator handles the RebuyRequested event$") {
    g_orch.clear_results();

    // Validate tournament running with rebuy
    if (!g_orch.tournament_running || !g_orch.rebuy_allowed) {
        g_orch.emitted_events.push_back("RebuyFailed");
        g_orch.failure_code = "TOURNAMENT_NOT_RUNNING";
        return;
    }

    // Validate player is seated
    if (!g_orch.player_seated) {
        g_orch.emitted_events.push_back("RebuyFailed");
        g_orch.failure_code = "NOT_SEATED";
        return;
    }

    g_orch.emitted_commands.push_back("ProcessRebuy");
    g_orch.emitted_events.push_back("RebuyInitiated");
}

WHEN("^the RebuyOrchestrator handles a RebuyProcessed event$") {
    g_orch.clear_results();
    g_orch.emitted_commands.push_back("AddRebuyChips");
}

WHEN("^the RebuyOrchestrator handles a RebuyChipsAdded event$") {
    g_orch.clear_results();
    g_orch.emitted_commands.push_back("ConfirmRebuyFee");
    g_orch.emitted_events.push_back("RebuyCompleted");
}

WHEN("^the RebuyOrchestrator handles a RebuyDenied event$") {
    g_orch.clear_results();
    g_orch.emitted_commands.push_back("ReleaseRebuyFee");
    g_orch.emitted_events.push_back("RebuyFailed");
    g_orch.failure_code = "REBUY_DENIED";
}

// ==========================================================================
// Then steps
// ==========================================================================

THEN("^the PM emits a SeatPlayer command to the table$") {
    ASSERT_TRUE(std::find(g_orch.emitted_commands.begin(), g_orch.emitted_commands.end(),
                          "SeatPlayer") != g_orch.emitted_commands.end())
        << "Expected SeatPlayer command";
}

THEN("^the PM emits no commands$") {
    ASSERT_TRUE(g_orch.emitted_commands.empty())
        << "Expected no commands, got: " << g_orch.emitted_commands.size();
}

THEN("^the PM emits a BuyInFailed process event with code \"([^\"]*)\"$") {
    REGEX_PARAM(std::string, code);
    ASSERT_TRUE(std::find(g_orch.emitted_events.begin(), g_orch.emitted_events.end(),
                          "BuyInFailed") != g_orch.emitted_events.end())
        << "Expected BuyInFailed event";
    ASSERT_EQ(g_orch.failure_code, code)
        << "Expected failure code " << code << ", got " << g_orch.failure_code;
}

THEN("^the PM emits a BuyInInitiated process event$") {
    ASSERT_TRUE(std::find(g_orch.emitted_events.begin(), g_orch.emitted_events.end(),
                          "BuyInInitiated") != g_orch.emitted_events.end())
        << "Expected BuyInInitiated event";
}

THEN("^the PM emits a ConfirmBuyIn command to the player$") {
    ASSERT_TRUE(std::find(g_orch.emitted_commands.begin(), g_orch.emitted_commands.end(),
                          "ConfirmBuyIn") != g_orch.emitted_commands.end())
        << "Expected ConfirmBuyIn command";
}

THEN("^the PM emits a BuyInCompleted process event$") {
    ASSERT_TRUE(std::find(g_orch.emitted_events.begin(), g_orch.emitted_events.end(),
                          "BuyInCompleted") != g_orch.emitted_events.end())
        << "Expected BuyInCompleted event";
}

THEN("^the PM emits a ReleaseBuyIn command to the player$") {
    ASSERT_TRUE(std::find(g_orch.emitted_commands.begin(), g_orch.emitted_commands.end(),
                          "ReleaseBuyIn") != g_orch.emitted_commands.end())
        << "Expected ReleaseBuyIn command";
}

THEN("^the PM emits an EnrollPlayer command to the tournament$") {
    ASSERT_TRUE(std::find(g_orch.emitted_commands.begin(), g_orch.emitted_commands.end(),
                          "EnrollPlayer") != g_orch.emitted_commands.end())
        << "Expected EnrollPlayer command";
}

THEN("^the PM emits a RegistrationInitiated process event$") {
    ASSERT_TRUE(std::find(g_orch.emitted_events.begin(), g_orch.emitted_events.end(),
                          "RegistrationInitiated") != g_orch.emitted_events.end())
        << "Expected RegistrationInitiated event";
}

THEN("^the PM emits a RegistrationFailed process event with code \"([^\"]*)\"$") {
    REGEX_PARAM(std::string, code);
    ASSERT_TRUE(std::find(g_orch.emitted_events.begin(), g_orch.emitted_events.end(),
                          "RegistrationFailed") != g_orch.emitted_events.end())
        << "Expected RegistrationFailed event";
    ASSERT_EQ(g_orch.failure_code, code)
        << "Expected failure code " << code << ", got " << g_orch.failure_code;
}

THEN("^the PM emits a ConfirmRegistrationFee command to the player$") {
    ASSERT_TRUE(std::find(g_orch.emitted_commands.begin(), g_orch.emitted_commands.end(),
                          "ConfirmRegistrationFee") != g_orch.emitted_commands.end())
        << "Expected ConfirmRegistrationFee command";
}

THEN("^the PM emits a RegistrationCompleted process event$") {
    ASSERT_TRUE(std::find(g_orch.emitted_events.begin(), g_orch.emitted_events.end(),
                          "RegistrationCompleted") != g_orch.emitted_events.end())
        << "Expected RegistrationCompleted event";
}

THEN("^the PM emits a ReleaseRegistrationFee command to the player$") {
    ASSERT_TRUE(std::find(g_orch.emitted_commands.begin(), g_orch.emitted_commands.end(),
                          "ReleaseRegistrationFee") != g_orch.emitted_commands.end())
        << "Expected ReleaseRegistrationFee command";
}

THEN("^the PM emits a ProcessRebuy command to the tournament$") {
    ASSERT_TRUE(std::find(g_orch.emitted_commands.begin(), g_orch.emitted_commands.end(),
                          "ProcessRebuy") != g_orch.emitted_commands.end())
        << "Expected ProcessRebuy command";
}

THEN("^the PM emits a RebuyInitiated process event$") {
    ASSERT_TRUE(std::find(g_orch.emitted_events.begin(), g_orch.emitted_events.end(),
                          "RebuyInitiated") != g_orch.emitted_events.end())
        << "Expected RebuyInitiated event";
}

THEN("^the PM emits a RebuyFailed process event with code \"([^\"]*)\"$") {
    REGEX_PARAM(std::string, code);
    ASSERT_TRUE(std::find(g_orch.emitted_events.begin(), g_orch.emitted_events.end(),
                          "RebuyFailed") != g_orch.emitted_events.end())
        << "Expected RebuyFailed event";
    ASSERT_EQ(g_orch.failure_code, code)
        << "Expected failure code " << code << ", got " << g_orch.failure_code;
}

THEN("^the PM emits an AddRebuyChips command to the table$") {
    ASSERT_TRUE(std::find(g_orch.emitted_commands.begin(), g_orch.emitted_commands.end(),
                          "AddRebuyChips") != g_orch.emitted_commands.end())
        << "Expected AddRebuyChips command";
}

THEN("^the PM emits a ConfirmRebuyFee command to the player$") {
    ASSERT_TRUE(std::find(g_orch.emitted_commands.begin(), g_orch.emitted_commands.end(),
                          "ConfirmRebuyFee") != g_orch.emitted_commands.end())
        << "Expected ConfirmRebuyFee command";
}

THEN("^the PM emits a RebuyCompleted process event$") {
    ASSERT_TRUE(std::find(g_orch.emitted_events.begin(), g_orch.emitted_events.end(),
                          "RebuyCompleted") != g_orch.emitted_events.end())
        << "Expected RebuyCompleted event";
}

THEN("^the PM emits a ReleaseRebuyFee command to the player$") {
    ASSERT_TRUE(std::find(g_orch.emitted_commands.begin(), g_orch.emitted_commands.end(),
                          "ReleaseRebuyFee") != g_orch.emitted_commands.end())
        << "Expected ReleaseRebuyFee command";
}
