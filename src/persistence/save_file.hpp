#pragma once
#include "gameplay/session/session_snapshot.hpp"
#include "content/classic_data.hpp"
#include <filesystem>

namespace d2x {
SessionSnapshot loadSave(const std::filesystem::path &path, const ClassicData &content);
void writeSave(const std::filesystem::path &path, const SessionSnapshot &snapshot, const ClassicData &content);
} // namespace d2x
