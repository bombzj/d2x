#pragma once
#include "gameplay/effects/definition.hpp"
#include <array>
#include <string>

namespace d2x {
// Prepared native entry identities and links. These are not DLL addresses.
struct Pd2SkillProgram {
    int startFunction{},doFunction{};
    CombatStateDefinition state;
    std::array<int,3> missiles{-1,-1,-1};
    int missile{-1};
    int summonClass{-1};
    std::string mode;
    int sequence{-1};
    std::string requiredWeaponType;
    std::array<std::string,3> weaponTypes;
};
}
