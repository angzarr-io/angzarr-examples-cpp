#include "tournament_state.hpp"

#include <iomanip>
#include <sstream>

#include "angzarr/helpers.hpp"

namespace tournament {

static std::string bytes_to_hex(const std::string& bytes) {
    std::ostringstream ss;
    for (unsigned char c : bytes) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(c);
    }
    return ss.str();
}

static void apply_created(TournamentState& state, const examples::TournamentCreated& event) {
    state.name = event.name();
    state.game_variant = event.game_variant();
    state.status = examples::TOURNAMENT_CREATED;
    state.buy_in = event.buy_in();
    state.starting_stack = event.starting_stack();
    state.max_players = event.max_players();
    state.min_players = event.min_players();
    if (event.has_rebuy_config()) {
        state.rebuy_config = event.rebuy_config();
        state.has_rebuy_config = true;
    }
    state.blind_structure.clear();
    for (const auto& level : event.blind_structure()) {
        state.blind_structure.push_back(level);
    }
    state.current_level = 0;
    state.players_remaining = 0;
    state.total_prize_pool = 0;
}

static void apply_registration_opened(TournamentState& state, const examples::RegistrationOpened&) {
    state.status = examples::TOURNAMENT_REGISTRATION_OPEN;
}

static void apply_registration_closed(TournamentState& state, const examples::RegistrationClosed&) {
    state.status = examples::TOURNAMENT_CREATED;
}

static void apply_player_enrolled(TournamentState& state, const examples::TournamentPlayerEnrolled& event) {
    std::string root_hex = bytes_to_hex(event.player_root());
    examples::PlayerRegistration reg;
    reg.set_player_root(event.player_root());
    reg.set_fee_paid(event.fee_paid());
    reg.set_starting_stack(event.starting_stack());
    state.registered_players[root_hex] = reg;
    state.players_remaining++;
    state.total_prize_pool += event.fee_paid();
}

static void apply_tournament_started(TournamentState& state, const examples::TournamentStarted&) {
    state.status = examples::TOURNAMENT_RUNNING;
}

static void apply_rebuy_processed(TournamentState& state, const examples::RebuyProcessed& event) {
    std::string root_hex = bytes_to_hex(event.player_root());
    auto it = state.registered_players.find(root_hex);
    if (it != state.registered_players.end()) {
        it->second.set_rebuys_used(event.rebuy_count());
    }
    state.total_prize_pool += event.rebuy_cost();
}

static void apply_blind_advanced(TournamentState& state, const examples::BlindLevelAdvanced& event) {
    state.current_level = event.level();
}

static void apply_player_eliminated(TournamentState& state, const examples::PlayerEliminated& event) {
    std::string root_hex = bytes_to_hex(event.player_root());
    state.registered_players.erase(root_hex);
    state.players_remaining--;
}

static void apply_paused(TournamentState& state, const examples::TournamentPaused&) {
    state.status = examples::TOURNAMENT_PAUSED;
}

static void apply_resumed(TournamentState& state, const examples::TournamentResumed&) {
    state.status = examples::TOURNAMENT_RUNNING;
}

static void apply_completed(TournamentState& state, const examples::TournamentCompleted&) {
    state.status = examples::TOURNAMENT_COMPLETED;
}

TournamentState TournamentState::from_event_book(const angzarr::EventBook* eb) {
    TournamentState state;
    if (!eb) return state;

    for (const auto& page : eb->pages()) {
        if (!page.has_event()) continue;
        const auto& event = page.event();

        if (event.Is<examples::TournamentCreated>()) {
            examples::TournamentCreated e;
            event.UnpackTo(&e);
            apply_created(state, e);
        } else if (event.Is<examples::RegistrationOpened>()) {
            examples::RegistrationOpened e;
            event.UnpackTo(&e);
            apply_registration_opened(state, e);
        } else if (event.Is<examples::RegistrationClosed>()) {
            examples::RegistrationClosed e;
            event.UnpackTo(&e);
            apply_registration_closed(state, e);
        } else if (event.Is<examples::TournamentPlayerEnrolled>()) {
            examples::TournamentPlayerEnrolled e;
            event.UnpackTo(&e);
            apply_player_enrolled(state, e);
        } else if (event.Is<examples::TournamentStarted>()) {
            examples::TournamentStarted e;
            event.UnpackTo(&e);
            apply_tournament_started(state, e);
        } else if (event.Is<examples::RebuyProcessed>()) {
            examples::RebuyProcessed e;
            event.UnpackTo(&e);
            apply_rebuy_processed(state, e);
        } else if (event.Is<examples::BlindLevelAdvanced>()) {
            examples::BlindLevelAdvanced e;
            event.UnpackTo(&e);
            apply_blind_advanced(state, e);
        } else if (event.Is<examples::PlayerEliminated>()) {
            examples::PlayerEliminated e;
            event.UnpackTo(&e);
            apply_player_eliminated(state, e);
        } else if (event.Is<examples::TournamentPaused>()) {
            examples::TournamentPaused e;
            event.UnpackTo(&e);
            apply_paused(state, e);
        } else if (event.Is<examples::TournamentResumed>()) {
            examples::TournamentResumed e;
            event.UnpackTo(&e);
            apply_resumed(state, e);
        } else if (event.Is<examples::TournamentCompleted>()) {
            examples::TournamentCompleted e;
            event.UnpackTo(&e);
            apply_completed(state, e);
        }
    }
    return state;
}

}  // namespace tournament
