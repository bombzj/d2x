#pragma once
#include "persistence/character_save.hpp"
#include "persistence/pd2_split_save.hpp"
#include "resources/atomic_file.hpp"
#include "content/classic_data.hpp"
#include <filesystem>
#include <span>

namespace d2x {
CharacterSaveData loadSave(const std::filesystem::path &path, const ClassicData &content,
    CharacterSaveFiles *source = nullptr);
void writeSave(const std::filesystem::path &path, const CharacterSaveData &snapshot, const ClassicData &content);
// The caller holds the local character lease when recovering or committing.
CharacterSaveFiles readSaveFiles(const std::filesystem::path &, const ClassicData &, bool recover = false);
void writeSaveFiles(const std::filesystem::path &, const CharacterSaveFiles &before, const CharacterSaveFiles &after);
std::filesystem::path characterLeasePath(const std::filesystem::path &);
} // namespace d2x
