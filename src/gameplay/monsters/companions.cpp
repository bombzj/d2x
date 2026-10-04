#include "gameplay/units/movement.hpp"
#include "gameplay/units/actions.hpp"
#include "gameplay/units/resources.hpp"
#include "gameplay/units/impairments.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/skills/projectile_source.hpp"
#include "core/random.hpp"
#include "gameplay/skills/necro_summon_spec.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
bool Simulation::summonHydra(EntityId ownerId, const SkillCastSpec &skill, Vec target) {
    const auto owner = combatUnit(ownerId);
    if (!owner.alive() || safeZone_ || !skills().blizzardTargetClear(*owner.position, target)) return false;
    constexpr MonsterKind kinds[]{MonsterKind::Hydra1, MonsterKind::Hydra2, MonsterKind::Hydra3};
    constexpr Vec offsets[]{{-1,-1}, {0,0}, {1,-1}};
    bool created = false;
    for (int head = 0; head < 3; ++head) {
        Enemy pet;
        pet.kind = kinds[head];
        pet.identity.monster = "hydra" + std::to_string(head + 1);
        pet.identity.origin = SpawnOrigin::Summoned;
        pet.pos = Vec{std::floor(target.x) + .5f, std::floor(target.y) + .5f} + offsets[head];
        if (!grid_->missileSegment(pet.pos, pet.pos, {5, 1})) continue;
        const auto attack = monsterAttackTiming_ ? monsterAttackTiming_(pet, 1) : std::nullopt;
        const auto rise = monsterSkill2Duration_ ? monsterSkill2Duration_(pet) : std::nullopt;
        if (!attack || !rise) continue;
        pet.id = ids_.allocate(); pet.combatRandom = childRandom(unitRandom_);
        pet.identity.spawnKey = "pet." + std::to_string(pet.id.value);
        pet.allegiance = {owner.identity.faction, owner.id, owner.identity.party, CombatRole::Summon, false};
        pet.summonSkill = skill.sourceId; pet.summonRank = skill.rank;
        pet.intrinsicCombat.emplace(); pet.intrinsicCombat->collisionSize = 0;
        pet.hydra = Enemy::HydraState{skill, state_.area.region, state_.frame + EffectFrame(skill.hydraFrames), true};
        pet.skill2Remaining = pet.skill2Duration = *rise;
        state_.companions.push_back(std::move(pet)); created = true;
    }
    int count = 0;
    for (const auto &pet : state_.companions) if (pet.hydra && pet.hydra->active && pet.allegiance.owner == ownerId) ++count;
    for (auto &pet : state_.companions) {
        if (count <= skill.hydraLimit) break;
        if (!pet.hydra || !pet.hydra->active || pet.allegiance.owner != ownerId) continue;
        pet.hydra->active = false; pet.deathAge = 0; --count;
    }
    return created;
}
void Simulation::advanceHydra(Enemy &pet, float dt) {
    auto &hydra = *pet.hydra;
    if (!hydra.active) { pet.deathAge += dt; return; }
    if (state_.frame > hydra.expiresAt || !petOwnerRetained(pet.allegiance.owner)) {
        hydra.active = false; pet.deathAge = 0; pet.attack = 0; return;
    }
    if (hydra.region != state_.area.region || safeZone_) return;
    if (pet.skill2Remaining > 0) { pet.skill2Remaining = std::max(0.f, pet.skill2Remaining - dt); return; }
    if (pet.attack > 0) {
        if (advanceTimedAction({pet.attack, pet.attackDuration, pet.attackImpact}, dt, 0.f, .00001f)) {
            const auto target = combatUnit(pet.combatTarget);
            if (target.alive() && canAttack(pet.id, target.id))
                skills().launchHydraBolt({pet.id, pet.pos, {}, pet.combatRandom}, hydra.skill, *target.position);
        }
        return;
    }
    pet.rethink = std::max(0.f, pet.rethink - dt);
    if (pet.rethink > 0) return;
    pet.combatTarget = {};
    float closest = 25;
    for (const auto &target : combatUnits()) {
        if (!target.alive() || !canAttack(pet.id, target.id) || !active(*target.position)) continue;
        const float distance = float(missileDistance(pet.pos, *target.position));
        if (distance < closest && grid_->missileSegment(pet.pos, *target.position, {0x04, 1})) {
            closest = distance; pet.combatTarget = target.id;
        }
    }
    if (pet.combatTarget && limitedRandom(pet.combatRandom, 100) < 60) {
        const auto timing = monsterAttackTiming_(pet, 1);
        if (timing) {
            pet.attack = pet.attackDuration = timing->duration;
            pet.attackImpact = timing->impact; pet.attackMode = 1;
            return;
        }
    }
    pet.rethink = 10.f / 25.f;
}
bool Simulation::usableCorpse(EntityId id, bool explosion) const {
    const auto *corpse = const_cast<Simulation *>(this)->findEnemy(id);
    if (!corpse || safeZone_ || !corpse->corpseAvailable() || !active(corpse->pos) ||
        (explosion ? !corpseExplosionLife_ || !corpseExplosionLife_(*corpse) : !corpseSelectable_ || !corpseSelectable_(*corpse))) return false;
    const auto duration = monsterDeathDuration_ ? monsterDeathDuration_(*corpse) : std::nullopt;
    return duration && corpse->deathAge >= *duration;
}
EntityId Simulation::corpseNear(Vec target, bool explosion) const {
    EntityId result;
    float closest = 3; // Mouse/world selection tolerance, not a skill damage/range parameter.
    for (const auto &corpse : state_.area.enemies) {
        const auto distance = (corpse.pos - target).length();
        if (distance < closest && usableCorpse(corpse.id, explosion)) { result = corpse.id; closest = distance; }
    }
    return result;
}
bool Simulation::petOwnerRetained(EntityId id) {
    const auto owner = combatUnit(id);
    // An owner in player DT remains registered until its animation-end event.
    // This also covers the lethal simulation step before PlayerDied is emitted.
    return owner.alive() || (owner.player && !owner.records.player->actions.deathCompleted);
}
void Simulation::finishCompanionDeath(Enemy &pet) {
    if (!pet.living()) return;
    const int collisionSize = combatUnit(pet.id).stats.collisionSize;
    pet.hp = 0;
    if (pet.hydra) pet.hydra->active = false;
    pet.noTreasure = true;
    finishMonsterDeath(pet, {});
    emit(UnitDied{pet.id, pet.deathShattered, pet.pos, collisionSize});
}
void Simulation::enforceSummonLimit(EntityId owner, int skill, int limit) {
    auto sameType = [&](const Enemy &pet) {
        const bool golem = pet.necroPet && pet.necroPet->spec->kind != NecroSummonKind::Mage && pet.necroPet->spec->kind != NecroSummonKind::Revive;
        const auto found = std::find_if(state_.companions.begin(), state_.companions.end(), [&](const Enemy &candidate) { return candidate.summonSkill == skill && candidate.necroPet; });
        const bool requestedGolem = found != state_.companions.end() && found->necroPet->spec->kind != NecroSummonKind::Mage && found->necroPet->spec->kind != NecroSummonKind::Revive;
        return requestedGolem ? golem : pet.summonSkill == skill;
    };
    int count = 0;
    for (const auto &pet : state_.companions)
        if (pet.living() && pet.allegiance.owner == owner && sameType(pet)) ++count;
    // Native pet lists remove the oldest summon when the per-type limit is exceeded.
    for (auto &pet : state_.companions) {
        if (count <= limit) break;
        if (!pet.living() || pet.allegiance.owner != owner || !sameType(pet)) continue;
        finishCompanionDeath(pet);
        --count;
    }
}
bool Simulation::summonFromCorpse(EntityId ownerId, const SkillCastSpec &skill, EntityId corpseId) {
    const auto owner = combatUnit(ownerId);
    if (!skill.summon || !owner.alive() || !usableCorpse(corpseId)) return false;
    auto *corpse = findEnemy(corpseId);
    return summonPet(ownerId, skill, corpse->pos, corpseId);
}
bool Simulation::summonPet(EntityId ownerId, const SkillCastSpec &skill, Vec position, EntityId corpseId) {
    const auto owner = combatUnit(ownerId);
    if (!skill.summon || !owner.alive() || !active(position)) return false;
    const auto &spec = *skill.summon;
    Enemy pet;
    pet.kind = spec.kind;
    pet.identity.monster = spec.monster;
    pet.identity.origin = SpawnOrigin::Summoned;
    pet.pos = position;
    pet.intrinsicCombat = spec.stats;
    pet.hp = pet.maxHp = float(spec.stats.attributes.maxLife);
    pet.allegiance = {owner.identity.faction, owner.id, owner.identity.party, CombatRole::Summon};
    pet.summonSkill = skill.sourceId; pet.summonRank = skill.rank;
    const auto attack = monsterAttackTiming_ ? monsterAttackTiming_(pet, 1) : std::nullopt;
    const auto rise = monsterResurrectionDuration_ ? monsterResurrectionDuration_(pet) : std::nullopt;
    if (!spec.amazon && (!attack || !rise || *rise <= 0)) {
        state_.message = "Original summon animation is unavailable"; return false;
    }
    auto clear = [&](Vec position) {
        if (!grid_->walkable(position, movementRule(pet))) return false;
        for (auto unit : combatUnits())
            if (unit.alive() && missileDistance(position, *unit.position) < spec.stats.collisionSize) return false;
        return true;
    };
    bool placed = clear(pet.pos);
    for (int radius = 1; !placed && radius <= 4; ++radius)
        for (int y = -radius; y <= radius && !placed; ++y)
            for (int x = -radius; x <= radius && !placed; ++x) {
                if (std::abs(x) != radius && std::abs(y) != radius) continue;
                const Vec candidate = position + Vec{float(x), float(y)};
                if (clear(candidate) && grid_->segment(position, candidate, {}, movementRule(pet))) { pet.pos = candidate; placed = true; }
            }
    if (!placed) { state_.message = "No clear ground for this summon"; return false; }
    pet.id = ids_.allocate(); pet.combatRandom = childRandom(unitRandom_);
    pet.identity.spawnKey = "pet." + std::to_string(pet.id.value);
    // Monster.cpp chooses components; SkillNec fixes the base body/weapon and
    // occasionally resets SH to component 0. Remaining shields retain that draw.
    pet.summonShield = int(limitedRandom(pet.combatRandom, unsigned(std::max(1, spec.shieldVariants))));
    if (spec.necro && spec.necro->kind == NecroSummonKind::Mage) pet.summonShield = int(limitedRandom(*owner.random, 4));
    if (spec.shieldChance > 0 && limitedRandom(*owner.random, 100) < unsigned(spec.shieldChance)) pet.summonShield = 0;
    pet.resurrectionRemaining = pet.resurrectionDuration = spec.amazon ? 0.f : *rise;
    if (spec.amazon) {
        pet.noTreasure = true;
        pet.amazonPet = std::make_shared<AmazonPetState>(); pet.amazonPet->spec = spec.amazon;
        pet.amazonPet->region = state_.area.region;
        if (!spec.amazon->decoy) {
            const int life = spec.amazon->lifeMinimum + int(limitedRandom(pet.combatRandom,
                unsigned(spec.amazon->lifeMaximum - spec.amazon->lifeMinimum + 1)));
            pet.intrinsicCombat->attributes.maxLife = life * (100 + spec.amazon->lifePercent) / 100;
            pet.hp = pet.maxHp = float(pet.intrinsicCombat->attributes.maxLife);
            pet.rethink = 20.f / 25.f;
        }
        if (spec.amazon->lifetimeFrames > 0) pet.amazonPet->expiresAt = state_.frame + EffectFrame(spec.amazon->lifetimeFrames);
        CombatEffectSpec state; state.state = spec.amazon->state;
        state.source = {CombatEffectSource::Skill, owner.id, skill.sourceId, skill.rank};
        state.visual.overlayId = spec.amazon->stateOverlay;
        state.modifiers.combat.defensePercent = pet.intrinsicCombat->attributes.combat.defensePercent;
        pet.intrinsicCombat->attributes.combat.defensePercent = 0;
        pet.combatEffects.apply(std::move(state), state_.frame);
        if (spec.amazon->appearOverlay >= 0)
            state_.area.effects.push_back({pet.pos, 0, spec.amazon->appearDuration, -1, spec.amazon->appearOverlay, pet.id});
    }
    if (auto *corpse = findEnemy(corpseId)) corpse->corpseConsumed = true;
    if (spec.necro) {
        pet.necroPet = std::make_shared<NecroPetState>(); pet.necroPet->spec = spec.necro;
        CombatEffectSpec passive;
        passive.stacking = EffectStacking::ReplaceSource;
        passive.source = {CombatEffectSource::Skill, pet.id, skill.sourceId, skill.rank};
        passive.modifiers.velocityPercent = spec.necro->velocityPercent;
        if (spec.necro->slowPercent > 0) {
            const SlowOther slow{spec.necro->slowPercent, spec.necro->slowState};
            passive.reactions.push_back({CombatEffectEvent::DamagedInMelee, slow});
            passive.reactions.push_back({CombatEffectEvent::DealtMeleeDamage, slow});
        }
        pet.combatEffects.apply(std::move(passive), state_.frame);
    }
    state_.companions.push_back(std::move(pet));
    enforceSummonLimit(owner.id, skill.sourceId, spec.limit);
    return true;
}
void Simulation::relocateCompanions(EntityId owner, Vec destination, EntityId only) {
    for (auto &pet : state_.companions) {
        if (pet.hydra) continue;
        if (pet.allegiance.owner != owner || (only && pet.id != only)) continue;
        if (pet.amazonPet && !pet.amazonPet->spec->warp) continue;
        if (pet.hp <= 0) {
            if (!only) pet.deathAge = std::max(pet.deathAge,
                monsterDeathDuration_ ? monsterDeathDuration_(pet).value_or(0.f) : 0.f);
            continue;
        }
        Vec point = destination;
        bool placed = false;
        for (int radius = 2; radius <= 8 && !placed; ++radius)
            for (int y = -radius; y <= radius && !placed; ++y)
                for (int x = -radius; x <= radius && !placed; ++x) {
                    if (std::abs(x) != radius && std::abs(y) != radius) continue;
                    const Vec candidate = destination + Vec{float(x), float(y)};
                    if (!grid_->walkable(candidate, movementRule(pet)) || !grid_->segment(destination, candidate, {}, movementRule(pet))) continue;
                    bool occupied = false;
                    for (auto unit : combatUnits())
                        if (unit.id != pet.id && unit.alive() && missileDistance(candidate, *unit.position) < 2) { occupied = true; break; }
                    if (!occupied) { point = candidate; placed = true; }
                }
        // Narrow exits can temporarily share the owner's tile, as native pet warp does.
        pet.pos = point; pet.route.clear(); pet.combatTarget = {};
        pet.attack = pet.attackDuration = 0; pet.attackImpact = -1; pet.rethink = 0;
    }
}
void Simulation::updateCompanions(float dt) {
    for (auto &pet : state_.companions) {
        if (pet.hydra) { advanceHydra(pet, dt); continue; }
        if (pet.hp <= 0) {
            if (pet.necroPet && pet.necroPet->explosionAt && state_.frame >= *pet.necroPet->explosionAt) {
                pet.necroPet->explosionAt.reset();
                if (!safeZone_) {
                    for (auto target : combatUnits()) {
                        const float x = std::floor(target.position->x)-std::floor(pet.pos.x), y = std::floor(target.position->y)-std::floor(pet.pos.y);
                        if (!target.alive() || !canAttack(pet.id,target.id) || x*x+y*y > 36) continue;
                        DamageRequest explosion{pet.id,target.id,100,DamageType::Physical}; explosion.channels[size_t(DamageType::Fire)] = 100;
                        dealDamage(explosion);
                    }
                    const auto &spec = *pet.necroPet->spec;
                    state_.area.effects.push_back({pet.pos,0,spec.explosionDuration,spec.explosionId});
                    emit(MissileReleased{spec.explosionId});
                }
            }
            pet.deathAge += dt; continue;
        }
        if (!pet.intrinsicCombat) continue;
        auto owner = combatUnit(pet.allegiance.owner);
        if (!petOwnerRetained(pet.allegiance.owner) || (pet.necroPet && pet.necroPet->expiresAt && state_.frame >= *pet.necroPet->expiresAt)) {
            finishCompanionDeath(pet);
            continue;
        }
        pet.combatEffects.expire(state_.frame);
        if (advanceAuraKnockback(pet, dt)) continue;
        advanceImpairments({&pet.chill, &pet.freeze, &pet.stun, &pet.freezeActive,
                           {&pet.webSlowRemaining, &pet.webSlowPercent}},
                          dt, WebSlowExpiry::RetainMetadata);
        pet.rethink = std::max(0.f, pet.rethink - dt);
        if (pet.poisonRemaining <= 0 && pet.openWoundsRemaining <= 0)
            advanceLifeRegeneration(pet.hp, pet.maxHp, pet.intrinsicCombat->damageRegen,
                                    dt, LifeRegenOrder::FrameRateThenStep);
        if (pet.amazonPet && ((pet.amazonPet->expiresAt && state_.frame >= *pet.amazonPet->expiresAt) ||
            (!pet.amazonPet->spec->warp && pet.amazonPet->region != state_.area.region))) {
            finishCompanionDeath(pet); continue;
        }
        if (pet.amazonPet && pet.amazonPet->spec->decoy) { pet.route.clear(); pet.attack = 0; continue; }
        if (pet.resurrectionRemaining > 0) { pet.resurrectionRemaining = std::max(0.f, pet.resurrectionRemaining - dt); continue; }
        if (pet.hitFlash > 0 || pet.freeze > 0 || pet.stun > 0) {
            pet.attack = pet.attackDuration = 0; pet.attackImpact = -1; pet.route.clear(); continue;
        }
        const int ownerDistance = std::max(0, missileDistance(pet.pos, *owner.position) - 2);
        if (ownerDistance > 50) { relocateCompanions(owner.id, *owner.position, pet.id); continue; }
        if (pet.attack > 0) {
            if (advanceTimedAction({pet.attack, pet.attackDuration, pet.attackImpact}, dt, 0.f, .00001f)) {
                if (pet.necroPet && pet.necroPet->spec->kind == NecroSummonKind::Mage) {
                    const auto target = combatUnit(pet.combatTarget);
                    if (target.alive() && canAttack(pet.id, target.id)) {
                        const auto &payload = pet.necroPet->spec->missiles.at(size_t(pet.summonShield));
                        const float minimum = payload.minimumDamage * 256.f, maximum = payload.maximumDamage * 256.f;
                        const float damage = (minimum + float(limitedRandom(pet.combatRandom, unsigned(std::max(0.f, maximum-minimum))))) / 256.f;
                        Missile missile{{}, pet.id, pet.pos, (*target.position-pet.pos).unit()*payload.velocity, payload.lifetime, SkillBehavior::None, false, payload.id};
                        missile.fixedElement = payload.element; missile.impact = payload.impact;
                        missile.impactDamage.channels[size_t(payload.element)] = payload.element == DamageType::Poison ? damage*25.f : damage;
                        if (payload.element == DamageType::Poison) missile.impactDamage.poisonDuration = payload.duration;
                        if (payload.element == DamageType::Cold) missile.impactDamage.coldDuration = payload.duration;
                        skills().launchStraight(std::move(missile)); emit(MissileReleased{payload.id});
                    }
                } else if (monsterProjectile_ && monsterProjectile_(pet, 1)) launchMonsterProjectile(pet);
                else resolveMonsterAttack(pet);
            }
            continue;
        }
        if (pet.rethink <= 0) {
            pet.combatTarget = ownerDistance > 28 ? EntityId{} : chooseTarget(pet.id, 24);
            if (owner.player && owner.records.player->actions.attackTarget && canAttack(pet.id, owner.records.player->actions.attackTarget)) {
                const auto target = combatUnit(owner.records.player->actions.attackTarget);
                if (target.alive() && missileDistance(pet.pos, *target.position) < 36) pet.combatTarget = target.id;
            }
            pet.rethink = float(pet.amazonPet ? pet.amazonPet->spec->thinkFrames : 10) / 25.f;
            auto target = combatUnit(pet.combatTarget);
            const bool ranged = pet.necroPet && pet.necroPet->spec->kind == NecroSummonKind::Mage;
            if (target.alive() && (ranged ? missileDistance(pet.pos, *target.position) < 20 :
                meleeDistance(pet.pos, pet.intrinsicCombat->collisionSize, *target.position, target.stats.collisionSize) <= 1 + (pet.amazonPet ? pet.amazonPet->weapon.rangeAdder : 0)) &&
                (ranged ? grid_->missileSegment(pet.pos, *target.position, {4, 1}) : grid_->segment(pet.pos, *target.position))) {
                pet.route.clear();
                if (limitedRandom(pet.combatRandom, 100) < unsigned(pet.amazonPet ? pet.amazonPet->spec->attackChance : 80)) beginMonsterAttack(pet, 1);
                continue;
            }
            if (target.alive()) pet.route = grid_->path(pet.pos, *target.position, false, movementRule(pet));
            else if (ownerDistance > 4) pet.route = grid_->path(pet.pos, *owner.position, false, movementRule(pet));
            else pet.route.clear();
        }
        if (pet.route.empty()) continue;
        discardReachedWaypoints(pet.pos, pet.route, .05f);
        if (pet.route.empty()) continue;
        int velocity = 75;
        if (ownerDistance > 28) velocity += pet.intrinsicCombat->followVelocityBonus;
        pet.movementVelocityPercent = velocity;
        const auto speed = monsterMoveSpeed_ ? monsterMoveSpeed_(pet, velocity) : std::nullopt;
        if (!speed) { pet.route.clear(); continue; }
        const Vec delta = pet.route.front() - pet.pos;
        const auto neighbors = combatUnits();
        auto clear = [&](Vec point) {
            if (!grid_->segment(pet.pos, point, {}, movementRule(pet))) return false;
            for (const auto &unit : neighbors)
                if (unit.id != pet.id && unit.alive() && missileDistance(point, *unit.position) < 2 &&
                    (point - *unit.position).length() < (pet.pos - *unit.position).length()) return false;
            return true;
        };
        if (!advanceMovement(pet.pos, delta.unit(), std::min(delta.length(), *speed * dt),
            [&](Vec, Vec to) { return clear(to); }).accepted) {
            // The world's static pathfinder has no dynamic unit occupancy. Steer
            // around a blocked next step, then let it replan from that position.
            const auto heading = delta.unit();
            const Vec side{-heading.y, heading.x};
            const float step = *speed * dt;
            const Vec left = pet.pos + (heading + side).unit() * step;
            const Vec right = pet.pos + (heading - side).unit() * step;
            if (clear(left)) pet.pos = left;
            else if (clear(right)) pet.pos = right;
            else if (clear(pet.pos + side * step)) pet.pos = pet.pos + side * step;
            else if (clear(pet.pos - side * step)) pet.pos = pet.pos - side * step;
            pet.route.clear(); pet.rethink = 0;
        }
    }
}
} // namespace d2x
