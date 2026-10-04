#pragma once
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
struct CharacterState {
    std::string name = "Hero";
    std::string characterClass = "Barbarian";
    std::string nativeSaveSections;
    unsigned weaponSet = 0;
    unsigned gold = 0, bankGold = 0;
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
    std::array<std::array<bool, 4>, 3> completedActs{};
};
} // namespace d2x
