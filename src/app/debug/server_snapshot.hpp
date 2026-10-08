#pragma once
#include "server/runtime/diagnostics.hpp"
#include <nlohmann/json_fwd.hpp>
namespace d2x {
nlohmann::json debugServerSnapshot(const server::DiagnosticSnapshot &, uint64_t since, uint64_t commandSince);
}
