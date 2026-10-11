#pragma once
#include "contracts/character.hpp"
#include "gameplay/items/skill_sources.hpp"
#include "content/skills/skill_eligibility.hpp"
#include "content/character/combat_display.hpp"
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
    std::map<int, int> itemSkillQuantities;
    std::vector<ChargedSkill> chargedSkills;
    bool baseRanksAssigned = false;
    std::array<bool, 2> throwReady{};
    std::array<std::optional<SkillHandEquipment>, 2> handEquipment;
    struct ResourceStates { bool bloodMana = false, inferno = false; };
    std::optional<ResourceStates> resourceStates;
    std::optional<int> chargedSkill; // This recursion projects a native item owner, not a learned cast.
    std::optional<int> fireMastery, lightningMastery, coldDamagePercent;
    std::optional<unsigned> difficulty;
    std::optional<KnownAttackTiming> attackTiming;
    std::optional<int> missilePierceChance;
    std::optional<CombatDisplayEquipment> combatEquipment;
};
CharacterView projectCharacterDisplay(const ClassicData &, const CharacterProjectionInput &);
} // namespace d2x
