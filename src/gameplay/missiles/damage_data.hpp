#pragma once
#include "damage.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <cstdint>
#include <array>
#include <optional>
#include <vector>
#include <utility>
#include "gameplay/monsters/types.hpp"

namespace d2x {
struct SkillFormulaCatalog;
// D2Common MISSILE_CalculateDamageData stores ranges, not rolled hits.
// Values are already in native ItemStatCost units; no actor/world ownership.
enum class MissileDamageElement : uint8_t { None,Fire,Lightning,Magic,Cold,Poison,LifeDrain,ManaDrain,StaminaDrain,Stun,Random,Burn,Freeze };
struct MissileDamageData {
    uint32_t flags{};
    MissileDamageRange physical,fire,lightning,magic,cold,poison,lifeDrain,manaDrain,staminaDrain,burn;
    int32_t fireLength{},coldLength{},poisonLength{},poisonCount{},stunLength{},burnLength{};
    int32_t demonPercent{},undeadPercent{},targetArmor{},damagePercent{};
};
struct MissileDamageWeapon {
    bool twoHanded{},throwable{},dualWield{},blunt{};
    int16_t strengthBonus{},dexterityBonus{};
    int32_t masteryDamage{};
};
struct MissileDamageInput {
    MissileDamageRange physical,elemental;
    MissileDamageElement element{};
    int32_t length{},unitSourceDamage{},originSourceDamage{};
    uint8_t holy{};
    bool halfTwoHanded{},hireling{},bonusStats{};
    std::optional<MissileDamageWeapon> weapon;
    // These are native queried stat views at their explicit source boundary.
    const StatSnapshot &owner,&missile;
    const StatSnapshot *origin{};
};
struct MissileDamageMastery {int32_t fire{},lightning{},magic{},cold{},poison{};};
struct MissileDamageRollState {
    MissileDamageData data;MissileDamageMastery mastery;uint64_t random{};
    std::vector<std::pair<uint16_t,int32_t>> monsterTypeBonuses;
    std::shared_ptr<const MonsterTypeCatalog> monsterTypes;
    int lifeStealOverlay{-1},manaStealOverlay{-1};
    std::array<int,3> lifeStealDivisor{1,1,1},manaStealDivisor{1,1,1};
    std::shared_ptr<const StatRuleCatalog> statRules;
    std::shared_ptr<const SkillFormulaCatalog> formulas;
    int definition{},skill{},level{},programOverlay{-1};
    uint16_t serverDamageFunction{};
    MissileDamageElement element{};
    std::array<int32_t,2> damageParameters{};
    std::optional<int32_t> targetParameterX;
    bool getHit{},softHit{};
    uint8_t knockbackChance{};
    int uninterruptableState{-1};
};
struct MissileDamageTarget {bool present{},demon{},undead{};int32_t monsterTypePercent{};};
struct MissileHitDamage {
    // Physical, magic, fire, lightning, cold, poison in native fixed units.
    std::array<int32_t,6> channels{};
    int32_t coldLength{},freezeLength{},poisonLength{},stunLength{},burn{},burnLength{};
    int32_t lifeLeech{},manaLeech{},staminaLeech{};
    bool critical{},bypassUndead{},bypassDemon{},bypassBeasts{};
    bool softHit{},knockback{},disableTreasure{};
    uint8_t hitClass{};
};
struct MissileDamageProgramInput {
    uint16_t function{};
    MissileDamageElement element{};
    std::array<int32_t,2> parameters{};
    int32_t percentage{},level{},skillParameter1{},skillParameter2{},ownerDamagePercent{};
    bool skillExists{},ownerHireling{},targetMonster{},targetPlayer{},targetSmall{},targetLarge{},targetDimensionsKnown{};
    std::optional<int32_t> remainingPoison;
    int32_t maximumPoisonLength{};
};
// Native SrvDmg executes after FullDamageParams and before SetDamageFlags.
bool applyMissileDamageProgram(MissileHitDamage &,const MissileDamageProgramInput &,
    const MissileDamageData &,const MissileDamageMastery &,MissileDamageTarget,uint64_t &random);
struct MissileElementDamage {MissileHitDamage hit;int32_t value{};};
MissileHitDamage rollMissileDamageData(const MissileDamageData &,const MissileDamageMastery &,MissileDamageTarget,uint64_t &random);
MissileElementDamage rollMissileElementDamage(const MissileDamageData &,MissileDamageElement,const MissileDamageMastery &,
    MissileDamageTarget,uint64_t &random);
// Retail 1.13c D2Common RVA 6B180: one unsigned draw for critical even
// when zero, then nonzero deadly, then nonzero equipped-weapon mastery.
bool missileHasBonusStats(int32_t critical,int32_t deadly,bool weapon,int32_t mastery,uint64_t &random);
void addMissileDamageStats(MissileDamageData &,const StatSnapshot &,const StatRuleCatalog &,uint8_t shift);
void scaleMissileDamageElements(MissileDamageData &,int32_t sourceDamage);
MissileDamageData calculateMissileDamageData(const MissileDamageInput &,const StatRuleCatalog &);
// Preserve SetDamageStats' ordered writes, including the later FIRELENGTH
// burn write and up to 128 parameter layers copied from the actual owner.
std::vector<StatValue> missileDamageStatWrites(const MissileDamageData &,int32_t level,const StatSnapshot &owner,const StatRuleCatalog &);
}
