#pragma once
#include <array>
#include <cstdint>

namespace d2x {
struct BaalTargetScore {
    int damageResist{},magicResist{},fireResist{},lightResist{},coldResist{};
    int maximumDamage{},fireDamage{},lightDamage{},magicDamage{},coldDamage{},poisonDamage{};
    int leftRank{},leftAttackRank{},rightRank{},rightAttackRank{};
    int lifePercentage{},threat{14};
    bool melee{},clearRay{},cold{},returnHome{};
};
int baalTargetScore(const BaalTargetScore &);
struct BaalWeights {
    int targets{},score{},distance{},lifePercentage{},targetLifePercentage{},difficulty{},clones{};
    bool melee{},blocked{},cold{},specialSkill{},far{},further{},portal{};
};
// AIBAAL_RollRandomAiParam and its non-colliding branch. No-target and
// retained-action branches belong to the caller and consume their own draw.
std::array<int,16> baalWeights(const BaalWeights &);
int baalWeightedAction(const std::array<int,16> &,uint64_t &random);
}
