#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace d2x {
// Local debug syntax only. A recognized malformed command throws rather than
// falling through to room chat or Battle.net.
std::optional<nlohmann::json> parseDebugChatCommand(std::string_view);
std::vector<std::string> debugChatSummary(const nlohmann::json &);
}
