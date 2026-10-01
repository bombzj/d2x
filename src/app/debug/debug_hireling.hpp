#pragma once
#include "debug_commands.hpp"
#include <nlohmann/json_fwd.hpp>

namespace d2x {
void debugHireling(const std::string &command, const nlohmann::json &request,
                   nlohmann::json &result, GameSession &session, SceneView &view);
}
