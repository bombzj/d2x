#pragma once
#include "gameplay/effects/definition.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace d2x {
// Native procedure roles. All variable parameters remain in the MPQ catalog.
enum class DruidProgram : uint8_t {
    Raven,Vine,Shape,Firestorm,Summon,Boulder,Arctic,Rage,Fissure,Defensive,
    Rabies,FireClaws,Wind,Hunger,ShockWave,Volcano,Fury,Periodic,BearSmite
};
struct DruidSkillSpec {
    DruidProgram program{};
    uint16_t startFunction{},doFunction{};
    std::string mode;
    int sequence{-1},restriction{},activeFunction{};
    CombatStateDefinition state,targetState;
    std::array<int,3> requiredStates{-1,-1,-1};
    std::array<int,3> missiles{-1,-1,-1};
    int transformClass{-1};
    std::vector<int> removalGroupStates;
    bool useAttackRate{},useManaOnDo{};
};
}
