#pragma once
#include "summon_spec.hpp"
#include "world/collision.hpp"
#include <array>
#include <memory>
#include <string>

namespace d2x {
class SkillFormulaValues;
struct DruidPetDefinition {
    enum class Ai { Raven,SpiritWolf,DireWolf,Spirit,PoisonVine,LifeVine,ManaVine,Bear } ai{};
    std::array<std::array<int,3>,8> parameters{};
    std::array<int,8> skills{-1,-1,-1,-1,-1,-1,-1,-1};
    std::array<std::string,8> skillModes;
    std::array<std::pair<int,int>,3> lifeRanges;
    std::string summonMode;
    int nativeClass{-1},group{},warpProbeClass{-1},baseMaximum{};
    MovementCollisionRule warpProbeCollision;
    bool attackable{},vine{};
};
struct DruidPetSpec {
    std::shared_ptr<const DruidPetDefinition> definition;
    int skill{-1},rank{};
    std::optional<uint8_t> initialCircle;
};
SummonCastSpec resolveDruidSummon(const SummonSkillSpec &,int rank,int difficulty,SkillFormulaValues &,int skill);
}
