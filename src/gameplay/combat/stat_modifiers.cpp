#include "stat_modifiers.hpp"
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
void add(int &target, int value) {
    const auto sum = int64_t(target) + value;
    if (sum < std::numeric_limits<int>::min() || sum > std::numeric_limits<int>::max())
        throw std::runtime_error("Combat modifier sum exceeds supported range");
    target = int(sum);
}
}
void mergeAttackTargetModifiers(AttackTargetModifiers &a, const AttackTargetModifiers &b) {
    add(a.demonDamage, b.demonDamage); add(a.undeadDamage, b.undeadDamage);
    add(a.demonAttackRating, b.demonAttackRating); add(a.undeadAttackRating, b.undeadAttackRating);
    add(a.defenseReduction, b.defenseReduction);
    a.ignoreDefense |= b.ignoreDefense;
}
void mergeCombatModifiers(CombatModifiers &a, const CombatModifiers &b) {
    mergeAttackTargetModifiers(a.target, b.target);
#define D2X_ADD(field) add(a.field, b.field)
    D2X_ADD(damagePercent); D2X_ADD(attackRatingPercent);
    D2X_ADD(defensePercent);
    D2X_ADD(experiencePercent);
    a.preventPoison = a.preventPoison || b.preventPoison;
    a.preventBurn = a.preventBurn || b.preventBurn;
    D2X_ADD(minimumDamagePercent); D2X_ADD(maximumDamagePercent);
    D2X_ADD(normalDamage); D2X_ADD(minimumDamage); D2X_ADD(maximumDamage);
    D2X_ADD(fireMinimum); D2X_ADD(fireMaximum);
    D2X_ADD(lightningMinimum); D2X_ADD(lightningMaximum);
    D2X_ADD(coldMinimum); D2X_ADD(coldMaximum); D2X_ADD(coldFrames);
    D2X_ADD(coldSkillDamagePercent); D2X_ADD(coldPierce);
    D2X_ADD(magicMinimum); D2X_ADD(magicMaximum);
    D2X_ADD(poisonMinimum); D2X_ADD(poisonMaximum); D2X_ADD(poisonFrames); D2X_ADD(poisonSources);
    D2X_ADD(fireMaxResist); D2X_ADD(lightningMaxResist);
    D2X_ADD(coldMaxResist); D2X_ADD(poisonMaxResist); D2X_ADD(magicMaxResist);
    D2X_ADD(physicalResist); D2X_ADD(magicResist);
    D2X_ADD(flatPhysicalReduction); D2X_ADD(flatMagicReduction);
    D2X_ADD(fireAbsorbPercent); D2X_ADD(lightningAbsorbPercent);
    D2X_ADD(coldAbsorbPercent); D2X_ADD(magicAbsorbPercent);
    D2X_ADD(fireAbsorb); D2X_ADD(lightningAbsorb);
    D2X_ADD(coldAbsorb); D2X_ADD(magicAbsorb);
    D2X_ADD(lifePercent); D2X_ADD(manaPercent); D2X_ADD(blockBonus);
    D2X_ADD(smiteMinimum); D2X_ADD(smiteMaximum);
    D2X_ADD(shieldDefensePercent);
    D2X_ADD(fasterAttack); D2X_ADD(fasterCast);
    D2X_ADD(attackRate);
    D2X_ADD(thornsPercent); D2X_ADD(concentrationChance);
    D2X_ADD(ironMaidenPercent);
    D2X_ADD(lifeTapPercent);
    D2X_ADD(fasterHitRecovery); D2X_ADD(fasterBlock);
    D2X_ADD(lifeLeech); D2X_ADD(manaLeech);
    D2X_ADD(crushingBlow); D2X_ADD(openWounds); D2X_ADD(deadlyStrike);
    D2X_ADD(magicFind); D2X_ADD(goldFind); D2X_ADD(poisonLengthResist);
    D2X_ADD(curseResistance);
    D2X_ADD(reducedPrices);
    D2X_ADD(replenishLife); D2X_ADD(manaRecovery); D2X_ADD(lifeOnKill); D2X_ADD(manaOnKill);
    D2X_ADD(allSkills);
#undef D2X_ADD
    a.cannotBeFrozen |= b.cannotBeFrozen;
    a.halfFreezeDuration |= b.halfFreezeDuration;
    for (auto [id, value] : b.classSkills) add(a.classSkills[id], value);
    for (auto [id, value] : b.singleSkills) add(a.singleSkills[id], value);
    for (auto [id, value] : b.nonClassSkills) add(a.nonClassSkills[id], value);
    for (auto [id, value] : b.tabSkills) add(a.tabSkills[id], value);
    for (const auto &[id, bonus] : b.weapons) {
        auto &target = a.weapons[id];
        mergeAttackTargetModifiers(target.target, bonus.target);
        add(target.attackRating, bonus.attackRating);
        add(target.attackRatingPercent, bonus.attackRatingPercent);
        add(target.fasterAttack, bonus.fasterAttack);
        add(target.minimum, bonus.minimum);
        add(target.maximum, bonus.maximum);
        add(target.normalDamage, bonus.normalDamage);
        add(target.enhancedMinimum, bonus.enhancedMinimum);
        add(target.enhancedMaximum, bonus.enhancedMaximum);
        add(target.fireMinimum, bonus.fireMinimum); add(target.fireMaximum, bonus.fireMaximum);
        add(target.lightningMinimum, bonus.lightningMinimum); add(target.lightningMaximum, bonus.lightningMaximum);
        add(target.coldMinimum, bonus.coldMinimum); add(target.coldMaximum, bonus.coldMaximum);
        add(target.coldFrames, bonus.coldFrames);
        add(target.magicMinimum, bonus.magicMinimum); add(target.magicMaximum, bonus.magicMaximum);
        add(target.poisonMinimum, bonus.poisonMinimum); add(target.poisonMaximum, bonus.poisonMaximum);
        add(target.poisonFrames, bonus.poisonFrames); add(target.poisonSources, bonus.poisonSources);
        add(target.deadlyStrike, bonus.deadlyStrike);
        add(target.lifeLeech, bonus.lifeLeech); add(target.manaLeech, bonus.manaLeech);
        add(target.crushingBlow, bonus.crushingBlow); add(target.openWounds, bonus.openWounds);
    }
    for (const auto &[id, bonus] : b.armorPercent) add(a.armorPercent[id], bonus);
}
} // namespace d2x
