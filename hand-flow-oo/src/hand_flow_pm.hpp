#pragma once

#include <google/protobuf/any.pb.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "angzarr/destinations.hpp"
#include "angzarr/errors.hpp"
#include "angzarr/macros.hpp"
#include "angzarr/process_manager.grpc.pb.h"
#include "angzarr/process_manager.hpp"
#include "angzarr/saga.pb.h"
#include "angzarr/types.pb.h"
#include "examples/hand.pb.h"
#include "examples/poker_types.pb.h"
#include "examples/table.pb.h"

namespace hand_flow_oo {

// Phase I-Cpp-v3 HIGH-EX-4.1 — 9-phase canonical enum matching Py/Rs.
// The previous 5-phase variant left DRAW / SHOWDOWN / AWARDING_POT /
// WAITING_FOR_START unrepresented; this lift makes each enum value an
// observable transition step. Mirrors Java HandFlowProcessManager.HandPhase
// (post-I-Java-v2a HIGH-EX-4.1 expansion).
enum class HandPhase {
  WAITING_FOR_START = 0,
  DEALING = 1,
  POSTING_BLINDS = 2,
  BETTING = 3,
  DEALING_COMMUNITY = 4,
  DRAW = 5,
  SHOWDOWN = 6,
  AWARDING_POT = 7,
  COMPLETE = 8,
};

// docs:start:pm_state_oo
/// Process manager state for a single hand.
///
/// Phase I-Cpp-v3: tracks the 9-phase enum to mirror Java
/// HandFlowProcessManager.
struct PMState {
  std::string hand_root;
  bool hand_in_progress = false;
  // Phase I-Cpp-v3 HIGH-EX-4.1 — observable phase. Default WAITING_FOR_START
  // (Py-canonical initial state); HandStarted advances to DEALING.
  HandPhase phase = HandPhase::WAITING_FOR_START;
  // Phase I-Cpp-v3 HIGH-EX-4.1 — variant routing for advancePhase.
  int32_t game_variant = 0;
  int32_t betting_phase = examples::PREFLOP;
};
// docs:end:pm_state_oo

// Phase I-Cpp-v3 HIGH-EX-4.1 — pure phase-transition table.
//
// Drives a process from one observable HandPhase to the next based on the
// event class and (when applicable) the betting phase + game variant. Mirrors
// the Java advancePhase logic in HandFlowProcessManager.advancePhaseHoldem /
// advancePhaseDraw.
//
// `event_name` is the proto message simple name (e.g. "HandStarted",
// "PotAwarded"). The function is intentionally pure: testing is direct on the
// truth table rather than via the wire-protocol PM.
HandPhase next_phase(HandPhase current, const std::string& event_name,
                     int32_t betting_phase, int32_t game_variant);

/// Pure helper: Hold'em / Omaha branch of advancePhase.
/// Returns the next phase given the just-completed betting_phase.
HandPhase advance_phase_holdem(int32_t betting_phase);

/// Pure helper: Five-card-draw branch of advancePhase.
HandPhase advance_phase_draw(int32_t betting_phase, HandPhase current);

// docs:start:pm_handler_oo
/// Hand Flow Process Manager using OO-style explicit registration.
///
/// This PM orchestrates poker hand flow by:
/// - Tracking when hands start and complete
/// - Coordinating between table and hand domains
/// - Carrying the 9-phase HandPhase through observable transitions
class HandFlowPM {
 public:
  HandFlowPM() {
    // Register prepare handlers
    prepare_handlers_["HandStarted"] =
        [this](const google::protobuf::Any& any) {
          examples::HandStarted evt;
          any.UnpackTo(&evt);
          return prepare_HandStarted(evt);
        };

    // Register event handlers
    handlers_["HandStarted"] = [this](const google::protobuf::Any& any,
                                      const std::string& corr_id) {
      examples::HandStarted evt;
      any.UnpackTo(&evt);
      return handle_HandStarted(evt, corr_id);
    };
    handlers_["CardsDealt"] = [this](const google::protobuf::Any& any,
                                     const std::string& corr_id) {
      examples::CardsDealt evt;
      any.UnpackTo(&evt);
      return handle_CardsDealt(evt, corr_id);
    };
    handlers_["BlindPosted"] = [this](const google::protobuf::Any& any,
                                      const std::string& corr_id) {
      examples::BlindPosted evt;
      any.UnpackTo(&evt);
      return handle_BlindPosted(evt, corr_id);
    };
    handlers_["ActionTaken"] = [this](const google::protobuf::Any& any,
                                      const std::string& corr_id) {
      examples::ActionTaken evt;
      any.UnpackTo(&evt);
      return handle_ActionTaken(evt, corr_id);
    };
    handlers_["CommunityCardsDealt"] = [this](const google::protobuf::Any& any,
                                              const std::string& corr_id) {
      examples::CommunityCardsDealt evt;
      any.UnpackTo(&evt);
      return handle_CommunityCardsDealt(evt, corr_id);
    };
    handlers_["ShowdownStarted"] = [this](const google::protobuf::Any& any,
                                          const std::string& corr_id) {
      examples::ShowdownStarted evt;
      any.UnpackTo(&evt);
      return handle_ShowdownStarted(evt, corr_id);
    };
    handlers_["PotAwarded"] = [this](const google::protobuf::Any& any,
                                     const std::string& corr_id) {
      examples::PotAwarded evt;
      any.UnpackTo(&evt);
      return handle_PotAwarded(evt, corr_id);
    };

    // Register event appliers (advance the observable HandPhase).
    appliers_["HandStarted"] = [this](PMState& state,
                                      const google::protobuf::Any& any) {
      examples::HandStarted evt;
      any.UnpackTo(&evt);
      apply_HandStarted(state, evt);
    };
    appliers_["CardsDealt"] = [this](PMState& state,
                                     const google::protobuf::Any& any) {
      examples::CardsDealt evt;
      any.UnpackTo(&evt);
      apply_CardsDealt(state, evt);
    };
    appliers_["BlindPosted"] = [this](PMState& state,
                                      const google::protobuf::Any& any) {
      examples::BlindPosted evt;
      any.UnpackTo(&evt);
      apply_BlindPosted(state, evt);
    };
    appliers_["CommunityCardsDealt"] =
        [this](PMState& state, const google::protobuf::Any& any) {
          examples::CommunityCardsDealt evt;
          any.UnpackTo(&evt);
          apply_CommunityCardsDealt(state, evt);
        };
    appliers_["ShowdownStarted"] = [this](PMState& state,
                                          const google::protobuf::Any& any) {
      examples::ShowdownStarted evt;
      any.UnpackTo(&evt);
      apply_ShowdownStarted(state, evt);
    };
    appliers_["PotAwarded"] = [this](PMState& state,
                                     const google::protobuf::Any& any) {
      examples::PotAwarded evt;
      any.UnpackTo(&evt);
      apply_PotAwarded(state, evt);
    };
  }

