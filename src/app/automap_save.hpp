#pragma once
#include <filesystem>

namespace d2x {
class GameSession;
class SceneView;
// Character and client discovery are separate files. Each replacement keeps a backup.
void saveLocalGame(const std::filesystem::path &path, const GameSession &session, const SceneView &view);
bool restoreLocalAutomap(const std::filesystem::path &path, const GameSession &session, SceneView &view);
} // namespace d2x
