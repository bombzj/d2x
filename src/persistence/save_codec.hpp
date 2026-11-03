#pragma once
#include "core/bytes.hpp"
#include "persistence/character_save.hpp"
#include "content/classic_data.hpp"
#include <span>
#include <optional>

namespace d2x {
inline constexpr size_t maxSaveBytes = 16 * 1024 * 1024;
Bytes encodeSave(const CharacterSaveData &snapshot, const ClassicData &content);
// Without a game difficulty, project the character's last played difficulty for the roster.
CharacterSaveData decodeSave(std::span<const uint8_t> bytes, const ClassicData &content, std::optional<int> gameDifficulty = {});
} // namespace d2x
