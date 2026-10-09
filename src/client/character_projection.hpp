#pragma once
#include "contracts/character.hpp"
#include "gameplay/items/skill_sources.hpp"
#include <map>
#include <optional>

namespace d2x {
struct ClassicData;
// Already decoded values, borrowed only for one projection. No wire, session,
// inventory authority, device or resource handle enters display calculations.
struct CharacterProjectionInput {
    uint64_t revision{};
    EntityId actor;
    std::string name;
    std::optional<size_t> characterClass;
    std::map<std::string, double, std::less<>> stats;
    std::optional<float> life, mana, stamina;
    bool dead = false, running = false, town = false, aliveAfterDeathSave = false;
    unsigned weaponSet{};
    std::array<int, 4> selectedSkills{-1, -1, -1, -1};
    std::array<SkillHotkey, 8> hotkeys{};
    std::map<int, int> baseRanks, effectiveRanks;
    std::vector<ChargedSkill> chargedSkills;
    bool baseRanksAssigned = false;
    std::array<bool, 2> throwReady{};
    std::optional<int> fireMastery, lightningMastery, coldDamagePercent;
    std::optional<unsigned> difficulty;
    std::optional<KnownAttackTiming> attackTiming;
    std::optional<int> missilePierceChance;
};
CharacterView projectCharacterDisplay(const ClassicData &, const CharacterProjectionInput &);
} // namespace d2x
