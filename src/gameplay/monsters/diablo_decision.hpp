#pragma once
#include <array>
#include <cstdint>

namespace d2x {
struct DiabloTargetScore {
    int damageResist{},magicResist{},fireResist{},lightResist{},coldResist{};
    int maximumDamage{},fireDamage{},lightDamage{},magicDamage{},coldDamage{},poisonDamage{};
    int leftRank{},leftAttackRank{},rightRank{},rightAttackRank{};
    int lifePercentage{},threat{14};
    bool melee{},clearRay{},cold{},returnHome{};
};
// Retail 1.13c Diablo score A3E80. Stat inputs
// are native values, before any damage receiver's resistance caps.
int diabloTargetScore(const DiabloTargetScore &in);
struct DiabloWeights {
    int targets{},score{},distance{},fireResist{},lightResist{},lifePercentage{},playerDifficulty{};
    bool melee{},blocked{},cold{},portal{},specialSkill{},far{},further{};
};
// Retail 1.13c A8E80. The caller performs the two ordered, stateful prison
// probes after these weights and before the single weighted selection draw.
std::array<int,17> diabloWeights(const DiabloWeights &in);
int diabloWeightedAction(const std::array<int,17> &weights,uint64_t &random);
}
