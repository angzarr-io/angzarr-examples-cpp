/**
 * Table -> Player saga using OO pattern.
 *
 * Reacts to HandEnded events from Table domain.
 * Sends ReleaseFunds commands to Player domain for each player.
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

constexpr int DEFAULT_PORT = 50413;

/**
 * Saga: Table -> Player (OO Pattern)
 *
 * Reacts to HandEnded events from Table domain.
 * Sends ReleaseFunds commands to Player domain for each player.
 *
 * Note: Uses explicit handler registration because each HandEnded event
 * may produce multiple commands targeting different player aggregates.
 */
class TablePlayerSaga : public angzarr::Saga {
   public:
    ANGZARR_SAGA("saga-table-player", "table", "player")

    TablePlayerSaga() {
        register_event_handler("HandEnded",
            [](angzarr::Saga* self, const google::protobuf::Any& any,
               const std::string& corr_id) -> std::vector<angzarr::CommandBook> {
                examples::HandEnded event;
                any.UnpackTo(&event);
                return static_cast<TablePlayerSaga*>(self)->handle_HandEnded(event, corr_id);
            });
    }

   protected:
    std::vector<angzarr::CommandBook> handle_HandEnded(const examples::HandEnded& event,
                                                       const std::string& corr_id) {
        std::vector<angzarr::CommandBook> commands;

        for (const auto& [player_hex, stack_change] : event.stack_changes()) {
            (void)stack_change;

            // Convert hex string to bytes
            std::string player_bytes;
            for (size_t i = 0; i < player_hex.length(); i += 2) {
                player_bytes +=
                    static_cast<char>(std::stoi(player_hex.substr(i, 2), nullptr, 16));
            }

            examples::ReleaseFunds release;
            release.set_table_root(event.hand_root());

            angzarr::CommandBook cmd_book;
            auto* cover = cmd_book.mutable_cover();
            cover->set_domain(output_domain());
            cover->mutable_root()->set_value(player_bytes);
            cover->set_correlation_id(corr_id);

            auto* page = cmd_book.add_pages();
            page->mutable_header()->mutable_angzarr_deferred();
            page->mutable_command()->PackFrom(release, "type.googleapis.com/");

            commands.push_back(std::move(cmd_book));
        }

        return commands;
    }
};

/**
 * gRPC service for Table->Player saga using OO pattern.
 */
class TablePlayerSagaService final : public angzarr::SagaService::Service {
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
    TablePlayerSaga saga_;
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

    TablePlayerSagaService service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Table->Player saga (OO) listening on " << server_address << std::endl;

    server->Wait();
    return 0;
}