  std::string name() const { return "pmg-hand-flow-oo"; }

  std::vector<std::string> input_domains() const { return {"table", "hand"}; }

  /// Prepare destinations for events (two-phase protocol).
  std::vector<angzarr::Cover> prepare_destinations(
      const angzarr::EventBook& book) {
    std::vector<angzarr::Cover> destinations;
    for (const auto& page : book.pages()) {
      if (!page.has_event()) continue;
      auto suffix =
          angzarr::helpers::type_name_from_url(page.event().type_url());
      auto it = prepare_handlers_.find(suffix);
      if (it != prepare_handlers_.end()) {
        auto covers = it->second(page.event());
        destinations.insert(destinations.end(), covers.begin(), covers.end());
      }
    }
    return destinations;
  }

  /// Dispatch events to handlers.
  std::vector<angzarr::CommandBook> dispatch(
      const angzarr::EventBook& book,
      const angzarr::EventBook* prior_events = nullptr,
      const angzarr::Destinations& /* destinations */ =
          angzarr::Destinations{}) {
    rebuild_state(prior_events);

    auto correlation_id = book.has_cover() ? book.cover().correlation_id() : "";
    if (correlation_id.empty()) {
      return {};  // PMs require correlation ID
    }

    std::vector<angzarr::CommandBook> commands;
    for (const auto& page : book.pages()) {
      if (!page.has_event()) continue;

      auto suffix =
          angzarr::helpers::type_name_from_url(page.event().type_url());

      // Apply event to state first
      auto applier_it = appliers_.find(suffix);
      if (applier_it != appliers_.end()) {
        applier_it->second(state_, page.event());
      }

      // Dispatch to handler
      auto it = handlers_.find(suffix);
      if (it != handlers_.end()) {
        auto cmds = it->second(page.event(), correlation_id);
        commands.insert(commands.end(), cmds.begin(), cmds.end());
      }
    }
    return commands;
  }

  const PMState& state() const { return state_; }

 protected:
  /// Declare the hand destination needed when a hand starts.
  std::vector<angzarr::Cover> prepare_HandStarted(
      const examples::HandStarted& evt) {
    angzarr::Cover cover;
    cover.set_domain("hand");
    cover.mutable_root()->set_value(evt.hand_root());
    return {cover};
  }

