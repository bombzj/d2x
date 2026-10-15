#pragma once
#include "gameplay/effects/definition.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace d2x {
// Roles, not Skills IDs. Parameters/formulas remain in the native catalog.
enum class AssassinProgram : uint8_t {
    FireBlast, PsychicHammer, Charge, ElementalCharge, DragonTalon, ShockWeb,
    BladeSentinel, DragonClaw, Sentry, Cloak, BladeFury, Shadow, DragonTail,
    MindBlast, DragonFlight, BladeShield, Defensive
};
struct AssassinSkillSpec {
    AssassinProgram program{};
    uint16_t startFunction{},doFunction{};
    std::string mode,summon;
    int sequence{-1},petType{-1},summonClass{-1};
    CombatStateDefinition state,targetState;
    CombatStateDefinition conversionState,conversionSaveState;
    std::array<int,3> missiles{-1,-1,-1};
    std::array<uint16_t,3> progressiveFunctions{};
    int progressiveDamage{},chargeStat{-1},chargeHitStat{-1},targetOverlay{-1};
    bool progressiveStack{},useAttackRate{},useManaOnDo{};
};
}
