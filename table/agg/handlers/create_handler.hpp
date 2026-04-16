#pragma once

#include "examples/table.pb.h"
#include "table_state.hpp"

namespace table {
namespace handlers {

/// Handle CreateTable command.
examples::TableCreated handle_create_table(const examples::CreateTable& cmd, const TableState& state);

}  // namespace handlers
}  // namespace table
