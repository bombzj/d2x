#pragma once
#include "core/math.hpp"
#include "gameplay/character/record.hpp"

namespace d2x {
struct PlayerState;
struct HirelingState;
CharacterRecord captureCharacterRecord(const PlayerState &player);
HirelingState restoreHirelingRecord(const HirelingRecord &record, Vec position);
PlayerState restoreCharacterRecord(CharacterRecord record, Vec position);
} // namespace d2x
