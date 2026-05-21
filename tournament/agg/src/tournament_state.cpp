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

static void apply_created(TournamentState& state,
                          const examples::TournamentCreated& event) {
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

static void apply_registration_opened(TournamentState& state,
                                      const examples::RegistrationOpened&) {
  state.status = examples::TOURNAMENT_REGISTRATION_OPEN;
}

static void apply_registration_closed(TournamentState& state,
                                      const examples::RegistrationClosed&) {
  state.status = examples::TOURNAMENT_CREATED;
}

static void apply_player_enrolled(
    TournamentState& state, const examples::TournamentPlayerEnrolled& event) {
  std::string root_hex = bytes_to_hex(event.player_root());
  examples::PlayerRegistration reg;
  reg.set_player_root(event.player_root());
  reg.set_fee_paid(event.fee_paid());
  reg.set_starting_stack(event.starting_stack());
  state.registered_players[root_hex] = reg;
  state.players_remaining++;
  state.total_prize_pool += event.fee_paid();
}

static void apply_tournament_started(TournamentState& state,
                                     const examples::TournamentStarted&) {
  state.status = examples::TOURNAMENT_RUNNING;
}

static void apply_rebuy_processed(TournamentState& state,
                                  const examples::RebuyProcessed& event) {
  std::string root_hex = bytes_to_hex(event.player_root());
  auto it = state.registered_players.find(root_hex);
  if (it != state.registered_players.end()) {
    it->second.set_rebuys_used(event.rebuy_count());
  }
  state.total_prize_pool += event.rebuy_cost();
}

static void apply_blind_advanced(TournamentState& state,
                                 const examples::BlindLevelAdvanced& event) {
  state.current_level = event.level();
}

static void apply_player_eliminated(TournamentState& state,
                                    const examples::PlayerEliminated& event) {
  std::string root_hex = bytes_to_hex(event.player_root());
  state.registered_players.erase(root_hex);
  state.players_remaining--;
}

static void apply_paused(TournamentState& state,
                         const examples::TournamentPaused&) {
  state.status = examples::TOURNAMENT_PAUSED;
}

static void apply_resumed(TournamentState& state,
                          const examples::TournamentResumed&) {
  state.status = examples::TOURNAMENT_RUNNING;
}

static void apply_completed(TournamentState& state,
                            const examples::TournamentCompleted&) {
  state.status = examples::TOURNAMENT_COMPLETED;
}

// Phase I-Cpp-v3 HIGH-EX-2.4.2 appliers.

static void apply_hand_for_hand_started(
    TournamentState& state, const examples::HandForHandStarted& event) {
  state.hand_for_hand = true;
  state.hand_for_hand_active_tables.clear();
  state.hand_for_hand_pending_tables.clear();
  for (const auto& root : event.active_table_roots()) {
    std::string hex = bytes_to_hex(root);
    state.hand_for_hand_active_tables.insert(hex);
    state.hand_for_hand_pending_tables.insert(hex);
  }
}

static void apply_hand_for_hand_round_complete(
    TournamentState& state, const examples::HandForHandRoundComplete& event) {
  state.hand_for_hand_round = event.round_number();
  state.hand_for_hand_pending_tables = state.hand_for_hand_active_tables;
}

static void apply_hand_for_hand_ended(TournamentState& state,
                                      const examples::HandForHandEnded&) {
  state.hand_for_hand = false;
  state.hand_for_hand_active_tables.clear();
  state.hand_for_hand_pending_tables.clear();
}

static void apply_player_moved_tables(
    TournamentState& state, const examples::PlayerMovedTables& event) {
  // Marker emit from record_table_hand_complete only sets from_table_root.
  if (!event.from_table_root().empty()) {
    std::string hex = bytes_to_hex(event.from_table_root());
    state.hand_for_hand_pending_tables.erase(hex);
  }
}

static void apply_penalty_issued(TournamentState& state,
                                 const examples::PenaltyIssued& event) {
  std::string root_hex = bytes_to_hex(event.player_root());
  state.active_penalties[root_hex] =
      ActivePenalty(event.type(), event.rounds());
}

static void apply_penalty_rounds_decremented(
    TournamentState& state, const examples::PenaltyRoundsDecremented& event) {
  std::string root_hex = bytes_to_hex(event.player_root());
  auto it = state.active_penalties.find(root_hex);
  if (it != state.active_penalties.end()) {
    it->second.rounds_remaining = event.rounds_remaining();
    if (it->second.rounds_remaining <= 0) {
      state.active_penalties.erase(it);
    }
  }
}

static void apply_player_disqualified(
    TournamentState& state, const examples::PlayerDisqualified& event) {
  std::string root_hex = bytes_to_hex(event.player_root());
  state.disqualified_players.insert(root_hex);
  state.total_chips_in_play -= event.chips_removed();
}

static void apply_no_show_detected(TournamentState& state,
                                   const examples::NoShowDetected& event) {
  std::string root_hex = bytes_to_hex(event.player_root());
  state.no_show_players.insert(root_hex);
  state.total_chips_in_play -= event.chips_removed();
}

static void apply_new_hands_halted(TournamentState& state,
                                   const examples::NewHandsHalted&) {
  state.new_hands_halted = true;
}

static void apply_bag_and_tag_complete(
    TournamentState& state, const examples::BagAndTagComplete& event) {
  state.bag_snapshots.clear();
  for (const auto& snap : event.snapshots()) {
    BagSnapshot s;
    s.player_hex = bytes_to_hex(snap.player_root());
    s.stack = snap.stack();
    s.seat = snap.seat();
    s.table_hex = bytes_to_hex(snap.table_root());
    state.bag_snapshots.push_back(s);
  }
}

static void apply_mixed_game_variant_rotated(
    TournamentState& state, const examples::MixedGameVariantRotated&) {
  state.mixed_game_index++;
}

static void apply_bounty_awarded(TournamentState& state,
                                 const examples::BountyAwarded& event) {
  std::string root_hex = bytes_to_hex(event.eliminator_root());
  state.bounty_totals[root_hex] += event.amount();
}

static void apply_player_re_entered(TournamentState& state,
                                    const examples::PlayerReEntered& event) {
  state.total_chips_in_play += (event.chips_added() - event.chips_forfeited());
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
    } else if (event.Is<examples::HandForHandStarted>()) {
      examples::HandForHandStarted e;
      event.UnpackTo(&e);
      apply_hand_for_hand_started(state, e);
    } else if (event.Is<examples::HandForHandRoundComplete>()) {
      examples::HandForHandRoundComplete e;
      event.UnpackTo(&e);
      apply_hand_for_hand_round_complete(state, e);
    } else if (event.Is<examples::HandForHandEnded>()) {
      examples::HandForHandEnded e;
      event.UnpackTo(&e);
      apply_hand_for_hand_ended(state, e);
    } else if (event.Is<examples::PlayerMovedTables>()) {
      examples::PlayerMovedTables e;
      event.UnpackTo(&e);
      apply_player_moved_tables(state, e);
    } else if (event.Is<examples::PenaltyIssued>()) {
      examples::PenaltyIssued e;
      event.UnpackTo(&e);
      apply_penalty_issued(state, e);
    } else if (event.Is<examples::PenaltyRoundsDecremented>()) {
      examples::PenaltyRoundsDecremented e;
      event.UnpackTo(&e);
      apply_penalty_rounds_decremented(state, e);
    } else if (event.Is<examples::PlayerDisqualified>()) {
      examples::PlayerDisqualified e;
      event.UnpackTo(&e);
      apply_player_disqualified(state, e);
    } else if (event.Is<examples::NoShowDetected>()) {
      examples::NoShowDetected e;
      event.UnpackTo(&e);
      apply_no_show_detected(state, e);
    } else if (event.Is<examples::NewHandsHalted>()) {
      examples::NewHandsHalted e;
      event.UnpackTo(&e);
      apply_new_hands_halted(state, e);
    } else if (event.Is<examples::BagAndTagComplete>()) {
      examples::BagAndTagComplete e;
      event.UnpackTo(&e);
      apply_bag_and_tag_complete(state, e);
    } else if (event.Is<examples::MixedGameVariantRotated>()) {
      examples::MixedGameVariantRotated e;
      event.UnpackTo(&e);
      apply_mixed_game_variant_rotated(state, e);
    } else if (event.Is<examples::BountyAwarded>()) {
      examples::BountyAwarded e;
      event.UnpackTo(&e);
      apply_bounty_awarded(state, e);
    } else if (event.Is<examples::PlayerReEntered>()) {
      examples::PlayerReEntered e;
      event.UnpackTo(&e);
      apply_player_re_entered(state, e);
    }
  }
  return state;
}

}  // namespace tournament
