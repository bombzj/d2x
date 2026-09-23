#pragma once
#include <nlohmann/json_fwd.hpp>

namespace d2x {
class GameSession;
class SceneView;
void debugItemMove(const nlohmann::json &request, nlohmann::json &result,
                   GameSession &session, SceneView &view);
void debugItemInspect(const nlohmann::json &request, nlohmann::json &result,
                      const GameSession &session);
} // namespace d2x
