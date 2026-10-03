#include "gameplay/skills/world_values.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/aura.hpp"
#include "gameplay/skills/aura_owner.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include <algorithm>
#include <cmath>
#include "core/random.hpp"

namespace d2x {
bool SkillRuntime::clearAuraIfChanged(SkillAuraOwner owner, int skill, int rank) {
    auto &aura = owner.aura;
    if (!aura || (aura->definition.skill == skill && aura->definition.rank == rank)) return false;
    auto unit = combatUnit(owner.id);
    std::vector<EffectHandle> remove;
    for (const auto &effect : unit.effects->entries())
        if (effect.spec.state.id == aura->definition.ownerState.id &&
            effect.spec.source.entity == owner.id && effect.spec.source.definition == aura->definition.skill)
            remove.push_back(effect.handle);
    for (auto handle : remove) unit.effects->remove(handle);
    world_.suppressManaRegen(owner.id, false);
    if (aura->definition.skill == 114 && !owner.dead)
        unit.effects->removeState(world_.shatterState().id);
    aura.reset();
    return true;
}
void SkillRuntime::prepareAura(SkillAuraOwner owner, const AuraDefinition &definition, bool immediate) {
    if (owner.aura) {
        owner.aura->definition = definition;
        return;
    }
    const EffectFrame period = EffectFrame(std::max(5, definition.periodFrames));
    owner.aura = ActiveAura{definition, world_.frame() + period};
    if (immediate) {
        owner.aura->nextFrame = world_.frame();
        pulseAura(combatUnit(owner.id), owner.aura->definition, owner.aura->nextFrame);
    } else {
        CombatEffectSpec effect;
        effect.state = definition.ownerState;
        effect.source = {CombatEffectSource::Skill, owner.id, definition.skill, definition.rank};
        effect.stacking = EffectStacking::AuraLevel;
        effect.duration = period + 1;
        const auto applied = combatUnit(owner.id).effects->apply(std::move(effect), world_.frame());
        world_.effectsChanged(applied.removed);
    }
}
void SkillRuntime::pulseAura(CombatUnit source, const AuraDefinition &aura, EffectFrame &nextFrame) {
    if (!source.alive() || !active(*source.position) || world_.frame() < nextFrame) return;
    const EffectFrame period = EffectFrame(std::max(5, aura.periodFrames));
    nextFrame = world_.frame() + period;
    const EffectFrame duration = nextFrame - world_.frame() + 1;
    bool restoredLife = false;
    auto apply = [&](CombatUnit target, const CombatStateDefinition &state, bool owner) {
        if (state.id < 0) return;
        if (!world_.auraEligible(target.id, false)) return;
        if (aura.skill == 123 && !owner) {
            const auto *ownAura = world_.ownAura(target.id);
            if (ownAura && ownAura->skill == aura.skill && ownAura->rank > aura.rank) return;
        }
        for (const auto &effect : target.effects->entries())
            if (effect.activeAt(world_.frame()) && effect.spec.state.id == state.id &&
                effect.spec.source.definition == aura.skill && effect.spec.source.level > aura.rank) return;
        const bool enoughMana = !source.player || *source.mana >= aura.manaPerPulse;
        if (enoughMana && aura.harmfulDurationPercent < 100) {
            target.effects->shortenCurableCurses(world_.frame(), aura.harmfulDurationPercent);
            const int poisonFrames = std::max(0, int(*target.poison.remaining * 25.f + .0001f));
            *target.poison.remaining = float(poisonFrames * aura.harmfulDurationPercent / 100) / 25.f;
            if (*target.poison.remaining == 0) *target.poison.rate = 0;
        }
        if (enoughMana && aura.lifePerPulse > 0 && *target.life < target.stats.attributes.maxLife) {
            const float before = *target.life;
            world_.restore(target.id, aura.lifePerPulse);
            restoredLife |= *target.life > before;
        }
        CombatEffectSpec effect;
        effect.state = state;
        effect.source = {CombatEffectSource::Skill, source.id, aura.skill, aura.rank};
        effect.stacking = EffectStacking::AuraLevel;
        effect.duration = duration;
        if ((!aura.hostile || !owner) && enoughMana) effect.modifiers = aura.modifiers;
        if (owner) mergeCharacterModifiers(effect.modifiers, aura.ownerModifiers);
        if (aura.skill == 114 && !owner) {
            const int limit = world_.coldEffect(target.id).value_or(-50);
            effect.modifiers.velocityPercent = std::max(effect.modifiers.velocityPercent, limit);
            effect.modifiers.combat.attackRate = std::max(effect.modifiers.combat.attackRate, limit);
            effect.modifiers.otherAnimationRate = effect.modifiers.combat.attackRate;
        }
        if (aura.skill == 123 && !owner && target.monster && !target.hireling) {
            auto reduce = [&](int &value, MonsterDamageType type) {
                const auto base = world_.baseResistance(target.id, type);
                if (base && *base >= 100 && value < 0) value /= 5;
            };
            reduce(effect.modifiers.fireResist, MonsterDamageType::Fire);
            reduce(effect.modifiers.coldResist, MonsterDamageType::Cold);
            reduce(effect.modifiers.lightningResist, MonsterDamageType::Lightning);
        }
        if (owner && source.player) effect.modifiers.combat.damagePercent += aura.ownerDamageBonus;
        if (aura.skill == 122 && !owner)
            for (const auto &existing : target.effects->entries())
                if (existing.activeAt(world_.frame()) && existing.spec.state.id == state.id &&
                    existing.spec.source.definition == aura.skill && existing.spec.source.level == aura.rank &&
                    existing.spec.source.entity == target.id) {
                    effect.modifiers.combat.damagePercent = existing.spec.modifiers.combat.damagePercent;
                    effect.source.entity = target.id;
                }
        const auto removed = target.effects->apply(std::move(effect), world_.frame()).removed;
        if (target.player) world_.effectsChanged(removed);
    };
    apply(source, aura.ownerState, true);
    if (aura.skill == 124) {
        if (world_.safeZone()) return;
        for (const auto &corpse : world_.corpses()) {
            if (!corpse.available || !world_.nearby(*source.position, corpse.position) ||
                !world_.redeemableCorpse(corpse.id)) continue;
            const float offsetX = std::floor(source.position->x) - std::floor(corpse.position.x);
            const float offsetY = std::floor(source.position->y) - std::floor(corpse.position.y);
            if (offsetX * offsetX + offsetY * offsetY > aura.radius * aura.radius) continue;
            if (limitedRandom(*source.random, 100) >= unsigned(aura.redemptionChance)) continue;
            world_.restore(source.id, aura.redemptionLife, aura.redemptionMana);
            world_.consumeCorpse(corpse.id);
        }
        return;
    }
    float pulseDamage = 0;
    if (aura.hostile && aura.element >= 0 && !world_.safeZone()) {
        const int minimum = int(aura.minimumDamage * 256.f), maximum = int(aura.maximumDamage * 256.f);
        pulseDamage = float(minimum + limitedRandom(*source.random, unsigned(std::max(0, maximum - minimum)))) / 256.f;
    }
    for (auto target : combatUnits()) {
        if (!target.alive() || target.id == source.id || !world_.nearby(*source.position, *target.position)) continue;
        if (!(aura.filter & (target.player ? 1 : 2))) continue;
        if ((aura.filter & 0x80) && !target.identity.attackable) continue;
        if (!aura.hostile && !world_.auraEligible(target.id, true)) continue;
        if (aura.hostile && world_.safeZone()) continue;
        if ((aura.filter & 4) && !target.stats.undead) continue;
        if ((aura.filter & 0x4000) && target.stats.boss) continue;
        if ((aura.filter & 0x40000) && target.stats.primeEvil) continue;
        if ((aura.filter & 0x200) && !world_.collisionSegment(*source.position, *target.position, 4)) continue;
        const float offsetX = std::floor(source.position->x) - std::floor(target.position->x);
        const float offsetY = std::floor(source.position->y) - std::floor(target.position->y);
        if (offsetX * offsetX + offsetY * offsetY > aura.radius * aura.radius) continue;
        if (aura.hostile ? !canAttack(source.id, target.id) : relation(source.id, target.id) != Relation::Allied) continue;
        if (aura.skill == 114 && (target.monster || target.hireling) &&
            (!world_.coldEffect(target.id) || *world_.coldEffect(target.id) >= 0)) continue;
        apply(target, aura.state, false);
        if (pulseDamage > 0) {
            DamageRequest hit{source.id, target.id, pulseDamage, MonsterDamageType(aura.element), 0, false, false};
            hit.hitClass = aura.hitClass;
            hit.softHit = (aura.resultFlags & 0x4000) != 0;
            dealDamage(hit);
            if (aura.resultFlags & 8) world_.knockback(source.id, target.id);
        }
        if (aura.skill == 114 && target.monster && target.alive()) {
            target.effects->removeState(world_.shatterState().id);
            if (limitedRandom(*target.random, 100) < 20) {
                CombatEffectSpec shatter;
                shatter.state = world_.shatterState();
                shatter.source = {CombatEffectSource::Skill, source.id, aura.skill, aura.rank};
                target.effects->apply(std::move(shatter), world_.frame());
            }
        }
    }
    if (source.player && aura.manaPerPulse > 0) {
        world_.suppressManaRegen(source.id, restoredLife);
        if (restoredLife) *source.mana -= aura.manaPerPulse;
    }
}
void SkillRuntime::updateAuras(bool playerOnly) {
    for (const auto &source : world_.auraSources(playerOnly))
        pulseAura(combatUnit(source.actor), *source.definition, *source.nextFrame);
}
void SkillRuntime::reflectThorns(EntityId attacker, EntityId defender, float physicalDamage) {
    const auto source = combatUnit(attacker), target = combatUnit(defender);
    if (!source.alive() || !target.alive() || physicalDamage <= 0) return;
    int percent = target.stats.attributes.combat.thornsPercent;
    if (source.player || source.hireling) percent = (percent + 4) / 8;
    if (percent <= 0) return;
    const float reflected = float(int64_t(physicalDamage * 256.f) * percent / 100) / 256.f;
    DamageRequest hit{defender, attacker, reflected, MonsterDamageType::Physical, 0, false, false};
    hit.softHit = true;
    hit.hitClass = 0x8d;
    dealDamage(hit);
}
void SkillRuntime::reflectIronMaiden(EntityId attacker, EntityId defender, float physicalDamage) {
    const auto source = combatUnit(attacker), target = combatUnit(defender);
    if (!source.alive() || !target.alive() || physicalDamage <= 0) return;
    int percent = source.stats.attributes.combat.ironMaidenPercent;
    if (source.player || source.hireling) percent /= 4;
    if (percent <= 0) return;
    DamageRequest hit{defender, attacker, float(int64_t(physicalDamage * 256.f) * percent / 100) / 256.f,
        MonsterDamageType::Physical, 0, false, false};
    hit.softHit = true;
    hit.hitClass = 141;
    hit.permission = DamagePermission::ExistingEffect;
    dealDamage(hit);
}
void SkillRuntime::healLifeTap(EntityId attacker, EntityId defender, float physicalDamage) {
    const auto source = combatUnit(attacker), target = combatUnit(defender);
    if (!source.alive() || !target.alive() || physicalDamage <= 0) return;
    const int percent = target.stats.attributes.combat.lifeTapPercent;
    if (percent <= 0) return;
    world_.restore(attacker, float(int64_t(physicalDamage * 256.f) * percent / 100) / 256.f);
    for (const auto &effect : target.effects->entries())
        if (effect.activeAt(world_.frame()) && effect.spec.lifeTapOverlay >= 0)
            world_.addEffect({*source.position, 0, effect.spec.lifeTapOverlayDuration,
                -1, effect.spec.lifeTapOverlay, attacker});
}
}
