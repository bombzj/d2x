#pragma once
#include "persistence/character_save.hpp"
#include "resources/atomic_file.hpp"
#include "content/classic_data.hpp"
#include <filesystem>
#include <span>

namespace d2x {
CharacterSaveData loadSave(const std::filesystem::path &path, const ClassicData &content);
void writeSave(const std::filesystem::path &path, const CharacterSaveData &snapshot, const ClassicData &content);
} // namespace d2x
