#pragma once

#include <map>
#include <string>
#include <vector>

#include "angzarr/types.pb.h"
#include "examples/tournament.pb.h"

namespace tournament {

struct TournamentState {
    std::string name;
    examples::GameVariant game_variant = examples::GAME_VARIANT_UNSPECIFIED;
    examples::TournamentStatus status = examples::TOURNAMENT_STATUS_UNSPECIFIED;
    int64_t buy_in = 0;
    int64_t starting_stack = 0;
    int32_t max_players = 0;
    int32_t min_players = 0;
    examples::RebuyConfig rebuy_config;
    bool has_rebuy_config = false;
    std::vector<examples::BlindLevel> blind_structure;
    int32_t current_level = 0;
    std::map<std::string, examples::PlayerRegistration> registered_players;  // player_root_hex -> reg
    int32_t players_remaining = 0;
    int64_t total_prize_pool = 0;

    bool exists() const { return !name.empty(); }
    bool is_registration_open() const { return status == examples::TOURNAMENT_REGISTRATION_OPEN; }
    bool is_running() const { return status == examples::TOURNAMENT_RUNNING; }
    bool is_full() const { return static_cast<int32_t>(registered_players.size()) >= max_players; }
    bool is_player_registered(const std::string& root_hex) const {
        return registered_players.count(root_hex) > 0;
    }
    int32_t player_rebuy_count(const std::string& root_hex) const {
        auto it = registered_players.find(root_hex);
        return it != registered_players.end() ? it->second.rebuys_used() : 0;
    }

    static TournamentState from_event_book(const angzarr::EventBook* eb);
};

}  // namespace tournament
