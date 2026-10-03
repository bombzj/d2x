#pragma once
#include "core/id.hpp"
#include "gameplay/character/allocation.hpp"
#include <span>

namespace d2x {
// Authority binds these references to one actor; this does not own another record.
struct CharacterProgressionContext {
    EntityId actor;
    bool alive;
    uint64_t &experience;
    int &level, &unspentAttributes, &unspentSkills;
    AttributeAllocation &allocated;
};
bool grantCharacterExperience(CharacterProgressionContext context, uint64_t amount,
                              std::span<const uint64_t> thresholds, int statPerLevel);
bool allocateCharacterAttribute(CharacterProgressionContext context, Attribute attribute);
bool resetCharacterAttributes(CharacterProgressionContext context);
} // namespace d2x
