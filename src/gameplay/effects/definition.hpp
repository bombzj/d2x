#pragma once
#include <array>
#include <cstdint>
#include <map>

namespace d2x {
using EffectFrame = uint64_t; // Absolute 25 Hz simulation frame; never wall-clock time.
enum class CurseAi { None, DimVision, Terror, Confuse, Attract, Taunt };
// SKILLS sub_6FD10EC0 checks these MPQ flags for native UNIT_MONSTER.
// This is a posting rule, independent of allegiance, ownership, NoAura,
// attack modes, and whether the unit is presented as an NPC or companion.
struct NativeMonsterStateTarget {
    bool npc{},attackable{};
    constexpr bool acceptsCurseList() const noexcept {return !npc && attackable;}
};
// Explicit poison/freeze cures look up the posted list directly. The NPC
// curable-mask walk first requires the unit's state flag to be enabled.
struct NativeStateCure { int state=-1; bool requiresStateFlag{}; };
// Imported States.txt fields. These describe a state, not a particular skill.
struct CombatStateDefinition {
    int id = -1, group = 0;
    bool removeOnHit = false;
    std::array<bool, 3> stayOnDeath{};
    bool staminaBarBlue = false;
    bool curse = false;
    bool hideOnDeath = false, shatterOnDeath = false, corpseUnselectable = false;
    bool curable = false;
    bool noSend = false;
    bool life = false;
    bool aura = false;
    bool meleeOnly = false;
};
using CombatStateRules=std::map<int,CombatStateDefinition>;
} // namespace d2x
