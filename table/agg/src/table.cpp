#include "table.hpp"

#include "create_handler.hpp"
#include "end_hand_handler.hpp"
#include "join_handler.hpp"
#include "leave_handler.hpp"
#include "start_hand_handler.hpp"

namespace table {

examples::TableCreated Table::handle_create_table(const examples::CreateTable& cmd) {
    return handlers::handle_create_table(cmd, state_);
}

examples::PlayerJoined Table::handle_join_table(const examples::JoinTable& cmd) {
    return handlers::handle_join_table(cmd, state_);
}

examples::PlayerLeft Table::handle_leave_table(const examples::LeaveTable& cmd) {
    return handlers::handle_leave_table(cmd, state_);
}

examples::HandStarted Table::handle_start_hand(const examples::StartHand& cmd) {
    return handlers::handle_start_hand(cmd, state_);
}

examples::HandEnded Table::handle_end_hand(const examples::EndHand& cmd) {
    return handlers::handle_end_hand(cmd, state_);
}

}  // namespace table
