// Tournament aggregate gRPC server.
//
// Phase I-Cpp-v3 HIGH-EX-2.4.1 — wires the handlers from handlers/ into the
// CommandRouter. Pure-function handlers live in dedicated translation units
// (create_handler / registration_handler / lifecycle_handler / rebuy_handler /
// advanced_handlers) to mirror the Java tournament module layout and keep
// per-handler mutation testing focused.

#include <google/protobuf/any.pb.h>
#include <google/protobuf/timestamp.pb.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "advanced_handlers.hpp"
#include "angzarr/command_handler.grpc.pb.h"
#include "angzarr/helpers.hpp"
#include "angzarr/router.hpp"
#include "angzarr/types.pb.h"
#include "create_handler.hpp"
#include "examples/tournament.pb.h"
#include "lifecycle_handler.hpp"
#include "rebuy_handler.hpp"
#include "registration_handler.hpp"
#include "tournament_state.hpp"

namespace {

constexpr int DEFAULT_PORT = 50410;
constexpr const char* TOURNAMENT_DOMAIN = "tournament";

}  // namespace

int main(int argc, char** argv) {
  std::string port_str = std::to_string(DEFAULT_PORT);
  if (const char* env_port = std::getenv("PORT")) {
    port_str = env_port;
  }
  (void)argc;
  (void)argv;

  auto state_rebuilder = [](const angzarr::EventBook* eb) {
    return tournament::TournamentState::from_event_book(eb);
  };

  auto router =
      angzarr::CommandRouter<tournament::TournamentState>(TOURNAMENT_DOMAIN,
                                                          state_rebuilder)
          // Core lifecycle
          .on<examples::CreateTournament, examples::TournamentCreated>(
              tournament::handlers::handle_create_tournament)
          .on<examples::OpenRegistration, examples::RegistrationOpened>(
              tournament::handlers::handle_open_registration)
          .on<examples::CloseRegistration, examples::RegistrationClosed>(
              tournament::handlers::handle_close_registration)
          .on<examples::AdvanceBlindLevel, examples::BlindLevelAdvanced>(
              tournament::handlers::handle_advance_blind_level)
          .on<examples::EliminatePlayer, examples::PlayerEliminated>(
              tournament::handlers::handle_eliminate_player)
          .on<examples::PauseTournament, examples::TournamentPaused>(
              tournament::handlers::handle_pause_tournament)
          .on<examples::ResumeTournament, examples::TournamentResumed>(
              tournament::handlers::handle_resume_tournament)
          // Phase I-Cpp-v3 HIGH-EX-2.4.2 advanced handlers (single-event
          // success path).
          .on<examples::EnterHandForHand, examples::HandForHandStarted>(
              tournament::handlers::handle_enter_hand_for_hand)
          .on<examples::RecordTableHandComplete, examples::PlayerMovedTables>(
              tournament::handlers::handle_record_table_hand_complete)
          .on<examples::RecordHandForHandRoundComplete,
              examples::HandForHandRoundComplete>(
              tournament::handlers::handle_record_hand_for_hand_round_complete)
          .on<examples::RecordHandForHandHand,
              examples::HandForHandHandRecorded>(
              tournament::handlers::handle_record_hand_for_hand_hand)
          .on<examples::ColorUp, examples::ColorUpCompleted>(
              tournament::handlers::handle_color_up)
          .on<examples::RebalanceTables, examples::PlayerMovedBetweenTables>(
              tournament::handlers::handle_rebalance_tables)
          .on<examples::RecordSimultaneousBusts,
              examples::SimultaneousBustsRecorded>(
              tournament::handlers::handle_record_simultaneous_busts)
          .on<examples::TriggerSeatRedraw, examples::SeatRedrawTriggered>(
              tournament::handlers::handle_trigger_seat_redraw)
          .on<examples::ReseatAbsentPlayer, examples::PlayerMovedTables>(
              tournament::handlers::handle_reseat_absent_player)
          .on<examples::ReEntryPlayer, examples::PlayerReEntered>(
              tournament::handlers::handle_re_entry_player)
          .on<examples::AdvanceAbsentBlind, examples::AbsentBlindAdvanced>(
              tournament::handlers::handle_advance_absent_blind)
          .on<examples::AwardBounty, examples::BountyAwarded>(
              tournament::handlers::handle_award_bounty)
          .on<examples::DetectNoShow, examples::NoShowDetected>(
              tournament::handlers::handle_detect_no_show)
          .on<examples::IssuePenalty, examples::PenaltyIssued>(
              tournament::handlers::handle_issue_penalty)
          .on<examples::DecrementPenalty, examples::PenaltyRoundsDecremented>(
              tournament::handlers::handle_decrement_penalty)
          .on<examples::DisqualifyPlayer, examples::PlayerDisqualified>(
              tournament::handlers::handle_disqualify_player)
          .on<examples::StopNewHands, examples::NewHandsHalted>(
              tournament::handlers::handle_stop_new_hands)
          .on<examples::BagAndTag, examples::BagAndTagComplete>(
              tournament::handlers::handle_bag_and_tag)
          .on<examples::RotateMixedGameVariant,
              examples::MixedGameVariantRotated>(
              tournament::handlers::handle_rotate_mixed_game_variant);
  // Note: EnrollPlayer + ProcessRebuy emit dual-event responses (success
  // OR rejection); they're invoked via handle_enroll_player /
  // handle_process_rebuy from a wrapper at the wire boundary (the
  // pattern: rejection is a normal event, not an exception). Wire
  // wrapping is part of the server scaffold; the pure handlers live
  // alongside the simple-router entries above.

  std::string server_address = "0.0.0.0:" + port_str;
  std::cout << "Tournament aggregate listening on " << server_address
            << std::endl;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // TODO: Register CommandHandlerService with router
  auto server = builder.BuildAndStart();
  if (server) server->Wait();
  return 0;
}
