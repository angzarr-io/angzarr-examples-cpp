/**
 * Hand -> Player saga using OO pattern.
 *
 * Reacts to PotAwarded events from Hand domain.
 * Sends DepositFunds commands to Player domain for each winner.
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
#include "examples/player.pb.h"

namespace {

constexpr int DEFAULT_PORT = 50414;

/**
 * Saga: Hand -> Player (OO Pattern)
 *
 * Reacts to PotAwarded events from Hand domain.
 * Sends DepositFunds commands to Player domain for each winner.
 *
 * Note: Uses explicit handler registration because each PotAwarded event
 * may produce multiple commands targeting different player aggregates.
 */
class HandPlayerSaga : public angzarr::Saga {
   public:
    ANGZARR_SAGA("saga-hand-player", "hand", "player")

    HandPlayerSaga() {
        register_event_handler("PotAwarded",
            [](angzarr::Saga* self, const google::protobuf::Any& any,
               const std::string& corr_id) -> std::vector<angzarr::CommandBook> {
                examples::PotAwarded event;
                any.UnpackTo(&event);
                return static_cast<HandPlayerSaga*>(self)->handle_PotAwarded(event, corr_id);
            });
    }

   protected:
    std::vector<angzarr::CommandBook> handle_PotAwarded(const examples::PotAwarded& event,
                                                        const std::string& corr_id) {
        std::vector<angzarr::CommandBook> commands;

        for (const auto& winner : event.winners()) {
            examples::DepositFunds deposit;
            deposit.mutable_amount()->set_amount(winner.amount());

            angzarr::CommandBook cmd_book;
            auto* cover = cmd_book.mutable_cover();
            cover->set_domain(output_domain());
            cover->mutable_root()->set_value(winner.player_root());
            cover->set_correlation_id(corr_id);

            auto* page = cmd_book.add_pages();
            page->mutable_header()->mutable_angzarr_deferred();
            page->mutable_command()->PackFrom(deposit, "type.googleapis.com/");

            commands.push_back(std::move(cmd_book));
        }

        return commands;
    }
};

/**
 * gRPC service for Hand->Player saga using OO pattern.
 */
class HandPlayerSagaService final : public angzarr::SagaService::Service {
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
    HandPlayerSaga saga_;
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

    HandPlayerSagaService service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Hand->Player saga (OO) listening on " << server_address << std::endl;

    server->Wait();
    return 0;
}
