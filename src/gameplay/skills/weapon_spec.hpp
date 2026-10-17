#pragma once
#include "gameplay/effects/definition.hpp"
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace d2x {
struct BowSkillSpec;
struct SpearSkillSpec;
// Weapon skills use the ordinary attack animation, equipment and ammunition pipeline.
struct WeaponSkillSpec {
    std::shared_ptr<const BowSkillSpec> bow;
    std::shared_ptr<const SpearSkillSpec> spear;
    bool noAmmo = false;
    bool commonAttack = false, leftHand = false;
    std::string requiredType;
    bool thrown = false, manaOnRelease = false;
    bool useThrowMastery = false; // Native ITypeA[0] thrown ancestry and Range==2.
    bool dualWield = false;
    int sequence = -1;
    int attackRating = 0, attackRatingPerLevel = 0, delayFrames = 0;
    int damagePercent = 0, damagePerLevel = 0, selfDamagePercent = 0;
    std::map<int, int> damageSynergies;
    std::map<int, int> attackRatingSynergies;
    int damageStartLevel = 1, attacks = 1, attackLimit = 1, rollbackPercent = 0;
    bool interruptible = true;
    std::array<int, 3> elementPercent{};
    int elementPerLevel = 0;
    std::array<std::map<int, int>, 3> elementSynergies;
    bool smite = false;
    std::string mode;
    int stunFrames = 0, stunPerLevel = 0;
    std::optional<std::array<int,3>> stunCurve;
    std::map<int,int> stunSynergies;
    int physicalFlat = 0;
    int physicalFlatPerLevel = 0;
    bool knockback = false;
    bool clearTargetStun = false;
    int conversionMinimum = 0, conversionMaximum = 0, conversionChance = 0, conversionFrames = 0;
    CombatStateDefinition conversionState;
    int chargeVelocity = 0;
    int magicConversionPercent = 0;
    std::map<int,int> magicConversionSynergies;
    CombatStateDefinition attackState;
    int attackDefense = 0, attackDefensePerLevel = 0;
    bool frenzy = false;
    CombatStateDefinition frenzyState;
    std::array<int,4> frenzyParameters{};
    int frenzyFrames = 0;
    bool whirlwind = false;
    CombatStateDefinition movingState;
    int attackDurationMinimum = 0, attackDurationMaximum = 0, attackDuration = 0;
    int armorOverridePercent = 0, attackPhysicalResist = 0;
    CombatStateDefinition conversionSaveState;
    int conversionFramesPerLevel{};
    int startFunction{},doFunction{};
    bool allocatesMeleeAtStart() const {
        return (doFunction==2 && (startFunction==2 || startFunction==6 || startFunction==7 || startFunction==32 || startFunction==35 || startFunction==39)) ||
            (startFunction==29 && doFunction==64) || (startFunction==16 && doFunction==32) || (startFunction==32 && doFunction==79) ||
            (startFunction==6 && doFunction==11) || (startFunction==10 && doFunction==14) || (startFunction==42 && doFunction==83);
    }
    bool nativeMelee() const {return commonAttack || allocatesMeleeAtStart() || doFunction==7 || doFunction==9 || doFunction==13 ||
        doFunction==67 || doFunction==70 || doFunction==76 || doFunction==78 || doFunction==109 || doFunction==150;}
};
} // namespace d2x
