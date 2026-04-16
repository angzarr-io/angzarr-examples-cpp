// Tournament aggregate gRPC server using functional router pattern.
//
// Manages tournament lifecycle: creation, registration, blind levels,
// rebuys, player elimination, and pause/resume.

#include <google/protobuf/any.pb.h>
#include <google/protobuf/timestamp.pb.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "angzarr/command_handler.grpc.pb.h"
#include "angzarr/helpers.hpp"
#include "angzarr/router.hpp"
#include "angzarr/types.pb.h"
#include "examples/tournament.pb.h"
#include "tournament_state.hpp"

namespace {

constexpr int DEFAULT_PORT = 50410;
constexpr const char* TOURNAMENT_DOMAIN = "tournament";

std::string bytes_to_hex(const std::string& bytes) {
    std::ostringstream ss;
    for (unsigned char c : bytes) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(c);
    }
    return ss.str();
}

// Handler functions following guard/validate/compute pattern

examples::TournamentCreated handle_create_tournament(
    const examples::CreateTournament& cmd,
    const tournament::TournamentState& state,
    int seq
) {
    if (state.exists()) throw angzarr::CommandRejectedError("Tournament already exists");
    if (cmd.name().empty()) throw angzarr::InvalidArgumentError("name is required");
    if (cmd.buy_in() <= 0) throw angzarr::InvalidArgumentError("buy_in must be positive");
    if (cmd.starting_stack() <= 0) throw angzarr::InvalidArgumentError("starting_stack must be positive");
    if (cmd.max_players() < 2) throw angzarr::InvalidArgumentError("max_players must be at least 2");

    examples::TournamentCreated event;
    event.set_name(cmd.name());
    event.set_game_variant(cmd.game_variant());
    event.set_buy_in(cmd.buy_in());
    event.set_starting_stack(cmd.starting_stack());
    event.set_max_players(cmd.max_players());
    event.set_min_players(cmd.min_players());
    if (cmd.has_rebuy_config()) *event.mutable_rebuy_config() = cmd.rebuy_config();
    for (const auto& level : cmd.blind_structure()) {
        *event.add_blind_structure() = level;
    }
    *event.mutable_created_at() = angzarr::helpers::now();
    return event;
}

examples::RegistrationOpened handle_open_registration_tournament(
    const examples::OpenRegistration&,
    const tournament::TournamentState& state,
    int
) {
    if (!state.exists()) throw angzarr::CommandRejectedError("Tournament does not exist");
    if (state.is_registration_open()) throw angzarr::CommandRejectedError("Registration already open");
    if (state.is_running()) throw angzarr::CommandRejectedError("Tournament is running");

    examples::RegistrationOpened event;
    *event.mutable_opened_at() = angzarr::helpers::now();
    return event;
}

examples::RegistrationClosed handle_close_registration_tournament(
    const examples::CloseRegistration&,
    const tournament::TournamentState& state,
    int
) {
    if (!state.is_registration_open()) throw angzarr::CommandRejectedError("Registration not open");

    examples::RegistrationClosed event;
    event.set_total_registrations(static_cast<int32_t>(state.registered_players.size()));
    *event.mutable_closed_at() = angzarr::helpers::now();
    return event;
}

// EnrollPlayer returns either TournamentPlayerEnrolled or TournamentEnrollmentRejected
// For the router pattern, we need to handle dual-event responses differently
// The simplest approach: return the success event, throw for business rejections
// But enrollment rejections are events (not errors) — PM needs them for compensation.
// We'll return enrollment event; rejection is handled at the handler level.

angzarr::EventBook handle_enroll_player_full(
    const examples::EnrollPlayer& cmd,
    const tournament::TournamentState& state,
    int seq
) {
    std::string root_hex = bytes_to_hex(cmd.player_root());

    if (!state.is_registration_open()) {
        examples::TournamentEnrollmentRejected event;
        event.set_player_root(cmd.player_root());
        event.set_reservation_id(cmd.reservation_id());
        event.set_reason("closed");
        *event.mutable_rejected_at() = angzarr::helpers::now();
        return angzarr::helpers::new_event_book(event, seq);
    }
    if (state.is_full()) {
        examples::TournamentEnrollmentRejected event;
        event.set_player_root(cmd.player_root());
        event.set_reservation_id(cmd.reservation_id());
        event.set_reason("full");
        *event.mutable_rejected_at() = angzarr::helpers::now();
        return angzarr::helpers::new_event_book(event, seq);
    }
    if (state.is_player_registered(root_hex)) {
        examples::TournamentEnrollmentRejected event;
        event.set_player_root(cmd.player_root());
        event.set_reservation_id(cmd.reservation_id());
        event.set_reason("already_registered");
        *event.mutable_rejected_at() = angzarr::helpers::now();
        return angzarr::helpers::new_event_book(event, seq);
    }

    examples::TournamentPlayerEnrolled event;
    event.set_player_root(cmd.player_root());
    event.set_reservation_id(cmd.reservation_id());
    event.set_fee_paid(state.buy_in);
    event.set_starting_stack(state.starting_stack);
    event.set_registration_number(static_cast<int32_t>(state.registered_players.size()) + 1);
    *event.mutable_enrolled_at() = angzarr::helpers::now();
    return angzarr::helpers::new_event_book(event, seq);
}

