#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/combat/damage_type.hpp"
#include "gameplay/effects/definition.hpp"
#include "gameplay/skills/events.hpp"
#include "world/collision.hpp"
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace d2x {
struct CombatUnit;
struct SkillCorpse;
struct CorpseExplosionSource;
struct BoneSkillSpec;
struct SkillAuraSource;
struct SkillCastSpec;
struct AuraDefinition;
struct Missile;
struct Effect;
struct DamageRequest;
struct RemovedCombatEffect;
struct EffectSource;
enum class Relation;
// Authority-side execution boundary. No Session, Simulation, actor records,
// content tables, input or GPU resources cross this interface.
class ISkillWorld {
  public:
    virtual ~ISkillWorld() = default;
    virtual EntityId allocate() = 0;
    virtual uint64_t childSeed() = 0;
    virtual EffectFrame frame() const = 0;
    virtual float time() const = 0;
    virtual bool safeZone() const = 0;
    virtual Vec missileOrigin() const = 0;
    virtual CombatUnit unit(EntityId id) = 0;
    virtual std::vector<CombatUnit> units() = 0;
    virtual Vec position(EntityId id) const = 0;
    virtual bool canAttack(EntityId actor, EntityId target) const = 0;
    virtual Relation relation(EntityId first, EntityId second) const = 0;
    virtual bool active(Vec position) const = 0;
    virtual bool nearby(Vec observer, Vec point) const = 0;
    virtual float coldDuration(EntityId actor, const CombatUnit &target, int frames) const = 0;
    virtual float damage(const DamageRequest &request) = 0;
    virtual void restore(EntityId target, float life, float mana = 0) = 0;
    virtual void knockback(EntityId actor, EntityId target) = 0;
    virtual bool missileSegment(Vec from, Vec to, MissileCollisionRule rule) const = 0;
    virtual bool collisionSegment(Vec from, Vec to, uint16_t mask) const = 0;
    virtual bool walkable(EntityId actor, Vec target) const = 0;
    virtual bool pathClear(int missile, Vec from, Vec to) const = 0;
    virtual bool clipPath(int missile, Vec from, Vec &to) const = 0;
    virtual std::optional<int> missileSize(int missile) const = 0;
    virtual std::optional<std::pair<EntityId, float>> missileTarget(const Missile &missile, Vec to) = 0;
    virtual bool returnFire(int missile) const = 0;
    virtual bool hasResolver() const = 0;
    virtual SkillCastSpec resolve(EntityId actor, int skill, int rank) const = 0;
    virtual float &nextHitTime(EntityId target) = 0;
    virtual Missile &addMissile(Missile missile) = 0;
    virtual void enqueueMissile(Missile missile) = 0;
    virtual void addEffect(Effect effect) = 0;
    virtual void emit(SkillEvent event) = 0;
    virtual void message(std::string_view value) = 0;
    virtual bool telekinesis(EntityId target, int range, bool activate) = 0;
    virtual bool usableCorpse(EntityId target, bool explosion = false) const = 0;
    virtual CorpseExplosionSource corpseExplosionSource(EntityId target) = 0;
    virtual EntityId corpseNear(Vec target, bool explosion = false) const = 0;
    virtual bool summonGround(EntityId actor, const SkillCastSpec &skill, Vec target) = 0;
    virtual bool summonCorpse(EntityId actor, const SkillCastSpec &skill, EntityId corpse) = 0;
    virtual EntityId createBoneBarrier(EntityId actor, const BoneSkillSpec &program, Vec position, EntityId root, int skill, int rank, bool search = false, Vec facing = {1,0}) = 0;
    virtual std::optional<Vec> prisonTarget(EntityId target) const = 0;
    virtual bool summonHydra(EntityId actor, const SkillCastSpec &skill, Vec target) = 0;
    virtual void beginCast(EntityId actor) = 0;
    virtual void teleport(EntityId actor, Vec target) = 0;
    virtual bool curseEligible(EntityId target, bool ai) const = 0;
    virtual bool auraEligible(EntityId target, bool ally) const = 0;
    virtual int aiCurseDivisor() const = 0;
    virtual int attractState() const = 0;
    virtual void attract(EntityId target, EntityId victim, EffectFrame until, EffectSource source) = 0;
    virtual void resetCurseAi(EntityId target) = 0;
    virtual void effectsChanged(std::span<const RemovedCombatEffect> removed) = 0;
    virtual std::vector<SkillAuraSource> auraSources(bool playerOnly) = 0;
    virtual const AuraDefinition *ownAura(EntityId target) const = 0;
    virtual std::optional<int> baseResistance(EntityId target, DamageType type) const = 0;
    virtual std::optional<int> coldEffect(EntityId target) const = 0;
    virtual CombatStateDefinition shatterState() const = 0;
    virtual std::vector<SkillCorpse> corpses() const = 0;
    virtual bool redeemableCorpse(EntityId target) const = 0;
    virtual void consumeCorpse(EntityId target, bool hide = true) = 0;
    virtual void suppressManaRegen(EntityId target, bool suppress) = 0;
    virtual int blazeState() const = 0;
    virtual int energyShieldState() const = 0;
    virtual void freeze(EntityId actor, EntityId target, int frames) = 0;
    virtual void nativeMissileHit(const Missile &missile, EntityId target) = 0;
    virtual void impact(Missile &missile, std::vector<Missile> &spawned, EntityId target) = 0;
};
} // namespace d2x
