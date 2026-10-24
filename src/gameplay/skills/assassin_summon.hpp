#pragma once
#include "summon_spec.hpp"
#include "assassin_spec.hpp"
#include "world/collision.hpp"
#include <array>
#include <memory>
#include <string>
#include <utility>

namespace d2x {
class SkillFormulaValues;
struct AssassinPetDefinition {
    enum class Ai { Sentry, BladeCreeper, DeathSentry, ShadowWarrior, ShadowMaster } ai{};
    std::array<std::array<int,3>,8> parameters{};
    std::array<int,8> skills{};
    std::array<int,8> skillModes{};
    std::string summonMode;
    int nativeClass{-1};
    int infernoFrames{},infernoAnimation{};
    bool attackable{},warp{};
    // PD2 also assigns ShadowMaster AI to Amazon pets. AI identity does
    // not imply the Assassin disguise or its owner-class skill inheritance.
    bool inheritOwnerSkills{true};
    std::string transformClass{"ass"};
    int summonFunction{49};
    std::array<std::pair<int,int>,3> lifeRanges{};
    int warpProbeClass{-1};
    MovementCollisionRule warpProbeCollision;
};
struct AssassinPetSpec {
    std::shared_ptr<const AssassinPetDefinition> definition;
    int skill{-1},rank{},itemLevel{},lifeFrames{};
};
SummonCastSpec resolveAssassinSummon(const SummonSkillSpec &,int rank,int ownerLevel,int difficulty,
    SkillFormulaValues &,int skill,bool pd2=false);
}