examples::BlindLevelAdvanced handle_advance_blind_level(
    const examples::AdvanceBlindLevel&,
    const tournament::TournamentState& state,
    int
) {
    if (!state.is_running()) throw angzarr::CommandRejectedError("Tournament not running");

    int32_t next = state.current_level + 1;
    int64_t sb = 0, bb = 0, ante = 0;
    if (!state.blind_structure.empty()) {
        int idx = next - 1;
        if (idx >= static_cast<int>(state.blind_structure.size())) {
            idx = static_cast<int>(state.blind_structure.size()) - 1;
        }
        if (idx >= 0) {
            sb = state.blind_structure[idx].small_blind();
            bb = state.blind_structure[idx].big_blind();
            ante = state.blind_structure[idx].ante();
        }
    }

    examples::BlindLevelAdvanced event;
    event.set_level(next);
    event.set_small_blind(sb);
    event.set_big_blind(bb);
    event.set_ante(ante);
    *event.mutable_advanced_at() = angzarr::helpers::now();
    return event;
}

examples::PlayerEliminated handle_eliminate_player(
    const examples::EliminatePlayer& cmd,
    const tournament::TournamentState& state,
    int
) {
    if (!state.is_running()) throw angzarr::CommandRejectedError("Tournament not running");
    std::string root_hex = bytes_to_hex(cmd.player_root());
    if (!state.is_player_registered(root_hex)) throw angzarr::CommandRejectedError("Player not registered");

    examples::PlayerEliminated event;
    event.set_player_root(cmd.player_root());
    event.set_finish_position(state.players_remaining);
    event.set_hand_root(cmd.hand_root());
    *event.mutable_eliminated_at() = angzarr::helpers::now();
    return event;
}

examples::TournamentPaused handle_pause_tournament(
    const examples::PauseTournament& cmd,
    const tournament::TournamentState& state,
    int
) {
    if (!state.is_running()) throw angzarr::CommandRejectedError("Tournament not running");

    examples::TournamentPaused event;
    event.set_reason(cmd.reason());
    *event.mutable_paused_at() = angzarr::helpers::now();
    return event;
}

examples::TournamentResumed handle_resume_tournament(
    const examples::ResumeTournament&,
    const tournament::TournamentState& state,
    int
) {
    if (state.status != examples::TOURNAMENT_PAUSED) {
        throw angzarr::CommandRejectedError("Tournament not paused");
    }

    examples::TournamentResumed event;
    *event.mutable_resumed_at() = angzarr::helpers::now();
    return event;
}

}  // namespace

int main(int argc, char** argv) {
    std::string port_str = std::to_string(DEFAULT_PORT);
    if (const char* env_port = std::getenv("PORT")) {
        port_str = env_port;
    }

    auto state_rebuilder = [](const angzarr::EventBook* eb) {
        return tournament::TournamentState::from_event_book(eb);
    };

    auto router = angzarr::CommandRouter<tournament::TournamentState>(TOURNAMENT_DOMAIN, state_rebuilder)
        .on<examples::CreateTournament, examples::TournamentCreated>(handle_create_tournament)
        .on<examples::OpenRegistration, examples::RegistrationOpened>(handle_open_registration_tournament)
        .on<examples::CloseRegistration, examples::RegistrationClosed>(handle_close_registration_tournament)
        .on<examples::AdvanceBlindLevel, examples::BlindLevelAdvanced>(handle_advance_blind_level)
        .on<examples::EliminatePlayer, examples::PlayerEliminated>(handle_eliminate_player)
        .on<examples::PauseTournament, examples::TournamentPaused>(handle_pause_tournament)
        .on<examples::ResumeTournament, examples::TournamentResumed>(handle_resume_tournament);
    // Note: EnrollPlayer and ProcessRebuy require dual-event responses
    // (success OR rejection), which the simple on<Cmd, Event> pattern doesn't support.
    // These are handled via the full handler interface in production.

    std::string server_address = "0.0.0.0:" + port_str;
    std::cout << "Tournament aggregate listening on " << server_address << std::endl;

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // TODO: Register CommandHandlerService with router
    auto server = builder.BuildAndStart();
    server->Wait();

    return 0;
}
