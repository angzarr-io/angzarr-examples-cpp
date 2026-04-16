#include "hand.hpp"

#include "action_handler.hpp"
#include "award_pot_handler.hpp"
#include "deal_community_handler.hpp"
#include "deal_handler.hpp"
#include "post_blind_handler.hpp"

namespace hand {

examples::CardsDealt Hand::handle_deal_cards(const examples::DealCards& cmd) {
    return handlers::handle_deal_cards(cmd, state_);
}

examples::BlindPosted Hand::handle_post_blind(const examples::PostBlind& cmd) {
    return handlers::handle_post_blind(cmd, state_);
}

examples::ActionTaken Hand::handle_player_action(const examples::PlayerAction& cmd) {
    return handlers::handle_player_action(cmd, state_);
}

examples::CommunityCardsDealt Hand::handle_deal_community_cards(const examples::DealCommunityCards& cmd) {
    return handlers::handle_deal_community_cards(cmd, state_);
}

std::pair<examples::PotAwarded, examples::HandComplete> Hand::handle_award_pot(
    const examples::AwardPot& cmd) {
    return handlers::handle_award_pot(cmd, state_);
}

}  // namespace hand
