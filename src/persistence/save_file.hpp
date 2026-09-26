#pragma once
#include "gameplay/session/character_save.hpp"
#include "content/classic_data.hpp"
#include <filesystem>
#include <span>

namespace d2x {
void writeFileAtomically(const std::filesystem::path &path, std::span<const uint8_t> bytes,
                         bool keepBackup = false);
CharacterSaveData loadSave(const std::filesystem::path &path, const ClassicData &content);
void writeSave(const std::filesystem::path &path, const CharacterSaveData &snapshot, const ClassicData &content);
} // namespace d2x
