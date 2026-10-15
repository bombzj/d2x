#include "system.hpp"
#include "gameplay/combat/weapon_hit.hpp"
#include "gameplay/combat/weapon_snapshot.hpp"
#include "participants.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "core/random.hpp"
#include "gameplay/combat/avoidance.hpp"
#include "gameplay/combat/scaling.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/monsters/enchantment_damage.hpp"
#include "weapon_hit.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace d2x::server::combat {
DomainResult<> System::enqueue(const Damage &damage) {
    if (!damage.source || !damage.target || damage.source == damage.target || damage.type != DamageType::Physical ||
        damage.minimum < 0 || damage.maximum < damage.minimum || damage.maximum > INT32_MAX || !damage.action || damage.level <= 0)
        return {DomainStatus::InvalidRequest, {}};
    if (state_.pending.size() >= 4096) return {DomainStatus::Capacity, {}};
    if (std::any_of(state_.pending.begin(), state_.pending.end(), [&](const auto &existing) { return !existing.cancelled && existing.source == damage.source; }))
        return {DomainStatus::Conflict, {}};
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    state_.pending.push_back(damage);
    state_.pending.back().binding=participants.bind(participants.find(damage.source));
    return {DomainStatus::Applied, std::monostate{}};
}
void System::cancel(EntityId source) {
    // Reactions can cancel another action while step is traversing this queue.
    for(auto &entry:state_.pending) if(entry.source==source) entry.cancelled=true;
    for(auto &entry:state_.spells) if(entry.source==source && entry.nativeDrainHeld && entry.nativeSkill) entry.nativeSkill->cancelled=true;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked = false;
    state_.targetAttempts=0;
    const Participants participants{ports_.players, ports_.monsters, ports_.social};
    for (auto it = state_.pending.begin(); it != state_.pending.end();) {
        auto &damage = *it;
        if(damage.cancelled) {it=state_.pending.erase(it);continue;}
        const auto source = participants.find(damage.source), target = participants.find(damage.target);
        if(participants.bind(source)!=damage.binding) {it=state_.pending.erase(it);continue;}
        const auto *sourcePlayer = source.player, *targetPlayer = target.player;
        const auto *sourceMonster = source.monster, *targetMonster = target.monster;
        const bool petAttack=sourceMonster && (sourceMonster->amazonAttacker() || sourceMonster->necroSummon || sourceMonster->hireling) && (targetPlayer || targetMonster);
        const bool playerAttack = sourcePlayer && (targetMonster || targetPlayer);
        const bool monsterAttack = sourceMonster && !petAttack && (targetPlayer || targetMonster);
        const auto *area = ports_.areas.find(damage.area);
        bool valid = area && !area->definition.town && source.aliveIn(damage.area) && target.aliveIn(damage.area) && participants.canHarm(source,target) &&
            ((playerAttack && damage.weapon.has_value()) || monsterAttack || (petAttack && (damage.weapon || sourceMonster->necroSummon)));
        if (sourceMonster) valid = valid && sourceMonster->interruption==damage.sourceInterruption && !ports_.monsters.frozen(sourceMonster->id,tick.tick) && sourceMonster->stunnedUntil<=tick.tick && sourceMonster->knockedUntil<=tick.tick && (!sourceMonster->owner || petAttack || sourceMonster->conversion);
        if (!valid) { it = state_.pending.erase(it); continue; }
        if(damage.impact>tick.tick) {++it;continue;}
        ++state_.targetAttempts;
        const Vec from = source.position(), to = target.position();
        if (meleeDistance(from, damage.sourceSize, to, target.size()) > damage.range ||
            !area->definition.collision.segment(from, to)) { it = state_.pending.erase(it); continue; }
        if(damage.weapon && damage.weapon->nativeModifiers) {
            if(sourcePlayer) {
                const auto values=sourcePlayer->totals.handStats.find(damage.weapon->item);
                if(values==sourcePlayer->totals.handStats.end()) {it=state_.pending.erase(it);continue;}
                damage.weapon=sampleNativeWeapon(*damage.weapon,values->second,*sourcePlayer->rules.character->stats,
                    sourcePlayer->totals.character.combat,false,sourcePlayer->definition.toHitFactor);
            } else if(sourceMonster) {
                const auto values=ports_.effects.unitStatValues(sourceMonster->id,tick.tick,damage.weapon->item,
                    damage.monsterSkill?damage.monsterSkill->modeStats:nullptr);
                if(!values) {it=state_.pending.erase(it);continue;}
                auto procedures=sourceMonster->petStats.attributes.combat;
                mergeCombatModifiers(procedures,ports_.effects.unitModifiers(sourceMonster->id,tick.tick).combat);
                damage.weapon=sampleNativeWeapon(*damage.weapon,*values,*sourceMonster->rule.stats,procedures,true);
            }
        }
        if(damage.weapon && damage.binding.controller) {
            auto random=damage.actionRandom.value_or(ports_.random);
            SkillCastSpec skill=damage.skill.value_or(SkillCastSpec{});if(!skill.weapon) skill.weapon=WeaponSkillSpec{};
            SpellImpact contact{damage.source,damage.source,damage.area,DamageType::Physical,0,{damage.target}};
            contact.occurrence=damage.action;contact.binding=damage.binding;contact.melee=true;
            contact.weapon=rollWeaponSkillDamage(*damage.weapon,damage.attackModifiers.value_or(sourcePlayer?sourcePlayer->totals.character.combat:sourceMonster->petStats.attributes.combat),skill,damage.level,false,random);
            if(sourceMonster && sourceMonster->rule.criticalChance>0 && int(limitedRandom(random,100))<sourceMonster->rule.criticalChance)
                contact.weapon->channels[0]=nativeMultiply(nativeChecked(contact.weapon->channels[0]),2);
            contact.knockback=contact.weapon->knockback;contact.freeze=contact.weapon->freeze;
            contact.coldFrames=uint64_t(std::max(0,contact.weapon->coldFrames));
            contact.contactRandom=random;
            const auto queued=enqueue(std::move(contact));
            if(queued.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            if(queued && !damage.actionRandom) ports_.random=random;
            it=state_.pending.erase(it);continue;
        }
        // Reserve both private resource and public hit outputs before consuming RNG.
        if (!ports_.events.hasCapacity(4, targetPlayer ? 2 : 1) || !ports_.effects.reactionCapacity()) { blocked = true; ++it; continue; }
        auto random = damage.actionRandom.value_or(ports_.random);
        const auto commitRandom=[&]{if(!damage.actionRandom) ports_.random=random;};
        const bool running = targetPlayer && targetPlayer->moving && targetPlayer->runningNow;
        const auto chance = [&] {
            if (targetMonster && damage.weapon) {
                const auto &weapon = *damage.weapon;
                return weaponHitChance(damage.level, weapon.baseAttackRating, weapon.attackRatingPercent+(damage.skill && damage.skill->weapon?damage.skill->weapon->attackRating:0), weapon.target,
                    {targetMonster->rule.level, ports_.effects.unitDefense(targetMonster->id,tick.tick), targetMonster->rule.demon, targetMonster->rule.undead,targetMonster->rule.boss,false,targetMonster->hireling}, targetMonster->identity.rank);
            }
            return physicalHitChance(damage.level,sourceMonster?int(int64_t(damage.rating)*std::max(0,100+ports_.effects.unitModifiers(sourceMonster->id,tick.tick).combat.attackRatingPercent+(sourceMonster->rule.enchantment?sourceMonster->rule.enchantment->attackRatingPercent:0))/100):damage.rating,targetPlayer?targetPlayer->persistent.player.level:targetMonster->rule.level,targetPlayer?targetPlayer->totals.character.defense:ports_.effects.unitDefense(targetMonster->id,tick.tick));
        };
        bool hit = (damage.monsterSkill && damage.monsterSkill->alwaysHit) || running || int(limitedRandom(random, 100)) < chance();
        if (hit && targetPlayer && targetPlayer->totals.equipment.shield) {
            const int block = targetPlayer->totals.equipment.blockChance / (running ? 3 : 1);
            hit = int(limitedRandom(random, 100)) >= block;
            if(!hit) {
                const auto result=ports_.effects.block({targetPlayer->player,targetPlayer->actor,targetPlayer->area,area->generation,0,tick.tick});
                if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
                commitRandom();it=state_.pending.erase(it);continue;
            }
        }
        if(hit && targetMonster && !targetMonster->owner && (targetMonster->shield || targetMonster->rule.blockWithoutShield) &&
            int(limitedRandom(random,100))<targetMonster->rule.blockChance) {
            const auto result=ports_.monsters.block(targetMonster->id,tick.tick);
            if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            commitRandom();it=state_.pending.erase(it);continue;
        }
        if(hit) {
            const auto result=prepareWeaponBlock(target,tick,damage.weaponBlock);
            if(!result) {blocked=true;++it;continue;}
            if(*result.value) {commitRandom();it=state_.pending.erase(it);continue;}
        }
        if(hit && targetPlayer) {
            const auto avoided=rollWeaponAvoidance(targetPlayer->totals.character.combat,targetPlayer->moving,false,random);
            if(avoided!=WeaponAvoidance::None) {
                const auto result=ports_.effects.avoidance({targetPlayer->player,targetPlayer->actor,targetPlayer->area,area->generation,0,tick.tick},avoided,damage.source);
                if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
                commitRandom();it=state_.pending.erase(it);continue;
            }
        }
        if(hit && targetMonster && targetMonster->combatCompanion()) {
            if(targetMonster->petStats.block>0 && int(limitedRandom(random,100))<targetMonster->petStats.block) {
                const auto result=ports_.monsters.block(targetMonster->id,tick.tick);
                if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
                commitRandom();it=state_.pending.erase(it);continue;
            }
            if(hit && rollWeaponAvoidance(targetMonster->petStats.attributes.combat,targetMonster->moving,false,random)!=WeaponAvoidance::None) hit=false;
        }
        if ((monsterAttack || petAttack) && (!sourceMonster->owner || sourceMonster->conversion || (sourceMonster->necroSummon && !sourceMonster->petWeapon))) {
            const auto attack = sourceMonster->rule.attacks.find(damage.monsterMode);
            if (!damage.monsterSkill && attack == sourceMonster->rule.attacks.end()) { it = state_.pending.erase(it); continue; }
            const auto &slot = damage.monsterSkill?*damage.monsterSkill:attack->second;
            auto enchantmentDamage=sourceMonster->enchantmentDamage;
            MonsterHit rolled;
            if(hit) {
                const auto sampled=ports_.effects.unitStatValues(sourceMonster->id,tick.tick,{},slot.modeStats);
                if(!sampled || !sourceMonster->rule.stats) throw std::logic_error("Missing native monster attack sources");
                auto percent=nativeAdd(slot.damagePercent,sourceMonster->rule.enchantment?sourceMonster->rule.enchantment->damagePercent:0);
                if(sourceMonster->alignment()==UnitAlignment::Good && targetMonster) {
                    const auto stat=[&](std::string_view name){return sampled->get({sourceMonster->rule.stats->names.at(std::string(name)),0});};
                    if(targetMonster->rule.demon) percent=nativeAdd(percent,std::max(0,stat("item_demondamage_percent")));
                    if(targetMonster->rule.undead) percent=nativeAdd(percent,std::max(0,stat("item_undeaddamage_percent")));
                }
                rolled=rollMonsterUnitHit(*sampled,*sourceMonster->rule.stats,percent,128,random);
                if(sourceMonster->rule.enchantment) addMonsterEnchantmentDamage(rolled,*sourceMonster->rule.enchantment,128,random,enchantmentDamage);
                monsterCritical(rolled,sourceMonster->rule.criticalChance,random);
            }
            auto hitClassCursor=state_.hitClassCursor;
            if(hit) rolled.hitClass=slot.hitClass>=0?uint8_t(slot.hitClass):monsterDamageHitClass(rolled,sourceMonster->rule.hitClass,hitClassCursor);
            rolled.knockback=hit && (sourceMonster->rule.knockbackOnHit || slot.knockback);
            if(hit) rolled.stunFrames+=unsigned(std::max(0,slot.stunFrames));
            scaleHitDamage(rolled,damageClassPercentage({false,sourceMonster->hireling,sourceMonster->revived(),sourceMonster->rule.boss,sourceMonster->rule.primeEvil},
                {bool(targetPlayer),targetMonster && targetMonster->hireling,targetMonster && targetMonster->revived(),targetMonster && targetMonster->rule.boss,targetMonster && targetMonster->rule.primeEvil},
                sourceMonster->rule.hirelingBossDamagePercent,damage.source==damage.target));
            if(!hit) {
                SpellImpact miss{damage.source,damage.source,damage.area,DamageType::Physical,0,{damage.target}};
                miss.occurrence=damage.action;miss.binding=damage.binding;miss.melee=true;miss.pendingAttempts=1;miss.attemptOnly=true;
                const auto queued=enqueue(std::move(miss));if(queued.status==DomainStatus::Capacity) return StepStatus::Blocked;
                commitRandom();it=state_.pending.erase(it);continue;
            }
            SpellImpact contact{damage.source,damage.source,damage.area,DamageType::Physical,0,{damage.target}};
            contact.occurrence=damage.action;contact.binding=damage.binding;contact.melee=true;
            contact.monsterHit=rolled;contact.monsterStates=sourceMonster->rule.hitStates;
            contact.hitClass=rolled.hitClass;contact.knockback=rolled.knockback;contact.unblockable=true;contact.damageScaled=true;contact.contactRandom=random;
            const auto queued=enqueue(std::move(contact));
            if(queued.status==DomainStatus::Capacity) return StepStatus::Blocked;
            if(queued) {ports_.monsters.commitEnchantmentDamage(sourceMonster->id,std::move(enchantmentDamage));state_.hitClassCursor=hitClassCursor;commitRandom();}
            it=state_.pending.erase(it);continue;
        }
        // Every admitted source is handled by the common weapon or monster path.
        it=state_.pending.erase(it);
    }
    if(resolveSpells(tick)==StepStatus::Blocked) blocked=true;
    while(ports_.effects.hasContactEffects()) {
        if(ports_.effects.finishContacts(tick.tick)==StepStatus::Blocked) {blocked=true;break;}
        if(resolveSpells(tick)==StepStatus::Blocked) {blocked=true;break;}
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