  std::vector<angzarr::CommandBook> handle_HandStarted(
      const examples::HandStarted& /*evt*/, const std::string& /*corr_id*/) {
    return {};
  }
  void apply_HandStarted(PMState& state, const examples::HandStarted& evt) {
    state.hand_root = evt.hand_root();
    state.hand_in_progress = true;
    state.game_variant = evt.game_variant();
    // Phase I-Cpp-v3 HIGH-EX-4.1 — WAITING_FOR_START → DEALING.
    state.phase = HandPhase::DEALING;
  }

  std::vector<angzarr::CommandBook> handle_CardsDealt(
      const examples::CardsDealt& /*evt*/, const std::string& /*corr_id*/) {
    return {};
  }
  void apply_CardsDealt(PMState& state, const examples::CardsDealt& /*evt*/) {
    // DEALING → POSTING_BLINDS
    if (state.phase == HandPhase::DEALING) {
      state.phase = HandPhase::POSTING_BLINDS;
    }
  }

  std::vector<angzarr::CommandBook> handle_BlindPosted(
      const examples::BlindPosted& /*evt*/, const std::string& /*corr_id*/) {
    return {};
  }
  void apply_BlindPosted(PMState& state, const examples::BlindPosted& evt) {
    // After both blinds posted, framework transitions to BETTING. Use
    // blind_type "big" as the trigger to mirror Java's startBetting on
    // big-blind-post path.
    if (evt.blind_type() == "big" && state.phase == HandPhase::POSTING_BLINDS) {
      state.phase = HandPhase::BETTING;
    }
  }

  std::vector<angzarr::CommandBook> handle_ActionTaken(
      const examples::ActionTaken& /*evt*/, const std::string& /*corr_id*/) {
    return {};
  }

  std::vector<angzarr::CommandBook> handle_CommunityCardsDealt(
      const examples::CommunityCardsDealt& /*evt*/,
      const std::string& /*corr_id*/) {
    return {};
  }
  void apply_CommunityCardsDealt(PMState& state,
                                 const examples::CommunityCardsDealt& evt) {
    state.betting_phase = evt.phase();
    // DEALING_COMMUNITY or DRAW → BETTING.
    if (state.phase == HandPhase::DEALING_COMMUNITY ||
        state.phase == HandPhase::DRAW) {
      state.phase = HandPhase::BETTING;
    }
  }

  std::vector<angzarr::CommandBook> handle_ShowdownStarted(
      const examples::ShowdownStarted& /*evt*/,
      const std::string& /*corr_id*/) {
    return {};
  }
  void apply_ShowdownStarted(PMState& state,
                             const examples::ShowdownStarted& /*evt*/) {
    // Any non-terminal state → SHOWDOWN.
    if (state.phase != HandPhase::COMPLETE &&
        state.phase != HandPhase::WAITING_FOR_START) {
      state.phase = HandPhase::SHOWDOWN;
    }
  }

  std::vector<angzarr::CommandBook> handle_PotAwarded(
      const examples::PotAwarded& /*evt*/, const std::string& /*corr_id*/) {
    return {};
  }
  void apply_PotAwarded(PMState& state, const examples::PotAwarded& /*evt*/) {
    // Even fold-out routes go through AWARDING_POT before COMPLETE so the
    // intermediate state has an observable home (Java pattern).
    if (state.phase != HandPhase::AWARDING_POT)
      state.phase = HandPhase::AWARDING_POT;
    state.phase = HandPhase::COMPLETE;
    state.hand_in_progress = false;
  }

 private:
  void rebuild_state(const angzarr::EventBook* event_book) {
    state_ = PMState{};
    if (!event_book) return;

    for (const auto& page : event_book->pages()) {
      if (!page.has_event()) continue;
      auto suffix =
          angzarr::helpers::type_name_from_url(page.event().type_url());
      auto it = appliers_.find(suffix);
      if (it != appliers_.end()) {
        it->second(state_, page.event());
      }
    }
  }

  using EventHandler = std::function<std::vector<angzarr::CommandBook>(
      const google::protobuf::Any&, const std::string&)>;
  using PrepareHandler =
      std::function<std::vector<angzarr::Cover>(const google::protobuf::Any&)>;
  using EventApplier =
      std::function<void(PMState&, const google::protobuf::Any&)>;

  std::map<std::string, EventHandler> handlers_;
  std::map<std::string, PrepareHandler> prepare_handlers_;
  std::map<std::string, EventApplier> appliers_;
  PMState state_;
};
// docs:end:pm_handler_oo

}  // namespace hand_flow_oo
