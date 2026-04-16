// CloudEvents projector - publishes player events as CloudEvents.
//
// This projector transforms internal domain events into CloudEvents 1.0 format
// for external consumption via HTTP webhooks or Kafka.
//
// Uses the OO CloudEventsProjector base class pattern.

#include <optional>
#include <string>

#include "angzarr/angzarr.hpp"
#include "angzarr/cloudevents.pb.h"
#include "examples/player.pb.h"

using namespace angzarr;
using namespace examples;

// docs:start:cloudevents_oo
class PlayerCloudEventsProjector : public CloudEventsProjector {
   public:
    PlayerCloudEventsProjector() : CloudEventsProjector("prj-player-cloudevents", "player") {}

    std::optional<CloudEvent> on_player_registered(const PlayerRegistered& event) {
        // Filter sensitive fields, return public version
        PublicPlayerRegistered public_event;
        public_event.set_display_name(event.display_name());
        public_event.set_player_type(event.player_type());

        CloudEvent ce;
        ce.set_type("com.poker.player.registered");
        ce.mutable_data()->PackFrom(public_event);
        return ce;
    }

    std::optional<CloudEvent> on_funds_deposited(const FundsDeposited& event) {
        PublicFundsDeposited public_event;
        *public_event.mutable_amount() = event.amount();

        CloudEvent ce;
        ce.set_type("com.poker.player.deposited");
        ce.mutable_data()->PackFrom(public_event);
        (*ce.mutable_extensions())["priority"] = "normal";
        return ce;
    }
};
// docs:end:cloudevents_oo

int main() {
    PlayerCloudEventsProjector projector;
    run_cloudevents_projector("prj-player-cloudevents", 50092, projector);
    return 0;
}
