#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "angzarr/types.pb.h"
#include "examples/tournament.pb.h"

namespace tournament {

// Phase I-Cpp-v3 HIGH-EX-2.4.2 — pending penalty record (mirrors Java
// TournamentState.ActivePenalty). TDA Rule 71 / WSOP Rule 113 tracking.
struct ActivePenalty {
  std::string type;
  int32_t rounds_remaining = 0;

  ActivePenalty() = default;
  ActivePenalty(const std::string& t, int32_t r)
      : type(t), rounds_remaining(r) {}

  void decrement() {
    if (rounds_remaining > 0) rounds_remaining--;
  }
};

// Phase I-Cpp-v3 HIGH-EX-2.4.2 — per-player bag-and-tag snapshot (mirrors
// Java TournamentState.BagSnapshot). WSOP Rule 122 end-of-day capture.
struct BagSnapshot {
  std::string player_hex;
  int64_t stack = 0;
  int32_t seat = 0;
  std::string table_hex;
};

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
  std::map<std::string, examples::PlayerRegistration>
      registered_players;  // player_root_hex -> reg
  int32_t players_remaining = 0;
  int64_t total_prize_pool = 0;

  // Phase I-Cpp-v3 HIGH-EX-2.4.2 — advanced-handler state.
  bool hand_for_hand = false;
  int32_t hand_for_hand_round = 0;
  std::set<std::string> hand_for_hand_active_tables;
  std::set<std::string> hand_for_hand_pending_tables;
  std::map<std::string, ActivePenalty>
      active_penalties;  // player_hex -> penalty
  std::set<std::string> no_show_players;
  std::set<std::string> disqualified_players;
  bool new_hands_halted = false;
  std::vector<BagSnapshot> bag_snapshots;
  int32_t mixed_game_index = 0;
  std::map<std::string, int64_t> bounty_totals;  // player_hex -> total bounty
  int64_t total_chips_in_play = 0;

  bool exists() const { return !name.empty(); }
  bool is_registration_open() const {
    return status == examples::TOURNAMENT_REGISTRATION_OPEN;
  }
  bool is_running() const { return status == examples::TOURNAMENT_RUNNING; }
  bool is_full() const {
    return static_cast<int32_t>(registered_players.size()) >= max_players;
  }
  bool is_player_registered(const std::string& root_hex) const {
    return registered_players.count(root_hex) > 0;
  }
  int32_t player_rebuy_count(const std::string& root_hex) const {
    auto it = registered_players.find(root_hex);
    return it != registered_players.end() ? it->second.rebuys_used() : 0;
  }

  // Phase I-Cpp-v3 HIGH-EX-2.4.2 helpers.
  bool is_hand_for_hand() const { return hand_for_hand; }
  bool is_on_penalty(const std::string& player_hex) const {
    auto it = active_penalties.find(player_hex);
    return it != active_penalties.end() && it->second.rounds_remaining > 0;
  }
  bool is_no_show(const std::string& player_hex) const {
    return no_show_players.count(player_hex) > 0;
  }
  bool is_disqualified(const std::string& player_hex) const {
    return disqualified_players.count(player_hex) > 0;
  }
  bool is_new_hands_halted() const { return new_hands_halted; }

  static TournamentState from_event_book(const angzarr::EventBook* eb);
};

}  // namespace tournament
