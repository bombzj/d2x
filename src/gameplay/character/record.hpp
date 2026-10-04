#pragma once
#include "core/id.hpp"
#include "gameplay/character/allocation.hpp"
#include "gameplay/character/skill_choices.hpp"
#include "gameplay/quest/state.hpp"
#include "gameplay/quest/prelude.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace d2x {
struct HirelingRecord {
    int sourceRow = -1, classId = -1;
    std::string nameKey;
    int level = 0;
    // Native D2S only stores death status. Decode supplies the same base-life
    // marker as before; the session then derives equipped maximum life.
    float hp = 0;
    uint64_t experience = 0;
    uint32_t seed = 0;
};

// Character-owned values transferred to/from persistence. No actions, effects,
// positions, derived equipment stats or runtime random streams live here.
struct CharacterRecord {
    EntityId id; // Snapshot owner binding; regenerated on import, not a D2S field.
    std::string name = "Hero", characterClass = "Barbarian";
    std::string nativeSaveSections;
    float hp = 0, mana = 0, stamina = 0;
    unsigned weaponSet = 0, gold = 0, bankGold = 0;
    std::array<std::set<std::string>, 3> npcIntroductions;
    QuestPreludeBook questPreludes{};
    uint64_t experience = 0;
    int level = 1;
    AttributeAllocation allocated;
    int unspentAttributes = 0;
    std::map<int, int> skillRanks;
    int unspentSkills = 0;
    std::array<SkillHotkey, 8> skillHotkeys{};
    std::array<int, 4> selectedSkills{-1, -1, -1, -1};
    QuestBook quests{};
    // Native A1-A4 act-change records, separate from killing the act boss.
    std::array<std::array<bool, 4>, 3> completedActs{};
    HirelingRecord hireling;
};
} // namespace d2x
