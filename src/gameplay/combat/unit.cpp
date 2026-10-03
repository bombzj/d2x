#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "damage_resolution.hpp"
#include "core/random.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <algorithm>
#include <set>

namespace d2x {
namespace {
std::optional<CombatIdentity> identityOf(const WorldState &state, EntityId id) {
    if (!id) return std::nullopt;
    if (id == state.player.id) return state.player.allegiance;
    if (id == state.player.hireling.id) {
        auto identity = state.player.hireling.allegiance;
        identity.owner = state.player.id;
        return identity;
    }
    for (const auto &unit : state.area.enemies) if (unit.id == id) return unit.allegiance;
    for (const auto &unit : state.companions) if (unit.id == id) return unit.allegiance;
    return std::nullopt;
}
std::pair<EntityId, CombatIdentity> rootIdentity(const WorldState &state, EntityId id) {
    std::set<EntityId> visited;
    CombatIdentity identity;
    while (id && visited.insert(id).second) {
        auto value = identityOf(state, id);
        if (!value) return {{}, {}};
        identity = *value;
        if (!identity.owner) return {id, identity};
        id = identity.owner;
    }
    return {{}, {}}; // Invalid/cyclic ownership never confers hostility or credit.
}
}
EntityId Simulation::controllingPlayer(EntityId id) const {
    for (const auto &enemy : state_.area.enemies)
        if (enemy.id == id) {
            if (enemy.attractedUntil > state_.frame)
                for (const auto &target : state_.area.enemies)
                    if (target.id == enemy.attractedTarget)
                        for (const auto &effect : target.combatEffects.entries())
                            if (effect.activeAt(state_.frame) && effect.spec.curseAi == CurseAi::Attract &&
                                effect.spec.source.entity == state_.player.id) return state_.player.id;
            for (const auto &effect : enemy.combatEffects.entries())
                if (effect.activeAt(state_.frame) && effect.spec.curseAi == CurseAi::Confuse &&
                    effect.spec.source.entity == state_.player.id) return state_.player.id;
        }
    const auto [root, identity] = rootIdentity(state_, id);
    return identity.role == CombatRole::Player ? root : EntityId{};
}
Relation Simulation::relation(EntityId first, EntityId second) const {
    if (!first || !second) return Relation::Neutral;
    if (first == second) return Relation::Allied;
    const Enemy *firstMonster = nullptr, *secondMonster = nullptr;
    for (const auto &enemy : state_.area.enemies) {
        if (enemy.id == first) firstMonster = &enemy;
        if (enemy.id == second) secondMonster = &enemy;
    }
    const bool hostileMonsters = firstMonster && secondMonster && !firstMonster->allegiance.owner && !secondMonster->allegiance.owner &&
        state_.relations.factions.contains({firstMonster->allegiance.faction, state_.player.allegiance.faction}) &&
        state_.relations.factions.at({firstMonster->allegiance.faction, state_.player.allegiance.faction}) == Relation::Hostile &&
        state_.relations.factions.contains({secondMonster->allegiance.faction, state_.player.allegiance.faction}) &&
        state_.relations.factions.at({secondMonster->allegiance.faction, state_.player.allegiance.faction}) == Relation::Hostile;
    if (hostileMonsters)
        if (firstMonster->attractedUntil > state_.frame && firstMonster->attractedTarget == second && secondMonster->hp > 0 &&
            secondMonster->combatEffects.hasState(attractState_, state_.frame))
            return Relation::Hostile;
    if (hostileMonsters)
        for (const auto *monster : {firstMonster, secondMonster})
            for (const auto &effect : monster->combatEffects.entries())
                if (effect.activeAt(state_.frame) && effect.spec.curseAi == CurseAi::Confuse) return Relation::Hostile;
    const auto [a, left] = rootIdentity(state_, first);
    const auto [b, right] = rootIdentity(state_, second);
    if (!a || !b) return Relation::Neutral;
    if (a == b) return Relation::Allied;
    if (auto found = state_.relations.units.find({first, second}); found != state_.relations.units.end()) return found->second;
    if (auto found = state_.relations.units.find({a, b}); found != state_.relations.units.end()) return found->second;
    if (left.party && left.party == right.party) return Relation::Allied;
    if (auto found = state_.relations.factions.find({left.faction, right.faction}); found != state_.relations.factions.end()) return found->second;
    return left.faction && left.faction == right.faction ? Relation::Allied : Relation::Neutral;
}
bool Simulation::canAttack(EntityId attacker, EntityId defender) const {
    const auto identity = identityOf(state_, defender);
    return !safeZone_ && identity && identity->attackable && relation(attacker, defender) == Relation::Hostile;
}
Vec Simulation::unitPosition(EntityId id) const {
    auto unit = const_cast<Simulation *>(this)->combatUnit(id);
    return unit ? *unit.position : Vec{};
}
Vec Simulation::monsterTargetPosition(const Enemy &enemy) const { return unitPosition(enemy.combatTarget); }
EntityId Simulation::chooseTarget(EntityId actor, float range) {
    auto source = combatUnit(actor);
    if (!source.alive()) return {};
    if (source.monster && source.records.monster->attractedUntil > state_.frame) {
        const auto target = combatUnit(source.records.monster->attractedTarget);
        if (target.alive() && target.effects->hasState(attractState_, state_.frame) &&
            canAttack(actor, target.id) && active(*target.position)) return target.id;
    }
    EntityId result;
    // Target acquisition needs identity/geometry only; do not repeatedly derive
    // every candidate's resistances and equipment for every AI thinker.
    auto consider = [&](EntityId id, Vec position, float life) {
        if (life <= 0 || !canAttack(actor, id) || !active(position)) return;
        const float distance = (position - *source.position).length();
        if (distance < range && grid_->missileSegment(*source.position, position, {0x04, 1})) {
            range = distance; result = id;
        }
    };
    consider(state_.player.id, state_.player.pos, state_.player.hp);
    if (state_.player.hireling.active()) consider(state_.player.hireling.id, state_.player.hireling.pos, state_.player.hireling.hp);
    for (const auto &unit : state_.area.enemies) consider(unit.id, unit.pos, unit.hp);
    for (const auto &unit : state_.companions) consider(unit.id, unit.pos, unit.hp);
    return result;
}
float Simulation::incomingDamage(EntityId attacker, EntityId defender, float amount) const {
    auto &simulation = *const_cast<Simulation *>(this);
    const auto from = simulation.combatUnit(attacker), to = simulation.combatUnit(defender);
    if (!from || !to) return amount;
    int percent = 100;
    if (to.identity.role == CombatRole::Player && (from.identity.role == CombatRole::Player ||
        from.identity.role == CombatRole::Hireling || from.identity.role == CombatRole::Summon)) percent = 17;
    else if (to.identity.role == CombatRole::Hireling && from.identity.role == CombatRole::Hireling) percent = 25;
    else if (from.identity.role == CombatRole::Hireling && to.stats.boss) percent = hirelingBossDamagePercent_;
    else if (from.stats.primeEvil && to.identity.role == CombatRole::Hireling) percent = 200;
    else if (from.stats.primeEvil && to.identity.role == CombatRole::Summon) percent = 400;
    return float(int64_t(amount * 256.f) * percent / 100) / 256.f;
}
void Simulation::restoreUnit(EntityId id, float life, float mana) {
    auto unit = combatUnit(id);
    if (!unit.alive()) return;
    *unit.life = std::min(float(unit.stats.attributes.maxLife), *unit.life + life);
    if (unit.mana) *unit.mana = std::min(float(unit.stats.attributes.maxMana), *unit.mana + mana);
}
ResolvedDamage Simulation::resolveIncoming(EntityId attacker, const CombatUnit &defender, float amount, MonsterDamageType type) {
    if (!defender.stats.resolved) { state_.message = "Original combat attributes are unavailable"; return {}; }
    amount = incomingDamage(attacker, defender.id, amount);
    if (defender.player && defender.mana && type != MonsterDamageType::Poison && resolveUnitSkill_)
        amount = skills().absorbEnergyShield(defender.id, amount);
    if (!defender.stats.monsterResistanceRules) return mitigatePlayerDamage(amount, type, defender.stats.attributes);
    int resistance = rawResistance(defender.stats.attributes, type);
    if (type == MonsterDamageType::Physical && resistance > 0 && defender.stats.undead) {
        const auto source = combatUnit(attacker);
        if (source && source.effects->hasState(sanctuaryState_, state_.frame)) resistance = 0;
    }
    if (type == MonsterDamageType::Cold && resistance < 100 && coldPierce_) resistance -= coldPierce_(attacker);
    return {mitigateMonsterDamage(amount, resistance), 0};
}
void Simulation::blockUnit(EntityId defender) {
    auto target = combatUnit(defender);
    if (!target.alive() || !target.player || !target.records.player->equipment.shield) return;
    auto &player = *target.records.player;
    if (player.weaponAttack && player.weaponAttack->skill && !player.weaponAttack->skill->weapon->interruptible) return;
    const auto *weapon = attackWeapon(false, false);
    const auto timing = weapon && attackTiming_ ? attackTiming_(*weapon, false, false, "bl") : std::nullopt;
    if (!timing) return;
    player.pendingCast.reset(); skills().stopChannel(skillCaster(player.id));
    player.castTime = player.meleeTime = 0;
    player.weaponAttack.reset(); player.charge.reset(); player.approachSkill.reset();
    player.route.clear(); player.attackTarget = {}; player.attackPosition.reset();
    player.blockAnimation = WeaponAttackState{{}, {}, player.pos, *timing, false};
}
void Simulation::recoverUnit(EntityId defender, EntityId attacker, float damage, bool elemental, int baseHitClass, bool forced) {
    auto target = combatUnit(defender);
    if (!target.alive() || damage <= 0) return;
    if (target.monster) {
        auto &monster = *target.records.monster;
        monster.hitDisplay = 4.f / 25.f;
        if (monster.freezeActive || monster.combatEffects.hasState(uninterruptableState_, state_.frame)) return;
        monster.aiRetaliate = true;
        const auto source = combatUnit(attacker);
        const int hitClass = (baseHitClass >= 0 ? baseHitClass : source.monster && monsterHitProperties_
            ? monsterHitProperties_(*source.records.monster).first : elemental ? 13 : 0) & 15;
        const int divisor = hitClass == 2 || hitClass == 6 || hitClass == 10 || hitClass == 11 ? 8 :
                            hitClass == 5 ? 64 : hitClass == 4 || hitClass == 8 ? 32 : 16;
        const int dealt = int(damage * 256.f), maximum = int(monster.maxHp * 256.f);
        if (!forced && monster.stun <= 0 && (dealt < 256 || dealt < maximum / divisor ||
            (dealt < maximum / (divisor / 2) && !(rollRandom(monster.combatRandom) & 1)) ||
            (dealt < maximum / (divisor / 4) && !(rollRandom(monster.combatRandom) & 3)))) {
            if (monster.hitFlash <= 0) triggerMonsterLightning(monster);
            return;
        }
        const auto duration = monsterGetHitDuration_ ? monsterGetHitDuration_(monster.identity) : std::nullopt;
        if (!duration || *duration <= 0) {
            if (monster.hitFlash <= 0) triggerMonsterLightning(monster);
            return;
        }
        const int recovery = std::max(0, target.stats.attributes.combat.fasterHitRecovery);
        const int faster = recovery > 0 ? 120 * recovery / (120 + recovery) : 0;
        monster.hitFlash = monster.hitRecoveryDuration = *duration * 100.f / float(50 + faster);
        monsterStopApproach(monster);
        monster.route.clear();
        monster.aiEscaping = monster.aiCircling = monster.aiRunning = false;
        monster.attack = monster.attackDuration = 0;
        monster.attackImpact = -1;
        monster.teleportTarget.reset();
        monster.nestSpawnPosition.reset();
        if (monster.kind == MonsterKind::Andariel) monster.skillPosition.reset();
        monster.skill2Remaining = monster.skill2Duration = 0;
        monster.aiCorpse = {};
        if (monster.enchantment && monster.enchantment->has(17))
            monster.pendingUniqueLightningFrame = state_.frame + 2;
        emit(EnemyHit{defender, monster.kind});
    } else if (target.hireling) {
        const auto source = combatUnit(attacker);
        const int hitClass = (baseHitClass >= 0 ? baseHitClass : source.monster && monsterHitProperties_
            ? monsterHitProperties_(*source.records.monster).first : elemental ? 13 : 0) & 15;
        recoverHireling(damage, hitClass);
    } else if (target.player) {
        if (target.records.player->weaponAttack && target.records.player->weaponAttack->skill &&
            target.records.player->weaponAttack->skill->weapon && !target.records.player->weaponAttack->skill->weapon->interruptible) return;
        const int chance = target.stats.attributes.combat.concentrationChance;
        if (chance > 0 && (target.records.player->meleeTime > 0 || target.records.player->castTime > 0 || target.records.player->channelSkill() >= 0) &&
            limitedRandom(*target.random, 100) < unsigned(chance)) return;
        target.records.player->hitTime = .16f;
    }
}
float Simulation::dealDamage(const DamageRequest &request) {
    auto target = combatUnit(request.defender);
    if (!target.alive() || !target.identity.attackable ||
        (request.permission == DamagePermission::Hostile && !canAttack(request.attacker, request.defender)) ||
        (request.permission == DamagePermission::Environment && safeZone_)) return 0;
    float amount = 0, absorbed = 0;
    auto channels = request.channels;
    channels[size_t(request.type)] += request.amount;
    constexpr MonsterDamageType order[]{MonsterDamageType::Physical, MonsterDamageType::Fire,
        MonsterDamageType::Lightning, MonsterDamageType::Cold, MonsterDamageType::Magic, MonsterDamageType::Poison};
    for (const auto type : order) {
        float value = channels[size_t(type)];
        if (value <= 0) continue;
        if (!request.mitigated) {
            const auto damage = resolveIncoming(request.attacker, target, value, type);
            value = damage.dealt; absorbed += damage.absorbed;
        }
        amount += value;
    }
    // SUnitDmg applies cold/freeze before the alive -> dead transition. In
    // particular a lethal freezing hit must retain its corpse state flags.
    if (request.freezeFrames > 0) applyMissileFreeze(request.attacker, target, request.freezeFrames);
    else if ((amount > 0 || absorbed > 0) && request.chill > 0)
        applyChill(target.id, request.chill, request.freeze);
    if (amount <= 0 && absorbed <= 0) return 0;
    *target.life = std::min(float(target.stats.attributes.maxLife), *target.life + absorbed);
    const float dealt = std::min(*target.life, amount);
    *target.life = std::max(0.f, *target.life - amount);
    if (target.monster) onMonsterDamaged(*target.records.monster, request, dealt);
    else if (target.hireling) {
        auto &merc = *target.records.hireling;
        if (!target.alive()) {
            merc.attack.reset(); merc.attackTimer = 0; merc.route.clear(); merc.moving = false;
            merc.hitTime = 0; merc.healing.clear(); merc.chill = merc.poisonRemaining = merc.poisonPerSecond = 0;
            merc.combatEffects.onDeath(EffectUnitKind::Monster);
            merc.corpseRegion = state_.area.region; merc.corpseVisible = true; merc.deathAge = 0;
        }
    }
    const bool purePoison = channels[size_t(MonsterDamageType::Poison)] > 0 &&
        std::none_of(channels.begin(), channels.end() - 1, [](float value) { return value > 0; });
    if (request.softHit && target.alive() && target.monster && dealt > 0) {
        target.records.monster->hitDisplay = 4.f / 25.f;
        if (target.records.monster->hitFlash <= 0) triggerMonsterLightning(*target.records.monster);
    }
    if (request.hitRecovery && !purePoison)
        recoverUnit(target.id, request.attacker, dealt,
            request.type != MonsterDamageType::Physical ||
            std::any_of(request.channels.begin() + 1, request.channels.end(), [](float value) { return value > 0; }), request.hitClass);
    if (!target.alive()) {
        emit(UnitDied{target.id, target.monster && target.records.monster->deathShattered,
                     *target.position, target.stats.collisionSize});
        const auto attacker = combatUnit(request.attacker);
        if (attacker) restoreUnit(attacker.id, float(attacker.stats.attributes.combat.lifeOnKill),
                                  float(attacker.stats.attributes.combat.manaOnKill));
    }
    return dealt;
}
void Simulation::applyChill(EntityId defender, float duration, bool freeze) {
    auto target = combatUnit(defender);
    if (!target.alive() || duration <= 0 || target.stats.attributes.combat.cannotBeFrozen ||
        (unitColdEffect_ && unitColdEffect_(target) == 0)) return;
    if (freeze && target.monster && target.effects->hasState(uninterruptableState_, state_.frame)) return;
    if (freeze && target.monster && target.stats.freezable) {
        if (unitColdEffect_ && unitColdEffect_(target) >= 0) return;
        const int frames = int(duration * 25.f + .00001f);
        if (frames <= 0) return;
        target.records.monster->freezeActive = true;
        target.records.monster->freeze = std::max(target.records.monster->freeze, float(frames / monsterFreezeDivisor_) / 25.f);
        target.records.monster->route.clear();
    } else {
        int frames = int(duration * 25.f + .00001f);
        if (frames <= 0) return;
        if ((target.monster || target.hireling) && unitColdEffect_ && unitColdEffect_(target) < 0)
            frames /= std::max(1, monsterColdDivisor_);
        *target.chill = std::max(*target.chill, float(std::max(1, frames)) / 25.f);
    }
}
void Simulation::applyWeb(EntityId defender, float duration, int percent) {
    auto unit = combatUnit(defender);
    if (!unit.alive()) return;
    auto apply = [&](auto &record) { record.webSlowRemaining = std::max(record.webSlowRemaining, duration); record.webSlowPercent = percent; };
    if (unit.player) apply(*unit.records.player);
    else if (unit.hireling) apply(*unit.records.hireling);
    else if (unit.monster) apply(*unit.records.monster);
}
std::optional<std::pair<EntityId, float>> Simulation::missileTarget(const Missile &missile, Vec to) {
    const auto rule = missileCollisions_.find(missile.missileId);
    if (rule == missileCollisions_.end()) return std::nullopt;
    std::optional<std::pair<EntityId, float>> hit;
    for (auto target : combatUnits()) {
        if (!target.alive() || !canAttack(missile.owner, target.id) || missile.lastHit == target.id || !active(*target.position)) continue;
        const auto delayed = state_.area.novaHitUntil.find(target.id);
        if (missile.nextHitDelay && delayed != state_.area.novaHitUntil.end() && delayed->second > state_.time) continue;
        if (auto at = missileUnitIntersection(missile.pos, to, rule->second.size, *target.position, target.stats.collisionSize);
            at && (!hit || *at < hit->second)) hit = std::pair{target.id, *at};
    }
    return hit;
}
} // namespace d2x
