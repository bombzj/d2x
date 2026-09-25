#pragma once
#include "presentation/scene_view.hpp"
#include <functional>

namespace d2x {
std::string debugCommand(const std::string &request, GameSession &session, SceneView &view,
                         bool &paused, bool &quit, const std::string &savePath,
                         const std::function<void(const std::string &)> &screenshot,
                         const std::function<void(FrameInput)> &input);
} // namespace d2x
