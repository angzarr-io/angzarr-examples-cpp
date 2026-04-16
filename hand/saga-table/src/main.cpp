/**
 * Hand -> Table saga using OO pattern.
 *
 * Reacts to HandComplete events from Hand domain.
 * Sends EndHand commands to Table domain.
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
#include "examples/hand.pb.h"
#include "examples/table.pb.h"

namespace {

constexpr int DEFAULT_PORT = 50412;

/**
 * Saga: Hand -> Table (OO Pattern)
 *
 * Reacts to HandComplete events from Hand domain.
 * Sends EndHand commands to Table domain.
 */
class HandTableSaga : public angzarr::Saga {
   public:
    ANGZARR_SAGA("saga-hand-table", "hand", "table")

    ANGZARR_REACTS_TO(HandComplete)
    (const examples::HandComplete& event) {
        examples::EndHand end_hand;
        // hand_root comes from the source cover, but we also have table_root in the event
        end_hand.set_hand_root(event.table_root());  // The hand's own root

        for (const auto& winner : event.winners()) {
            auto* result = end_hand.add_results();
            result->set_winner_root(winner.player_root());
            result->set_amount(winner.amount());
            result->set_pot_type(winner.pot_type());
            *result->mutable_winning_hand() = winner.winning_hand();
        }

        return end_hand;
    }
};

/**
 * gRPC service for Hand->Table saga using OO pattern.
 */
class HandTableSagaService final : public angzarr::SagaService::Service {
   public:
    grpc::Status Handle(grpc::ServerContext* context, const angzarr::SagaHandleRequest* request,
                        angzarr::SagaResponse* response) override {
        (void)context;
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
    HandTableSaga saga_;
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

    HandTableSagaService service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Hand->Table saga (OO) listening on " << server_address << std::endl;

    server->Wait();
    return 0;
}
