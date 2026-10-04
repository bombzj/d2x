#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/combat/damage_type.hpp"
#include "gameplay/effects/definition.hpp"
#include "gameplay/skills/events.hpp"
#include <vector>

namespace d2x {
class ISkillWorld;
class ISkillWeaponWorld;
struct SkillCaster;
struct SkillAuraOwner;
struct WeaponSkillCaster;
struct SkillProjectileSource;
struct SkillCastSpec;
struct CombatUnit;
struct DamageRequest;
struct Missile;
struct WeaponDamage;
struct AuraDefinition;
struct FirewallSpec;
struct NativeSkillCast;
enum class SkillBehavior;
enum class CombatEffectEvent;
enum class Relation;
// Stateless handlers; all persistent values remain in the authority's unit,
// effect and missile owners. A runtime borrows its ports for one call chain.
class SkillRuntime {
    ISkillWorld &world_;
    ISkillWeaponWorld &weapons_;
    CombatUnit combatUnit(EntityId id) const;
    std::vector<CombatUnit> combatUnits() const;
    Vec unitPosition(EntityId id) const;
    bool canAttack(EntityId actor, EntityId target) const;
    Relation relation(EntityId first, EntityId second) const;
    bool active(Vec point) const;
    float dealDamage(const DamageRequest &hit);
    float missileColdDuration(EntityId actor, const CombatUnit &target, int frames) const;
    void spawnFrozenOrbBolt(const Missile &orb, Vec target, bool nova, std::vector<Missile> &spawned);
    void resolveGlacialSpikeImpact(Missile &missile);
    const WeaponDamage *attackWeapon(WeaponSkillCaster actor, bool thrown, bool leftHand) const;
    bool releaseBoneWall(SkillCaster actor, const SkillCastSpec &skill, Vec target);
    bool advanceBoneMissile(Missile &missile, float dt);
    bool releaseCorpseExplosion(SkillCaster actor, const SkillCastSpec &skill, EntityId corpse);
    void releaseCurse(SkillCaster actor, const SkillCastSpec &skill, Vec target, EntityId targetUnit);
    bool validAttractTarget(EntityId actor, EntityId target) const;
    void releaseTelekinesis(SkillCaster actor, const SkillCastSpec &skill, Vec target, EntityId targetUnit);
    void releaseAppliedEffect(SkillCaster actor, const SkillCastSpec &skill, Vec target, EntityId targetUnit);
    void releaseTeleport(SkillCaster actor, const SkillCastSpec &skill, Vec target, EntityId targetUnit);
    void releaseStaticField(SkillCaster actor, const SkillCastSpec &skill, int staticFieldMinimum);
    void launchProjectiles(SkillCaster actor, const SkillCastSpec &skill, Vec target, EntityId targetUnit);
    void pulseAura(CombatUnit source, const AuraDefinition &aura, EffectFrame &nextFrame);
    void emit(SkillEvent event);

  public:
    SkillRuntime(ISkillWorld &world, ISkillWeaponWorld &weapons);
    static void stopChannel(SkillCaster actor);
    void advanceSkillCasting(SkillCaster actor, float dt, bool moving);
    bool beginSkillCast(SkillCaster actor, const SkillCastSpec &skill, Vec target, bool teleportAllowed,
                        int staticFieldMinimum, EntityId enemy = {});
    void releaseSkillCast(SkillCaster actor, const SkillCastSpec &skill, Vec target,
                          int staticFieldMinimum, bool consumeMana = true, EntityId targetUnit = {});
    bool blizzardTargetClear(Vec origin, Vec target) const;
    void launchBlizzard(SkillProjectileSource actor, const SkillCastSpec &skill, Vec target);
    void launchFrozenOrb(SkillProjectileSource actor, const SkillCastSpec &skill, Vec target);
    void launchHydraBolt(SkillProjectileSource actor, const SkillCastSpec &skill, Vec target);
    void launchGlacialSpike(SkillProjectileSource actor, const SkillCastSpec &skill, Vec target);
    void advanceArc(Missile &missile, std::vector<Missile> &spawned);
    void advanceMeteor(Missile &missile, std::vector<Missile> &spawned);
    void advanceHeaven(Missile &missile, float dt, std::vector<Missile> &spawned);
    Missile &launchStraight(Missile prepared);
    void advanceBlizzard(Missile &missile, std::vector<Missile> &spawned);
    void advanceFrozenOrb(Missile &missile, std::vector<Missile> &spawned);
    void advanceGlacialSpike(Missile &missile, std::vector<Missile> &spawned);
    void reactToMissile(const Missile &incoming, EntityId target, std::vector<Missile> &spawned);
    void advanceChillingArmorBolt(Missile &missile, std::vector<Missile> &spawned);
    void advanceFirewall(Missile &missile, std::vector<Missile> &spawned);
    void createBlazeTrail(SkillCaster actor);
    void advanceThunderStorm(SkillCaster actor);
    bool clearAuraIfChanged(SkillAuraOwner owner, int skill, int rank);
    void prepareAura(SkillAuraOwner owner, const AuraDefinition &definition, bool immediate);
    void updateAuras(bool playerOnly = false);
    void reflectThorns(EntityId actor, EntityId target, float physicalDamage);
    void reflectIronMaiden(EntityId actor, EntityId target, float physicalDamage);
    void healLifeTap(EntityId actor, EntityId target, float physicalDamage, bool missile = false);
    void triggerCombatEffects(EntityId target, CombatEffectEvent event, EntityId other);
    float absorbEnergyShield(EntityId target, float damage);
    bool beginWeaponSkill(WeaponSkillCaster actor, const SkillCastSpec &skill, Vec aim, EntityId target);
    void advanceWeaponAttack(WeaponSkillCaster actor);
    void advanceCharge(WeaponSkillCaster actor, float dt);
    void launchFirewall(EntityId actor, Vec center, Vec heading, const FirewallSpec &spec, SkillBehavior behavior);
    void releaseNativeBurst(SkillProjectileSource actor, int missileId, const NativeSkillCast &definition);
    void pulseLegacyAura(EntityId actor, const AuraDefinition &definition, EffectFrame &nextFrame);
    void applyNativeCurse(EntityId actor, EntityId target, const AuraDefinition &definition);
    bool advanceSpecialMissile(Missile &missile, float dt, std::vector<Missile> &spawned);
    void hitMissile(Missile &missile, EntityId target, std::vector<Missile> &spawned);
    bool advanceHolyBolt(Missile &missile, Vec next, int size, std::vector<Missile> &spawned);
};
} // namespace d2x
