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
    const auto [root, identity] = rootIdentity(state_, id);
    return identity.role == CombatRole::Player ? root : EntityId{};
}
Relation Simulation::relation(EntityId first, EntityId second) const {
    if (!first || !second) return Relation::Neutral;
    if (first == second) return Relation::Allied;
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
CombatUnit Simulation::combatUnit(EntityId id) {
    CombatUnit unit;
    unit.id = id;
    auto &p = state_.player;
    auto bind = [&](auto &record) {
        unit.position = &record.pos; unit.life = &record.hp; unit.chill = &record.chill;
        unit.poisonRate = &record.poisonPerSecond; unit.poisonTime = &record.poisonRemaining;
        unit.random = &record.combatRandom; unit.effects = &record.combatEffects;
        unit.identity = record.allegiance;
    };
    if (id && id == p.id) {
        bind(p); unit.player = &p; unit.mana = &p.mana;
        unit.stats.attributes = state_.player.attributes; unit.stats.level = p.level;
        unit.stats.block = state_.player.equipment.blockChance;
        if (p.runningNow && p.moving) { unit.stats.attributes.defense = 0; unit.stats.block /= 3; }
    } else if (id && id == p.hireling.id && p.hireling.sourceRow >= 0 && hirelingAttributes_) {
        auto &merc = p.hireling;
        bind(merc); unit.hireling = &merc; unit.identity.owner = p.id;
        unit.stats.attributes = hirelingAttributes_(); unit.stats.level = merc.level;
        unit.stats.collisionSize = merc.collisionSize;
    } else if (auto *monster = findEnemy(id)) {
        bind(*monster); unit.monster = monster;
        if (monster->intrinsicCombat) unit.stats = *monster->intrinsicCombat;
        else {
            auto &stats = unit.stats;
            stats.monsterResistanceRules = true;
            stats.attributes.maxLife = int(monster->maxHp);
            const auto defense = monsterDefense_ ? monsterDefense_(*monster, state_.area.region) : std::nullopt;
            stats.resolved = defense.has_value();
            if (auto value = defense) {
                stats.level = value->level; stats.attributes.defense = value->defense;
                stats.demon = value->demon; stats.undead = value->undead; stats.boss = value->boss;
            }
            stats.rank = monster->identity.rank;
            stats.collisionSize = monsterSize_ ? monsterSize_(*monster) : 2;
            stats.drain = monsterDrain_ ? monsterDrain_(*monster) : 0;
            stats.freezable = monsterFreezable_ && monsterFreezable_(*monster).value_or(false);
            stats.primeEvil = monsterHitProperties_ && monsterHitProperties_(*monster).second;
            if (monsterResistance_) {
                auto resistance = [&](MonsterDamageType type) {
                    const auto value = monsterResistance_(*monster, state_.area.region, type);
                    if (!value) stats.resolved = false;
                    return value.value_or(0);
                };
                stats.attributes.combat.physicalResist = resistance(MonsterDamageType::Physical);
                stats.attributes.combat.magicResist = resistance(MonsterDamageType::Magic);
                stats.attributes.fireResist = resistance(MonsterDamageType::Fire);
                stats.attributes.coldResist = resistance(MonsterDamageType::Cold);
                stats.attributes.lightningResist = resistance(MonsterDamageType::Lightning);
                stats.attributes.poisonResist = resistance(MonsterDamageType::Poison);
            }
        }
        const auto modifiers = monster->combatEffects.modifiers(state_.frame);
        if (monster->conversion && monster->conversion->level > monster->conversion->convertedLevel) {
            unit.stats.level = monster->conversion->convertedLevel;
            unit.stats.attributes.maxLife = std::max(1, int(monster->maxHp));
        }
        auto &stats = unit.stats.attributes;
        stats.defense = std::max(0, (stats.defense + modifiers.defense) * (100 + modifiers.combat.defensePercent) / 100);
        stats.fireResist += modifiers.fireResist; stats.coldResist += modifiers.coldResist;
        stats.lightningResist += modifiers.lightningResist; stats.poisonResist += modifiers.poisonResist;
        mergeCombatModifiers(stats.combat, modifiers.combat);
        if (monster->identity.enchantment && monster->identity.enchantment->has(38))
            stats.combat.curseResistance = 100;
    }
    return unit;
}
std::vector<CombatUnit> Simulation::combatUnits() {
    std::vector<CombatUnit> result;
    result.push_back(combatUnit(state_.player.id));
    if (auto merc = combatUnit(state_.player.hireling.id)) result.push_back(merc);
    for (auto &monster : state_.area.enemies) result.push_back(combatUnit(monster.id));
    for (auto &monster : state_.companions) result.push_back(combatUnit(monster.id));
    return result;
}
Vec Simulation::unitPosition(EntityId id) const {
    auto unit = const_cast<Simulation *>(this)->combatUnit(id);
    return unit ? *unit.position : Vec{};
}
Vec Simulation::monsterTargetPosition(const Enemy &enemy) const { return unitPosition(enemy.combatTarget); }
EntityId Simulation::chooseTarget(EntityId actor, float range) {
    auto source = combatUnit(actor);
    if (!source.alive()) return {};
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
int Simulation::unitResistance(const CombatUnit &unit, MonsterDamageType type) const {
    const auto &stats = unit.stats.attributes;
    switch (type) {
    case MonsterDamageType::Physical: return stats.combat.physicalResist;
    case MonsterDamageType::Magic: return stats.combat.magicResist;
    case MonsterDamageType::Fire: return stats.fireResist;
    case MonsterDamageType::Lightning: return stats.lightningResist;
    case MonsterDamageType::Cold: return stats.coldResist;
    case MonsterDamageType::Poison: return stats.poisonResist;
    }
    return 0;
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
    if (defender.player && defender.mana && type != MonsterDamageType::Poison && resolveMissileSkill_) {
        for (const auto &effect : defender.effects->entries()) {
            if (effect.spec.state.id != energyShieldState_ || !effect.activeAt(state_.frame)) continue;
            const auto shield = resolveMissileSkill_(defender.id, effect.spec.source.definition, effect.spec.source.level);
            int64_t mana = int64_t(*defender.mana * 256.f);
            const int64_t fixed = std::max<int64_t>(0, int64_t(amount * 256.f));
            const int64_t absorb = std::min(fixed * shield.shieldPercent / 100, mana * 16 / shield.shieldManaFactor);
            mana = std::max<int64_t>(0, mana - absorb * shield.shieldManaFactor / 16);
            amount = float(fixed - absorb) / 256.f;
            *defender.mana = float(mana) / 256.f;
            const auto handle = effect.handle;
            if (mana == 0) combatEffectsChanged(defender.effects->remove(handle));
            break;
        }
    }
    if (!defender.stats.monsterResistanceRules) return mitigatePlayerDamage(amount, type, defender.stats.attributes);
    int resistance = unitResistance(defender, type);
    if (type == MonsterDamageType::Physical && resistance > 0 && defender.stats.undead) {
        const auto source = combatUnit(attacker);
        if (source && source.effects->hasState(sanctuaryState_, state_.frame)) resistance = 0;
    }
    if (type == MonsterDamageType::Cold && resistance < 100 && coldPierce_) resistance -= coldPierce_(attacker);
    return {mitigateMonsterDamage(amount, resistance), 0};
}
void Simulation::blockUnit(EntityId defender) {
    auto target = combatUnit(defender);
    if (!target.alive() || !target.player || !target.player->equipment.shield) return;
    auto &player = *target.player;
    if (player.weaponAttack && player.weaponAttack->skill && !player.weaponAttack->skill->weapon->interruptible) return;
    const auto *weapon = attackWeapon(false, false);
    const auto timing = weapon && attackTiming_ ? attackTiming_(*weapon, false, false, "bl") : std::nullopt;
    if (!timing) return;
    player.pendingCast.reset(); stopChannel(player);
    player.castTime = player.meleeTime = 0;
    player.weaponAttack.reset(); player.charge.reset(); player.approachSkill.reset();
    player.route.clear(); player.attackTarget = {}; player.attackPosition.reset();
    player.blockAnimation = WeaponAttackState{{}, {}, player.pos, *timing, false};
}
void Simulation::recoverUnit(EntityId defender, EntityId attacker, float damage, bool elemental, int baseHitClass, bool forced) {
    auto target = combatUnit(defender);
    if (!target.alive() || damage <= 0) return;
    if (target.monster) {
        auto &monster = *target.monster;
        monster.hitDisplay = 4.f / 25.f;
        if (monster.freezeActive || monster.combatEffects.hasState(uninterruptableState_, state_.frame)) return;
        monster.aiRetaliate = true;
        const auto source = combatUnit(attacker);
        const int hitClass = (baseHitClass >= 0 ? baseHitClass : source.monster && monsterHitProperties_
            ? monsterHitProperties_(*source.monster).first : elemental ? 13 : 0) & 15;
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
        if (monster.identity.enchantment && monster.identity.enchantment->has(17))
            monster.pendingUniqueLightningFrame = state_.frame + 2;
        emit(EnemyHit{defender, monster.kind});
    } else if (target.hireling) {
        const auto source = combatUnit(attacker);
        const int hitClass = (baseHitClass >= 0 ? baseHitClass : source.monster && monsterHitProperties_
            ? monsterHitProperties_(*source.monster).first : elemental ? 13 : 0) & 15;
        recoverHireling(damage, hitClass);
    } else if (target.player) {
        if (target.player->weaponAttack && target.player->weaponAttack->skill &&
            target.player->weaponAttack->skill->weapon && !target.player->weaponAttack->skill->weapon->interruptible) return;
        const int chance = target.stats.attributes.combat.concentrationChance;
        if (chance > 0 && (target.player->meleeTime > 0 || target.player->castTime > 0 || target.player->channelSkill() >= 0) &&
            limitedRandom(*target.random, 100) < unsigned(chance)) return;
        target.player->hitTime = .16f;
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
    if (target.monster) onMonsterDamaged(*target.monster, request, dealt);
    else if (target.hireling) {
        auto &merc = *target.hireling;
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
        target.monster->hitDisplay = 4.f / 25.f;
        if (target.monster->hitFlash <= 0) triggerMonsterLightning(*target.monster);
    }
    if (request.hitRecovery && !purePoison)
        recoverUnit(target.id, request.attacker, dealt,
            request.type != MonsterDamageType::Physical ||
            std::any_of(request.channels.begin() + 1, request.channels.end(), [](float value) { return value > 0; }), request.hitClass);
    if (!target.alive()) {
        emit(UnitDied{target.id, target.monster && target.monster->deathShattered,
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
        target.monster->freezeActive = true;
        target.monster->freeze = std::max(target.monster->freeze, float(frames / monsterFreezeDivisor_) / 25.f);
        target.monster->route.clear();
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
    if (unit.player) apply(*unit.player);
    else if (unit.hireling) apply(*unit.hireling);
    else if (unit.monster) apply(*unit.monster);
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
