#pragma once
#include "presentation/scene_view.hpp"
#include <functional>

namespace d2x {
std::string debugCommand(const std::string &request, GameSession &session, SceneView &view,
                         bool &paused, bool &quit, const std::string &savePath,
                         const std::function<void(const std::string &)> &screenshot);
} // namespace d2x