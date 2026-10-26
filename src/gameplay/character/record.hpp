#pragma once
#include "core/id.hpp"
#include "core/fixed.hpp"
#include "gameplay/character/allocation.hpp"
#include "gameplay/character/skill_choices.hpp"
#include "gameplay/character/hireling_record.hpp"
#include "gameplay/quest/state.hpp"
#include "gameplay/quest/prelude.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace d2x {
// Character-owned values transferred to/from persistence. No actions, effects,
// positions, derived equipment stats or runtime random streams live here.
struct CharacterRecord {
    EntityId id; // Snapshot owner binding; regenerated on import, not a D2S field.
    std::string name = "Hero", characterClass = "Barbarian";
    std::string nativeSaveSections;
    // Current native PD2 PlayerData[0x381..0x3A2], the 17 WORDs in st.
    // The surrounding, unclassified extension bytes remain opaque metadata.
    std::optional<std::array<uint16_t,17>> pd2Counters;
    std::optional<uint32_t> pd2BossKills; // Native bk DWORD (PlayerData+375).
    bool hardcore{}, hardcoreDead{}; // Native HC/DEAD identity; death survives leaving the game.
    Fixed8 hp, mana, stamina;
    unsigned weaponSet = 0, gold = 0, bankGold = 0;
    std::array<std::set<std::string>, 3> npcIntroductions;
    QuestPreludeBook questPreludes{};
    uint64_t experience = 0;
    int level = 1;
    AttributeAllocation allocated;
    int unspentAttributes = 0;
    std::map<int, int> skillRanks;
    int unspentSkills = 0;
    std::array<SkillHotkey, 16> skillHotkeys{}; // Native D2S/0x7B slots; HUD uses the first eight.
    std::array<int, 4> selectedSkills{-1, -1, -1, -1};
    QuestBook quests{};
    // Native UPDATEQUESTLOG, by difficulty and filter slot; not reward authority.
    std::array<std::array<bool, nativeQuestStatusCount>, 3> questLogViewed{};
    std::array<bool, 3> cowKingKilled{}; // Native A1Q4 CUSTOM6, per difficulty.
    // Native A1-A4 act-change records, separate from killing the act boss.
    std::array<std::array<bool, 4>, 3> completedActs{};
    HirelingRecord hireling;
    // Original owner GUIDs are runtime identities; native D2S encodes the
    // source as a one-based inventory index alongside each skill ID.
    std::array<uint32_t,4> selectedSkillOwners{UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
};
} // namespace d2x
