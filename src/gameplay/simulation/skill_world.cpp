#include "gameplay/units/actions.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/world_values.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/missile_launch_spec.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/combat/avoidance.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/weapon_port.hpp"
#include "gameplay/skills/weapon_caster.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/necro_summon_spec.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include "core/random.hpp"
#include <stdexcept>
#include <algorithm>
#include <utility>

namespace d2x {
// This is the only skill-world adapter that sees the concrete simulation.
// Queries use current storage so restore and region changes cannot leave a
// cached Grid, PlayerState or Enemy reference in a long-lived service.
class SimulationSkillWorld final : public ISkillWorld, public ISkillWeaponWorld {
    Simulation &simulation_;
    PlayerState &player(EntityId actor) {
        if (actor != simulation_.state_.player.id)
            throw std::runtime_error("Actor has no player action capabilities");
        return simulation_.state_.player;
    }
    Enemy *enemy(EntityId actor) const { return simulation_.findEnemy(actor); }

  public:
    explicit SimulationSkillWorld(Simulation &simulation) : simulation_(simulation) {}
    EntityId allocate() override { return simulation_.ids_.allocate(); }
    uint64_t childSeed() override { return childRandom(simulation_.unitRandom_); }
    EffectFrame frame() const override { return simulation_.state_.frame; }
    float time() const override { return simulation_.state_.time; }
    bool safeZone() const override { return simulation_.safeZone_; }
    Vec missileOrigin() const override { return simulation_.missileWorldOrigin_; }
    CombatUnit unit(EntityId actor) override { return simulation_.combatUnit(actor); }
    std::vector<CombatUnit> units() override {
        std::vector<CombatUnit> result;
        for (const auto &unit : simulation_.combatUnits()) result.push_back(unit);
        return result;
    }
    Vec position(EntityId actor) const override { return simulation_.unitPosition(actor); }
    bool canAttack(EntityId actor, EntityId target) const override { return simulation_.canAttack(actor, target); }
    Relation relation(EntityId first, EntityId second) const override { return simulation_.relation(first, second); }
    bool active(Vec point) const override { return simulation_.active(point); }
    bool nearby(Vec observer, Vec point) const override { return simulation_.rooms_->nearby(observer, point); }
    float coldDuration(EntityId actor, const CombatUnit &target, int frames) const override {
        // The existing cold adapter needs the actor type for the cold-effect
        // policy, but retains the caller's original attribute snapshot.
        auto unit = simulation_.combatUnit(target.id);
        unit.stats = target.stats;
        return simulation_.missileColdDuration(actor, unit, frames);
    }
    float damage(const DamageRequest &hit) override { return simulation_.dealDamage(hit); }
    bool avoidMissile(EntityId target) override {
        auto unit = simulation_.combatUnit(target);
        const int avoided = avoidCombatHit(unit, true, [this] {
            const auto *weapon = simulation_.attackWeapon(false, false);
            return weapon && simulation_.attackTiming_ ? simulation_.attackTiming_(*weapon, false, false, "s1") : std::nullopt;
        });
        if (avoided >= 0 && unit.player) simulation_.emit(SkillCast{unit.id, avoided, *unit.position});
        return avoided >= 0;
    }
    void restore(EntityId target, float life, float mana) override { simulation_.restoreUnit(target, life, mana); }
    void knockback(EntityId actor, EntityId target) override { simulation_.applyAuraKnockback(actor, target); }
    bool missileSegment(Vec from, Vec to, MissileCollisionRule rule) const override {
        return simulation_.grid_->missileSegment(from, to, rule);
    }
    bool collisionSegment(Vec from, Vec to, uint16_t mask) const override {
        return simulation_.grid_->collisionSegment(from, to, mask);
    }
    bool walkable(EntityId actor, Vec target) const override {
        const auto unit = simulation_.combatUnit(actor);
        if (unit.player) return simulation_.grid_->walkable(target, playerMovement);
        return unit.monster && simulation_.grid_->walkable(target, simulation_.movementRule(*unit.records.monster));
    }
    bool pathClear(int missile, Vec from, Vec to) const override { return simulation_.missilePathClear(missile, from, to); }
    bool clipPath(int missile, Vec from, Vec &to) const override { return simulation_.clipMissilePath(missile, from, to); }
    std::optional<int> missileSize(int missile) const override {
        const auto found = simulation_.missileCollisions_.find(missile);
        return found == simulation_.missileCollisions_.end() ? std::nullopt : std::optional<int>{found->second.size};
    }
    std::optional<std::pair<EntityId, float>> missileTarget(const Missile &missile, Vec to) override {
        return simulation_.missileTarget(missile, to);
    }
    bool returnFire(int missile) const override {
        const auto found = simulation_.missileReturnFire_.find(missile);
        return found != simulation_.missileReturnFire_.end() && found->second;
    }
    bool hasResolver() const override { return bool(simulation_.resolveUnitSkill_); }
    SkillCastSpec resolve(EntityId actor, int skill, int rank) const override {
        if (!hasResolver()) throw std::runtime_error("Actor has no skill resolver");
        return simulation_.resolveUnitSkill_(actor, skill, rank);
    }
    float &nextHitTime(EntityId target) override { return simulation_.state_.area.novaHitUntil[target]; }
    Missile &addMissile(Missile missile) override {
        prepareMissileLaunch(missile, simulation_.combatUnit(missile.owner).stats.attributes.combat,
            simulation_.missileCanSlow_ && simulation_.missileCanSlow_(missile.missileId),
            simulation_.missileCanPierce_ && simulation_.missileCanPierce_(missile.missileId));
        simulation_.state_.area.missiles.push_back(std::move(missile));
        return simulation_.state_.area.missiles.back();
    }
    void enqueueMissile(Missile missile) override {
        prepareMissileLaunch(missile, simulation_.combatUnit(missile.owner).stats.attributes.combat,
            simulation_.missileCanSlow_ && simulation_.missileCanSlow_(missile.missileId),
            simulation_.missileCanPierce_ && simulation_.missileCanPierce_(missile.missileId));
        if (simulation_.updatingMissiles_) simulation_.deferredMissiles_.push_back(std::move(missile));
        else simulation_.state_.area.missiles.push_back(std::move(missile));
    }
    void addEffect(Effect effect) override { simulation_.state_.area.effects.push_back(std::move(effect)); }
    void emit(SkillEvent event) override {
        std::visit([this](auto value) { simulation_.emit(std::move(value)); }, std::move(event));
    }
    void message(std::string_view value) override { simulation_.state_.message = value; }
    bool telekinesis(EntityId target, int range, bool activate) override {
        return simulation_.telekinesisTarget_ && simulation_.telekinesisTarget_(target, range, activate);
    }
    bool usableCorpse(EntityId target, bool explosion) const override { return simulation_.usableCorpse(target, explosion); }
    CorpseExplosionSource corpseExplosionSource(EntityId target) override {
        auto *corpse = enemy(target);
        if (!corpse || !simulation_.usableCorpse(target, true)) return {};
        const auto life = simulation_.corpseExplosionLife_(*corpse);
        return {life.value_or(0), simulation_.combatUnit(target).stats.level, &corpse->combatRandom};
    }
    EntityId corpseNear(Vec target, bool explosion) const override { return simulation_.corpseNear(target, explosion); }
    bool summonGround(EntityId actor, const SkillCastSpec &skill, Vec target) override {
        return simulation_.summonGround_ ? simulation_.summonGround_(actor, skill, target) : simulation_.summonPet(actor, skill, target);
    }
    bool summonCorpse(EntityId actor, const SkillCastSpec &skill, EntityId corpse) override {
        return simulation_.summonCorpse_ ? simulation_.summonCorpse_(actor, skill, corpse) : simulation_.summonFromCorpse(actor, skill, corpse);
    }
    EntityId createBoneBarrier(EntityId actor, const BoneSkillSpec &program, Vec position, EntityId root, int skill, int rank, bool search, Vec facing) override {
        if (simulation_.safeZone_ || !program.barrier || (root && !simulation_.combatUnit(root).alive())) return {};
        auto stats = program.barrierStats.at(size_t(simulation_.state_.population.difficulty));
        Vec cell{std::floor(position.x) + .5f, std::floor(position.y) + .5f};
        auto clear = [&](Vec candidate) {
            if (!simulation_.grid_->walkable(candidate, {0x3c01, stats.collisionSize})) return false;
            for (auto unit : units())
                if (unit.alive() && int(unit.position->x) == int(candidate.x) && int(unit.position->y) == int(candidate.y)) return false;
            return true;
        };
        bool placed = clear(cell);
        for (int radius = 1; search && !placed && radius <= 4; ++radius)
            for (int y = -radius; !placed && y <= radius; ++y)
                for (int x = -radius; !placed && x <= radius; ++x) {
                    if (std::abs(x) != radius && std::abs(y) != radius) continue;
                    const Vec candidate = position + Vec{float(x), float(y)};
                    if (clear(candidate)) { cell = {std::floor(candidate.x) + .5f, std::floor(candidate.y) + .5f}; placed = true; }
                }
        if (!placed) return {};
        const auto nativeLife = int64_t(stats.attributes.maxLife) * 256;
        const auto life = nativeLife + nativeLife * program.lifePercent / 100;
        stats.level = simulation_.combatUnit(actor).stats.level;
        stats.attributes.maxLife = int(life / 256);
        Enemy wall;
        wall.id = allocate(); wall.kind = MonsterKind::BoneWall;
        wall.identity.monster = "bonewall"; wall.identity.origin = SpawnOrigin::Summoned;
        wall.identity.spawnKey = "barrier." + std::to_string(wall.id.value);
        wall.pos = cell; wall.hp = wall.maxHp = float(life) / 256.f;
        wall.intrinsicCombat = stats;
        wall.allegiance = {0, {}, 0, CombatRole::Monster};
        wall.noTreasure = true; wall.deathUnselectable = true;
        wall.summonSkill = skill; wall.summonRank = rank;
        wall.combatRandom = childSeed();
        wall.boneBarrier = std::make_shared<BoneBarrierState>(BoneBarrierState{actor, root ? root : wall.id, frame() + uint64_t(program.barrierFrames), facing});
        const auto rise = program.prison ? std::optional<float>(0.f) : simulation_.monsterResurrectionDuration_ ? simulation_.monsterResurrectionDuration_(wall) : std::nullopt;
        if (!rise) return {};
        wall.resurrectionDuration = wall.resurrectionRemaining = *rise;
        const auto id = wall.id;
        simulation_.state_.area.enemies.push_back(std::move(wall));
        if (simulation_.barriersChanged_) simulation_.barriersChanged_();
        return id;
    }
    std::optional<Vec> prisonTarget(EntityId target) const override {
        const auto unit = simulation_.combatUnit(target);
        if (unit) return *unit.position;
        return simulation_.prisonTarget_ ? simulation_.prisonTarget_(target) : std::nullopt;
    }
    bool summonHydra(EntityId actor, const SkillCastSpec &skill, Vec target) override {
        return simulation_.summonHydra(actor, skill, target);
    }
    void beginCast(EntityId actor) override {
        auto &unit = player(actor);
        clearAttackIntent(simulation_.skillWeaponCaster(unit.id));
    }
    void teleport(EntityId actor, Vec target) override {
        auto &unit = player(actor);
        unit.movement.pos = unit.movement.previous = target;
        simulation_.relocateCompanions(actor, target);
        // PetType.hireable.warp=1: the owner's native teleport also warps the mercenary.
        if (unit.hireling.active()) {
            auto &merc = unit.hireling;
            merc.pos = target; merc.route.clear(); merc.moving = false;
            merc.attack.reset(); merc.attackTimer = merc.thinkTimer = 0; merc.animationTime = 0;
        }
    }
    bool curseEligible(EntityId target, bool ai) const override {
        return !simulation_.curseEligible_ || simulation_.curseEligible_(simulation_.combatUnit(target), ai);
    }
    bool auraEligible(EntityId target, bool ally) const override {
        return !simulation_.auraEligible_ || simulation_.auraEligible_(simulation_.combatUnit(target), ally);
    }
    int aiCurseDivisor() const override { return simulation_.aiCurseDivisor_; }
    int attractState() const override { return simulation_.attractState_; }
    void attract(EntityId target, EntityId victim, EffectFrame until, EffectSource source) override {
        if (auto *unit = enemy(target)) {
            unit->attractedTarget = victim; unit->attractedUntil = until;
            unit->attractionSource = source;
            if (unit->activeCurseAi == CurseAi::Terror && unit->terrorMovement) {
                unit->terrorMovement->threat = victim;
                unit->terrorMovement->beganEscape = false;
            }
            unit->combatTarget = {}; unit->route.clear(); unit->approach.reset(); unit->rethink = 0;
            cancelTimedAction({unit->attack, unit->attackDuration, unit->attackImpact});
        }
    }
    void resetCurseAi(EntityId target) override {
        if (auto *unit = enemy(target)) {
            for (const auto &effect : unit->combatEffects.entries())
                if (effect.activeAt(simulation_.state_.frame) && effect.spec.curseAi == CurseAi::DimVision) {
                    // Switching the think function does not cancel an accepted WL/RN/A1 action.
                    unit->activeCurseAi = CurseAi::DimVision;
                    unit->rethink = 0;
                    return;
                }
            for (const auto &effect : unit->combatEffects.entries())
                if (effect.activeAt(simulation_.state_.frame) && effect.spec.curseAi == CurseAi::Terror) {
                    const bool repeated = unit->activeCurseAi == CurseAi::Terror && unit->terrorMovement;
                    if (!repeated) {
                        const auto threat = simulation_.combatUnit(unit->combatTarget);
                        const auto [velocity, running] = simulation_.terrorMovement_
                            ? simulation_.terrorMovement_(*unit) : std::pair{0, false};
                        unit->terrorMovement = Enemy::TerrorMovement{
                            threat.alive() ? threat.id : effect.spec.source.entity, velocity, running, false};
                        unit->route.clear();
                    }
                    unit->terrorMovement->beganEscape = false;
                    unit->approach.reset(); unit->combatTarget = unit->terrorMovement->threat;
                    cancelTimedAction({unit->attack, unit->attackDuration, unit->attackImpact});
                    unit->skill2Remaining = unit->skill2Duration = 0;
                    unit->teleportTarget.reset(); unit->nestSpawnPosition.reset(); unit->aiCorpse = {};
                    unit->aiPursuing = unit->aiCircling = false;
                    unit->aiEscaping = !unit->route.empty(); unit->aiRunning = unit->terrorMovement->running;
                    unit->activeCurseAi = CurseAi::Terror; unit->rethink = 0;
                    return;
                }
            unit->terrorMovement.reset();
            unit->route.clear();
            unit->approach.reset(); unit->combatTarget = {};
            cancelTimedAction({unit->attack, unit->attackDuration, unit->attackImpact});
            unit->skill2Remaining = unit->skill2Duration = 0;
            unit->teleportTarget.reset(); unit->nestSpawnPosition.reset(); unit->aiCorpse = {};
            unit->aiPursuing = unit->aiEscaping = unit->aiCircling = unit->aiRunning = false;
            unit->rethink = 0;
        }
    }
    void effectsChanged(std::span<const RemovedCombatEffect> removed) override { simulation_.combatEffectsChanged(removed); }
    std::vector<SkillAuraSource> auraSources(bool playerOnly) override {
        std::vector<SkillAuraSource> result;
        auto &player = simulation_.state_.player;
        if (player.skills.aura) result.push_back({player.id, &player.skills.aura->definition, &player.skills.aura->nextFrame});
        if (!playerOnly)
            for (auto &unit : simulation_.state_.area.enemies)
                if (unit.enchantment && unit.enchantment->aura) {
                    const auto &aura = *unit.enchantment->aura;
                    if (aura.skill == 98 || aura.skill == 102 || aura.skill == 108 || aura.skill == 114 ||
                        aura.skill == 118 || aura.skill == 122 || aura.skill == 123)
                        result.push_back({unit.id, &aura, &unit.nextAuraFrame});
                }
        if (!playerOnly) for (auto &pet : simulation_.state_.companions)
            if (pet.living() && pet.necroPet && pet.necroPet->spec->aura)
                result.push_back({pet.id, &*pet.necroPet->spec->aura, &pet.necroPet->nextAura});
        return result;
    }
    const AuraDefinition *ownAura(EntityId actor) const override {
        auto unit = simulation_.combatUnit(actor);
        if (unit.monster && unit.records.monster->necroPet && unit.records.monster->necroPet->spec->aura) return &*unit.records.monster->necroPet->spec->aura;
        if (unit.player && unit.records.player->skills.aura) return &unit.records.player->skills.aura->definition;
        if (unit.monster && unit.records.monster->enchantment && unit.records.monster->enchantment->aura)
            return &*unit.records.monster->enchantment->aura;
        return nullptr;
    }
    std::optional<int> baseResistance(EntityId target, DamageType type) const override {
        const auto *unit = enemy(target);
        if (!unit) return std::nullopt;
        if (unit->intrinsicCombat) {
            const auto &stats = unit->intrinsicCombat->attributes;
            return type == DamageType::Fire ? stats.fireResist : type == DamageType::Cold ? stats.coldResist : stats.lightningResist;
        }
        return simulation_.monsterResistance_ ? simulation_.monsterResistance_(*unit, simulation_.state_.area.region, type) : std::nullopt;
    }
    std::optional<int> coldEffect(EntityId target) const override {
        return simulation_.unitColdEffect_ ? std::optional<int>{simulation_.unitColdEffect_(simulation_.combatUnit(target))} : std::nullopt;
    }
    CombatStateDefinition shatterState() const override { return simulation_.shatterDeathState_; }
    std::vector<SkillCorpse> corpses() const override {
        std::vector<SkillCorpse> result;
        for (const auto &unit : simulation_.state_.area.enemies)
            result.push_back({unit.id, unit.pos, unit.corpseAvailable()});
        return result;
    }
    bool redeemableCorpse(EntityId target) const override {
        const auto *unit = enemy(target);
        if (!unit || !simulation_.redemptionCorpseEligible_ || !simulation_.redemptionCorpseEligible_(*unit)) return false;
        const auto duration = simulation_.monsterDeathDuration_ ? simulation_.monsterDeathDuration_(*unit) : std::nullopt;
        return duration && unit->deathAge >= *duration;
    }
    void consumeCorpse(EntityId target, bool hide) override {
        if (auto *unit = enemy(target)) {
            unit->deathUnselectable = true;
            if (hide) unit->corpseConsumed = true;
        }
    }
    void suppressManaRegen(EntityId target, bool suppress) override { player(target).skills.auraSuppressesManaRegen = suppress; }
    int blazeState() const override { return simulation_.blazeState_; }
    int energyShieldState() const override { return simulation_.energyShieldState_; }
    void freeze(EntityId actor, EntityId target, int frames) override {
        simulation_.applyMissileFreeze(actor, simulation_.combatUnit(target), frames);
    }
    void nativeMissileHit(const Missile &missile, EntityId target) override {
        if (auto *source = enemy(missile.owner)) {
            if (source->kind == MonsterKind::BloodRaven && missile.monsterAttackMode == 4)
                simulation_.resolveMonsterAttack(*source, 1, true, target);
            else if (missile.monsterAttackMode >= 3) simulation_.resolveMonsterSpell(*source, missile, target);
            else simulation_.resolveMonsterAttack(*source, missile.monsterAttackMode, true, target);
        }
    }
    void impact(Missile &missile, std::vector<Missile> &spawned, EntityId target) override {
        simulation_.resolveMissileImpact(missile, spawned, target);
    }
    bool meleeReach(EntityId actor, EntityId target, const WeaponDamage &weapon) const override {
        if (actor != simulation_.state_.player.id)
            throw std::runtime_error("Actor has no equipment melee capabilities");
        return simulation_.meleeReach(target, weapon);
    }
    bool beginWeaponAttack(EntityId actor, Vec aim, EntityId target, const WeaponDamage &weapon,
                           bool thrown, bool leftHand, const WeaponSkillSpec *skill) override {
        player(actor);
        return simulation_.beginWeaponAttack(aim, target, weapon, thrown, leftHand, skill);
    }
    void requestWeaponAttack(EntityId actor, EntityId target, Vec aim) override {
        player(actor);
        simulation_.requestAttack(Attack{target, false, false, aim, false});
    }
    std::deque<Vec> approach(EntityId actor, Vec target) const override {
        auto unit = simulation_.combatUnit(actor);
        if (!unit) return {};
        const auto rule = unit.monster ? simulation_.movementRule(*unit.records.monster) : playerMovement;
        return simulation_.grid_->path(*unit.position, target, false, rule);
    }
    bool movementSegment(EntityId actor, Vec from, Vec to) const override {
        auto unit = simulation_.combatUnit(actor);
        if (!unit) return false;
        const auto rule = unit.monster ? simulation_.movementRule(*unit.records.monster) : playerMovement;
        return simulation_.grid_->segment(from, to, {}, rule);
    }
    void fireWeaponProjectile(EntityId actor, Vec aim, const WeaponDamage &weapon,
                              bool thrown, const SkillCastSpec *skill) override {
        player(actor);
        simulation_.firePhysicalProjectile(aim, weapon, thrown, skill);
    }
    void weaponMelee(EntityId actor, EntityId target, const WeaponDamage &weapon, const SkillCastSpec *skill) override {
        player(actor);
        simulation_.meleeDamage(target, weapon, skill);
    }
    AttackElements rollWeaponElements(EntityId actor, EntityId weapon) override {
        player(actor);
        return simulation_.rollAttackElements(weapon);
    }
    void weaponHit(EntityId actor, EntityId target, float amount, const AttackElements &elements) override {
        simulation_.resolveWeaponHit(target, amount, actor, elements);
    }
    bool hasEquipmentWear() const override { return bool(simulation_.wearEquipment_); }
    void wearWeapon(EntityId item) override { if (simulation_.wearEquipment_) simulation_.wearEquipment_(item, false); }
    bool canStun(EntityId target) const override {
        const auto *unit = enemy(target);
        return unit && simulation_.monsterWalkSpeed_ && simulation_.monsterWalkSpeed_(*unit).value_or(0) > 0;
    }
    void stun(EntityId target, int frames) override {
        if (auto *unit = enemy(target)) unit->stun = std::max(unit->stun, float(frames) / 25.f);
    }
    void convert(EntityId actor, const CombatUnit &target, const SkillCastSpec &skill, int level) override;
};


void SimulationSkillWorld::convert(EntityId actor, const CombatUnit &target, const SkillCastSpec &skill, int level) {
    auto *victim = enemy(target.id);
    if (!victim || !skill.weapon) return;
    const auto source = simulation_.combatUnit(actor);
    auto &monster = *victim;
    monster.conversion = Enemy::ConversionState{monster.allegiance,
        simulation_.state_.frame + EffectFrame(skill.weapon->conversionFrames), target.stats.level, level, monster.maxHp};
    monster.conversion->state = skill.weapon->conversionState.id;
    std::vector<EffectHandle> curses;
    for (const auto &effect : monster.combatEffects.entries())
        if (effect.spec.state.curse) curses.push_back(effect.handle);
    for (const auto handle : curses) monster.combatEffects.remove(handle);
    CombatEffectSpec converted;
    converted.state = skill.weapon->conversionState;
    converted.source = {CombatEffectSource::Skill, actor, skill.sourceId, skill.rank};
    converted.duration = EffectFrame(skill.weapon->conversionFrames);
    monster.combatEffects.apply(std::move(converted), simulation_.state_.frame);
    if (target.stats.level > level) {
        monster.maxHp = std::max(1.f / 256.f, float(int(monster.maxHp) * level / target.stats.level));
        monster.hp = std::clamp(float(int(monster.hp) * level / target.stats.level), 1.f / 256.f, monster.maxHp);
    }
    monster.allegiance.faction = source.identity.faction;
    monster.allegiance.owner = actor;
    monster.combatTarget = {};
    monsterStopApproach(monster);
    monster.route.clear(); cancelTimedAction({monster.attack, monster.attackDuration, monster.attackImpact});
    monster.aiPursuing = monster.aiEscaping = monster.aiCircling = monster.aiRunning = false;
    monster.aiCorpse = {};
    monster.skill2Remaining = monster.skill2Duration = 0;
    monster.rethink = 0;
}

Simulation::Simulation(EntityIds &ids) : ids_(ids), skillWorld_(std::make_unique<SimulationSkillWorld>(*this)) {
    state_.player.id = ids_.allocate();
    heal();
}
Simulation::~Simulation() = default;
SkillRuntime Simulation::skills() { return SkillRuntime{*skillWorld_, static_cast<SimulationSkillWorld &>(*skillWorld_)}; }
SkillCaster Simulation::skillCaster(EntityId actor) {
    auto &unit = state_.player;
    if (actor != unit.id) throw std::runtime_error("Actor has no casting capabilities");
    return {unit.id, unit.movement.pos, unit.movement.previous, unit.movement.look, unit.resources.mana, unit.actions.castTime,
            unit.skills.lastCastDuration, unit.skills.lastCastRate, unit.skills.skillDelayUntil, unit.skills.lightningSequence,
            unit.skills.pendingCast, unit.skills.channel, unit.skills.thunderStorm, unit.combatEffects, unit.combatRandom,
            unit.actions.dead, bool(unit.actions.blockAnimation), bool(unit.actions.charge), unit.movement.moving, bool(unit.equipment.shield),
            unit.actions.meleeTime, unit.actions.hitTime};
}
WeaponSkillCaster Simulation::skillWeaponCaster(EntityId actor) {
    auto &unit = state_.player;
    if (actor != unit.id) throw std::runtime_error("Actor has no weapon action capabilities");
    return {skillCaster(actor), unit.equipment, unit.actions.weaponAttack, unit.actions.charge, unit.actions.approachSkill,
            unit.movement.route, unit.actions.attackTarget, unit.actions.attackPosition, unit.actions.attackStationary,
            unit.actions.throwAttack, unit.actions.leftHandAttack, unit.movement.moving, unit.movement.runningNow,
            unit.actions.meleeTime, unit.resources.hp, unit.character.level};
}
} // namespace d2x
