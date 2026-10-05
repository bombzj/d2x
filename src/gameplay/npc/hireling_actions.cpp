#include "gameplay/skills/behavior.hpp"
#include "gameplay/units/actions.hpp"
#include "gameplay/session/session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/rank_sources.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/amazon_magic_spec.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/missile_launch_spec.hpp"
#include "content/skills/aura_data.hpp"
#include "gameplay/skills/aura_owner.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/projectile_source.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "hireling_skill_state.hpp"
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace d2x {
std::map<int, int> GameSessionImpl::hirelingSkillRanks() const {
    std::map<int, int> ranks;
    const auto *definition = hirelingDefinition();
    if (!definition) return ranks;
    const int level = state().player.hireling.level;
    const int delta = level - definition->level;
    for (const auto &entry : definition->skills)
        if (level >= entry.requiredLevel)
            ranks[entry.id] = std::clamp(entry.level + ((delta * entry.levelPerLevel) >> 5), 0, 32);
    return ranks;
}
SkillCastSpec GameSessionImpl::resolveHirelingSkill(int id, int rank) const {
    const auto *entry = content_.skills.find(id);
    if (!entry || !entry->spell) throw std::runtime_error("Original hireling skill is unavailable");
    const auto ranks = hirelingSkillRanks();
    const auto &combat = hirelingStats().combat;
    if (!rank) {
        const auto found = ranks.find(id);
        rank = resolveSkillSourceRank({id, found == ranks.end() ? 0 : found->second, -1, 0, false}, {}, combat);
    }
    return resolveSkill(*entry->spell, {rank, ranks, 0,
        0, combat.coldSkillDamagePercent});
}
void GameSessionImpl::beginHirelingAttack(const MonsterRecord &actor, const HirelingCombatStats &stats,
    const MonsterAttackTiming &timing, EntityId target, Vec aim) {
    auto &merc = simulation_->state_.player.hireling;
    const auto *definition = hirelingDefinition();
    std::optional<SkillCastSpec> skill;
    if (definition->act >= 1 && definition->act <= 3) {
        const auto ranks = hirelingSkillRanks();
        const int delta = merc.level - definition->level;
        int total = definition->defaultChance;
        std::vector<std::pair<int, int>> chances;
        for (const auto &entry : definition->skills) {
            if (!ranks.contains(entry.id) || ranks.at(entry.id) <= 0) continue;
            const auto *record = content_.skills.find(entry.id);
            if (!record) throw std::runtime_error("Missing original hireling skill record");
            if (entry.aiType == 1 && record->spell && record->spell->state.id >= 0 &&
                merc.combatEffects.hasState(record->spell->state.id, state().frame)) continue;
            if (record->spell && record->spell->effect == SkillBehavior::Inferno &&
                missileDistance(merc.pos, aim) - 2 > resolveSkillSourceRank({entry.id, ranks.at(entry.id), -1, 0, false}, {}, stats.combat) / 2 + 4) continue;
            if (record->auraImplemented && merc.skills && merc.skills->aura && merc.skills->aura->definition.skill == entry.id) continue;
            total += entry.chance + delta * entry.chancePerLevel / 4;
            chances.emplace_back(entry.id, total);
        }
        const int roll = int(limitedRandom(merc.combatRandom, unsigned(total + 1)));
        if (roll >= definition->defaultChance)
            for (const auto &[id, chance] : chances) if (roll <= chance) {
                const auto *record = content_.skills.find(id);
                if (record->auraImplemented) {
                    const int rank = resolveSkillSourceRank({id, ranks.at(id), -1, 0, false}, {}, stats.combat);
                    const auto aura = resolveAura(content_, id, rank, ranks);
                    if (!aura) throw std::runtime_error("Original hireling aura is unavailable");
                    if (!merc.skills) merc.skills = std::make_shared<HirelingSkillState>();
                    const bool dead = false;
                    simulation_->skills().clearAuraIfChanged({merc.id, dead, merc.skills->aura}, id, rank);
                    simulation_->skills().prepareAura({merc.id, dead, merc.skills->aura}, *aura, record->auraImmediate);
                    merc.thinkTimer = 10.f / 25.f;
                    simulation_->emit(SkillCast{merc.id, id, merc.pos}); return;
                }
                skill = resolveHirelingSkill(id); break;
            }
    }
    if (merc.skills) merc.skills->infernoEnd = merc.skills->nextInfernoPulse = 0;
    if (!skill && definition->act == 3) {
        auto defender = simulation_->combatUnit(target);
        if (!defender.alive() || meleeDistance(merc.pos, merc.collisionSize, aim, defender.stats.collisionSize) > 1 + actor.meleeRange) {
            merc.thinkTimer = 10.f / 25.f; return;
        }
    }
    const bool casting = skill && definition->act == 3;
    const auto &actionTiming = casting && actor.hirelingCastTiming ? *actor.hirelingCastTiming : timing;
    if (casting && !actor.hirelingCastTiming) throw std::runtime_error("Missing original Iron Wolf cast timing");
    const int baseRate = int(std::lround(actionTiming.frames * 256.f / (actionTiming.duration * 25.f)));
    const int actionFrame = int(std::lround(actionTiming.impact * baseRate * 25.f / 256.f));
    const int cold = merc.chill > 0 ? actor.coldEffect.at(size_t(state().population.difficulty)) : 0;
    WeaponAttackState attack; attack.weapon = stats.weapon.item; attack.target = target; attack.aim = aim;
    const int castPercent = std::min(175, 100 + 120 * stats.combat.fasterCast / (120 + stats.combat.fasterCast));
    attack.timing = {casting ? "sc" : "a1", actionTiming.frames, casting ? std::max(1, baseRate * castPercent / 100) :
        effectiveAttackSpeed(baseRate, stats.weapon.fasterAttack, stats.weapon.baseSpeed, cold + stats.combat.attackRate), actionFrame, 0};
    if (skill && skill->weapon && skill->weapon->spear && actor.hirelingSequence) {
        attack.sequence = actor.hirelingSequence;
        attack.timing.frames = int(attack.sequence->frames.size());
    }
    attack.skill = std::move(skill);
    merc.route.clear(); merc.moving = false;
    merc.attackTimer = float(attack.timing.durationTicks()) / 25.f; merc.attack = std::move(attack);
}
void GameSessionImpl::advanceHirelingAttack(const MonsterRecord &actor, const HirelingCombatStats &stats) {
    auto &merc = simulation_->state_.player.hireling;
    auto &attack = *merc.attack;
    if (attack.skill && attack.skill->effect == SkillBehavior::Inferno && merc.skills && merc.skills->infernoEnd) {
        if (state().frame < merc.skills->infernoEnd) {
            if (state().frame >= merc.skills->nextInfernoPulse) {
                if (auto target = simulation_->combatUnit(attack.target); target.alive()) attack.aim = *target.position;
                merc.look = (attack.aim - merc.pos).unit();
                simulation_->skills().releaseUnitSpell({merc.id, merc.pos, merc.look, merc.combatRandom}, *attack.skill, attack.aim, attack.target);
                merc.skills->nextInfernoPulse += 2; // SkillSor monster MODECHANGE cadence.
            }
            merc.attackTimer = float(merc.skills->infernoEnd - state().frame + actor.infernoLength) / 25.f;
            return;
        }
        merc.skills->infernoEnd = 0;
        // Original ENDANIM recovery is in game frames, independent of FCR.
        attack.timing.speed = 256; attack.timing.frames = actor.infernoAnimation + actor.infernoLength + 1;
        attack.ticks = actor.infernoAnimation;
    }
    const bool release = advanceWeaponAction(attack);
    merc.attackTimer = weaponActionRemaining(attack);
    if (release) {
        if (attack.skill && (attack.skill->amazonMagic || hirelingDefinition()->act == 3)) {
            if (auto target = simulation_->combatUnit(attack.target); target.alive()) attack.aim = *target.position;
            merc.look = (attack.aim - merc.pos).unit();
            simulation_->skills().releaseUnitSpell({merc.id, merc.pos, merc.look, merc.combatRandom}, *attack.skill, attack.aim, attack.target);
            simulation_->emit(SkillCast{merc.id, attack.skill->sourceId, merc.pos});
            if (attack.skill->effect == SkillBehavior::Inferno) {
                for (const auto &entry : hirelingDefinition()->skills) if (entry.id == attack.skill->sourceId) {
                    if (!merc.skills) merc.skills = std::make_shared<HirelingSkillState>();
                    merc.skills->infernoEnd = state().frame + EffectFrame(entry.channelFrames);
                    merc.skills->nextInfernoPulse = state().frame + 2;
                    attack.ticks = (actor.infernoAnimation * 256 + attack.timing.speed - 1) / attack.timing.speed;
                    return;
                }
            }
        } else if (!actor.attack1Projectile && hirelingDefinition()->act >= 2) {
            auto target = simulation_->combatUnit(attack.target);
            const auto &weapon = stats.weapon;
            if (target.alive() && !region().definition.safe && simulation_->canAttack(merc.id, target.id) &&
                meleeDistance(merc.pos, merc.collisionSize, *target.position, target.stats.collisionSize) <= 1 + actor.meleeRange &&
                map().grid.missileSegment(merc.pos, *target.position, {0x0804, 1})) {
                const auto *skill = attack.skill ? &*attack.skill : nullptr;
                const MonsterDefense defense{target.stats.level, target.stats.attributes.defense, target.stats.demon, target.stats.undead, target.stats.boss};
                const int chance = weaponHitChance(merc.level, weapon.baseAttackRating,
                    weapon.attackRatingPercent + (skill ? skill->weapon->attackRating : 0), weapon.target, defense, target.stats.rank);
                if (limitedRandom(merc.combatRandom, 100) < unsigned(chance)) {
                    const int percent = std::max(-90, weapon.damagePercent + (skill ? skill->weapon->damagePercent : 0) + targetDamageBonus(weapon.target, defense));
                    const int lo = weapon.meleeBaseMinimum, hi = weapon.meleeBaseMaximum;
                    const int raw = lo + int(limitedRandom(merc.combatRandom, unsigned(std::max(0, hi - lo))));
                    auto elements = simulation_->rollAttackElements(weapon.item, &stats.combat, skill, &merc.combatRandom);
                    elements.attackerLevel = merc.level; elements.manaLeech = 0; elements.hitClass = weapon.hitClass;
                    simulation_->resolveWeaponHit(target.id, float(int64_t(raw) * (100 + percent) / 100) / 256.f, merc.id, elements);
                } else simulation_->skills().triggerCombatEffects(target.id, CombatEffectEvent::AttackedInMelee, merc.id);
            }
            if (attack.skill) simulation_->emit(SkillCast{merc.id, attack.skill->sourceId, merc.pos});
        } else if (!region().definition.safe && actor.attack1Projectile) {
            const auto &projectile = *actor.attack1Projectile;
            if (auto target = simulation_->combatUnit(attack.target); target.alive()) attack.aim = *target.position;
            merc.look = (attack.aim - merc.pos).unit();
            const auto &weapon = stats.weapon;
            const int spread = std::max(0, weapon.projectileMaximum - weapon.projectileMinimum);
            const int raw = weapon.projectileMinimum + int(limitedRandom(merc.combatRandom, unsigned(spread)));
            const auto *skill = attack.skill ? &*attack.skill : nullptr;
            const auto *bow = skill && skill->weapon ? skill->weapon->bow.get() : nullptr;
            const int missileId = skill ? skill->missileId : projectile.id;
            Missile missile{ids_.allocate(), merc.id, merc.pos, merc.look * (skill ? skill->missileVelocity : projectile.velocity),
                skill ? skill->missileLifetime : projectile.lifetime, SkillBehavior::None, true, missileId,
                float(int64_t(raw) * projectile.sourceDamage / 128) / 256.f};
            missile.attackElements = simulation_->rollAttackElements(weapon.item, &stats.combat, skill, &merc.combatRandom);
            if (bow) {
                const int lo = int(skill->minimumDamage * 256.f), hi = int(skill->maximumDamage * 256.f);
                const float amount = float(lo + limitedRandom(merc.combatRandom, unsigned(std::max(0, hi - lo)))) / 256.f;
                if (bow->element == DamageType::Fire) missile.attackElements.fire += amount;
                else if (bow->element == DamageType::Cold) { missile.attackElements.cold += amount; missile.attackElements.coldDuration += skill->coldDuration; }
                missile.attackElements.conversionPercent = bow->conversionPercent;
                missile.attackElements.conversionElement = bow->element;
                missile.skillId = skill->sourceId; missile.skillRank = skill->rank;
                missile.impact = skill->missileImpact;
            }
            missile.attackElements.ranged = true;
            missile.attackElements.hitClass = weapon.hitClass;
            missile.weaponAttack = true;
            missile.attackElements.attackerLevel = merc.level;
            missile.attackElements.manaLeech = 0;
            missile.attackerLevel = merc.level;
            missile.attackRating = weapon.attackRating;
            missile.baseAttackRating = weapon.baseAttackRating;
            missile.attackRatingPercent = weapon.attackRatingPercent + (skill ? skill->weapon->attackRating : 0);
            missile.targetModifiers = weapon.target;
            missile.physicalDamagePercent = weapon.projectileDamagePercent;
            missile.combatRandom = childRandom(simulation_->unitRandom_);
            prepareMissileLaunch(missile, stats.combat, simulation_->missileCanSlow_ && simulation_->missileCanSlow_(missileId),
                simulation_->missileCanPierce_ && simulation_->missileCanPierce_(missileId));
            simulation_->state_.area.missiles.push_back(std::move(missile));
            simulation_->emit(MissileReleased{missileId});
            if (skill) simulation_->emit(SkillCast{merc.id, skill->sourceId, merc.pos});
        }
    }
    if (attack.ticks >= attack.timing.durationTicks()) { merc.attack.reset(); merc.attackTimer = 0; }
}
} // namespace d2x
