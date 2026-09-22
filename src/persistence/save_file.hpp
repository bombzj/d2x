#pragma once
#include "gameplay/session_snapshot.hpp"
#include <filesystem>

namespace d2x {
SessionSnapshot loadSave(const std::filesystem::path &path);
void writeSave(const std::filesystem::path &path, SessionSnapshot snapshot);
} // namespace d2x
