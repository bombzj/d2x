#pragma once
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace d2x {
class GameSession;
class SceneView;
void debugMonsterCommand(const std::string &command, const nlohmann::json &request,
                         nlohmann::json &result, GameSession &session, SceneView &view,
                         const std::function<void()> &step);
} // namespace d2x
