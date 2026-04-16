/**
 * Player -> Table saga using OO pattern.
 *
 * Reacts to PlayerSittingOut and PlayerReturningToPlay events from Player domain.
 * Emits PlayerSatOut and PlayerSatIn facts to Table domain.
 */
#include <google/protobuf/any.pb.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>

#include <iostream>
#include <memory>
#include <string>

#include "angzarr/macros.hpp"
#include "angzarr/saga.grpc.pb.h"
#include "angzarr/saga.hpp"
#include "angzarr/types.pb.h"
#include "examples/player.pb.h"
#include "examples/table.pb.h"

namespace {

constexpr int DEFAULT_PORT = 50214;

/**
 * Saga: Player -> Table (OO Pattern)
 *
 * Reacts to PlayerSittingOut and PlayerReturningToPlay events from Player domain.
 * Emits PlayerSatOut and PlayerSatIn facts to Table domain.
 *
 * Uses explicit handler registration because these handlers emit facts (not commands)
 * and need access to the source root.
 */
class PlayerTableSaga : public angzarr::Saga {
   public:
    ANGZARR_SAGA("saga-player-table", "player", "table")

    PlayerTableSaga() {
        register_event_handler("PlayerSittingOut",
            [](angzarr::Saga* self, const google::protobuf::Any& any,
               const std::string& /*corr_id*/) -> std::vector<angzarr::CommandBook> {
                examples::PlayerSittingOut event;
                any.UnpackTo(&event);
                static_cast<PlayerTableSaga*>(self)->handle_PlayerSittingOut(event);
                return {};
            });

        register_event_handler("PlayerReturningToPlay",
            [](angzarr::Saga* self, const google::protobuf::Any& any,
               const std::string& /*corr_id*/) -> std::vector<angzarr::CommandBook> {
                examples::PlayerReturningToPlay event;
                any.UnpackTo(&event);
                static_cast<PlayerTableSaga*>(self)->handle_PlayerReturningToPlay(event);
                return {};
            });
    }

    void set_source_root(const std::string& root) { source_root_ = root; }

   protected:
    void handle_PlayerSittingOut(const examples::PlayerSittingOut& event) {
        examples::PlayerSatOut sat_out;
        sat_out.set_player_root(source_root_);
        *sat_out.mutable_sat_out_at() = event.sat_out_at();

        google::protobuf::Any fact_any;
        fact_any.PackFrom(sat_out, "type.googleapis.com/");

        angzarr::EventBook fact;
        auto* cover = fact.mutable_cover();
        cover->set_domain(output_domain());
        cover->mutable_root()->set_value(event.table_root());

        auto* page = fact.add_pages();
        page->mutable_header()->mutable_angzarr_deferred();
        *page->mutable_event() = fact_any;

        emit_fact(std::move(fact));
    }

    void handle_PlayerReturningToPlay(const examples::PlayerReturningToPlay& event) {
        examples::PlayerSatIn sat_in;
        sat_in.set_player_root(source_root_);
        *sat_in.mutable_sat_in_at() = event.sat_in_at();

        google::protobuf::Any fact_any;
        fact_any.PackFrom(sat_in, "type.googleapis.com/");

        angzarr::EventBook fact;
        auto* cover = fact.mutable_cover();
        cover->set_domain(output_domain());
        cover->mutable_root()->set_value(event.table_root());

        auto* page = fact.add_pages();
        page->mutable_header()->mutable_angzarr_deferred();
        *page->mutable_event() = fact_any;

        emit_fact(std::move(fact));
    }

   private:
    std::string source_root_;
};

/**
 * gRPC service for Player->Table saga using OO pattern.
 */
class PlayerTableSagaService final : public angzarr::SagaService::Service {
   public:
    grpc::Status Handle(grpc::ServerContext* context, const angzarr::SagaHandleRequest* request,
                        angzarr::SagaResponse* response) override {
        (void)context;

        // Set source root for handler access
        if (request->source().has_cover() && request->source().cover().has_root()) {
            saga_.set_source_root(request->source().cover().root().value());
        }

        std::vector<angzarr::EventBook> destinations;
        auto result = saga_.dispatch(request->source(), destinations);

        for (const auto& cmd : result.commands) {
            *response->add_commands() = cmd;
        }
        for (const auto& fact : result.facts) {
            *response->add_events() = fact;
        }

        return grpc::Status::OK;
    }

   private:
    PlayerTableSaga saga_;
};

}  // anonymous namespace

int main(int argc, char** argv) {
    int port = DEFAULT_PORT;
    if (const char* env_port = std::getenv("GRPC_PORT")) {
        port = std::stoi(env_port);
    } else if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    std::string server_address = "0.0.0.0:" + std::to_string(port);

    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    PlayerTableSagaService service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Player->Table saga (OO) listening on " << server_address << std::endl;

    server->Wait();
    return 0;
}
