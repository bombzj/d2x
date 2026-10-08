#pragma once
#include "core/id.hpp"
#include "gameplay/character/intents.hpp"
#include "gameplay/character/display.hpp"
#include "gameplay/character/skill_choices.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <set>

namespace d2x {
struct CharacterSkillView {
    int id = -1, page = 0, row = 0, column = 0;
    int listRow = -1, listPool = 0, iconCell = -1;
    std::string classCode, name;
    int baseRank = 0, effectiveRank = 0, maximumRank = 0, nextRequiredLevel = 0;
    bool baseRankKnown = false, effectiveRankKnown = false;
    bool passive = false, leftAllowed = false, available = false, canAllocate = false, usableNow = false;
    bool pickerEnabled = false; // Selection icon state; independent of town casting permission.
    CharacterActionDisplay action;
    std::vector<std::string> treeTooltip, pickerTooltip;
    std::string treeBonusHeading;
    std::vector<std::string> treeBonusTooltip;
};
// Only the bound player's necessary UI data. No character record, equipment,
// execution spec, task book, inventory or authority pointer is sent to the UI.
struct CharacterView {
    uint64_t revision = 0;
    EntityId actor;
    std::string name, classCode, className;
    int level = 1, unspentAttributes = 0, unspentSkills = 0;
    uint64_t experience = 0, currentLevelExperience = 0, maximumExperience = 0;
    std::optional<uint64_t> nextLevelExperience;
    bool nextLevelKnown = false;
    std::array<int, 4> attributes{}, resistances{}; // STR/DEX/VIT/ENE; fire/cold/lightning/poison.
    float hp = 0, mana = 0, stamina = 0;
    int maxLife = 1, maxMana = 1, maxStamina = 1, defense = 0, blockChance = 0;
    int physicalResist = 0, magicResist = 0, flatPhysicalReduction = 0, flatMagicReduction = 0;
    int poisonLengthResist = 0, fireAbsorbPercent = 0;
    std::optional<int> fasterCast; // Known native self stat; never inferred for other players.
    bool dead = false, running = false, blueStamina = false, attackUsable = false, hasSkillTree = false;
    unsigned weaponSet = 0;
    std::array<int, 4> selectedSkills{-1, -1, -1, -1};
    std::array<SkillHotkey, 8> skillHotkeys{};
    std::array<std::string, 3> pageNames;
    std::array<std::vector<std::optional<int>>, 2> choices;
    CharacterActionDisplay attack;
    std::map<int, CharacterSkillView> skills;
    std::set<std::string, std::less<>> unknownStats;
    std::string number(std::string_view stat, int64_t value) const {
        return unknownStats.contains(stat) ? "?" : std::to_string(value);
    }
    const CharacterSkillView *skill(int id) const {
        const auto found = skills.find(id);
        return found == skills.end() ? nullptr : &found->second;
    }
    CharacterActionDisplay actionDisplay(std::optional<int> id) const {
        if (!id) return attack;
        const auto *entry = skill(*id);
        return entry ? entry->action : CharacterActionDisplay{};
    }
};
} // namespace d2x
