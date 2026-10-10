#include "system.hpp"
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
#include "gameplay/monsters/enchantment_damage.hpp"
#include "hireling_hit.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::combat {
DomainResult<> System::enqueue(const Damage &damage) {
    if (!damage.source || !damage.target || damage.source == damage.target || damage.type != DamageType::Physical ||
        damage.minimum < 0 || damage.maximum < damage.minimum || damage.maximum > INT32_MAX || !damage.action || damage.level <= 0)
        return {DomainStatus::InvalidRequest, {}};
    if (state_.pending.size() >= 4096) return {DomainStatus::Capacity, {}};
    if (std::any_of(state_.pending.begin(), state_.pending.end(), [&](const auto &existing) { return !existing.cancelled && existing.source == damage.source; }))
        return {DomainStatus::Conflict, {}};
    const Participants participants{ports_.players,ports_.monsters};
    state_.pending.push_back(damage);
    state_.pending.back().binding=participants.bind(participants.find(damage.source));
    return {DomainStatus::Applied, std::monostate{}};
}
void System::cancel(EntityId source) {
    // Reactions can cancel another action while step is traversing this queue.
    for(auto &entry:state_.pending) if(entry.source==source) entry.cancelled=true;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked = false;
    state_.targetAttempts=0;
    const Participants participants{ports_.players, ports_.monsters};
    for (auto it = state_.pending.begin(); it != state_.pending.end();) {
        auto &damage = *it;
        if(damage.cancelled) {it=state_.pending.erase(it);continue;}
        const auto source = participants.find(damage.source), target = participants.find(damage.target);
        if(participants.bind(source)!=damage.binding) {it=state_.pending.erase(it);continue;}
        const auto *sourcePlayer = source.player, *targetPlayer = target.player;
        const auto *sourceMonster = source.monster, *targetMonster = target.monster;
        const bool petAttack=sourceMonster && (sourceMonster->amazonAttacker() || sourceMonster->hireling) && targetMonster && !targetMonster->owner;
        const bool playerAttack = sourcePlayer && targetMonster;
        const bool monsterAttack = sourceMonster && (targetPlayer || (targetMonster && (targetMonster->combatCompanion() || (sourceMonster->conversion && !targetMonster->owner))));
        const auto *area = ports_.areas.find(damage.area);
        bool valid = area && !area->definition.town && source.aliveIn(damage.area) && target.aliveIn(damage.area) && participants.canHarm(source,target) &&
            ((playerAttack && damage.weapon.has_value()) || monsterAttack || (petAttack && damage.weapon));
        if (sourceMonster) valid = valid && sourceMonster->interruption==damage.sourceInterruption && sourceMonster->frozenUntil <= tick.tick && sourceMonster->stunnedUntil<=tick.tick && sourceMonster->knockedUntil<=tick.tick && (!sourceMonster->owner || petAttack || sourceMonster->conversion);
        if (targetMonster) valid = valid && (!targetMonster->owner || (monsterAttack && targetMonster->combatCompanion()));
        if (!valid) { it = state_.pending.erase(it); continue; }
        if(targetPlayer && !damage.reactionsStarted) {
            if(!ports_.effects.reactionCapacity()) {blocked=true;++it;continue;}
            ports_.effects.react({targetPlayer->player,targetPlayer->actor,targetPlayer->area,area->generation,0,tick.tick},
                damage.source,CombatEffectEvent::AttackedInMelee);damage.reactionsStarted=true;
        }
        if(damage.impact>tick.tick) {++it;continue;}
        ++state_.targetAttempts;
        const Vec from = source.position(), to = target.position();
        if (meleeDistance(from, damage.sourceSize, to, target.size()) > damage.range ||
            !area->definition.collision.segment(from, to)) { it = state_.pending.erase(it); continue; }
        // Reserve both private resource and public hit outputs before consuming RNG.
        if (!ports_.events.hasCapacity(4, targetPlayer ? 2 : 1) || !ports_.effects.reactionCapacity()) { blocked = true; ++it; continue; }
        auto random = damage.actionRandom.value_or(ports_.random);
        const auto commitRandom=[&]{if(!damage.actionRandom) ports_.random=random;};
        const bool running = targetPlayer && targetPlayer->moving && targetPlayer->runningNow;
        const auto chance = [&] {
            if (targetMonster && damage.weapon) {
                const auto &weapon = *damage.weapon;
                return weaponHitChance(damage.level, weapon.baseAttackRating, weapon.attackRatingPercent+(damage.skill && damage.skill->weapon?damage.skill->weapon->attackRating:0), weapon.target,
                    {targetMonster->rule.level, ports_.effects.unitDefense(targetMonster->id,tick.tick), targetMonster->rule.demon, targetMonster->rule.undead, false}, targetMonster->identity.rank);
            }
            return physicalHitChance(damage.level,sourceMonster?int(int64_t(damage.rating)*std::max(0,100+ports_.effects.unitModifiers(sourceMonster->id,tick.tick).combat.attackRatingPercent+(sourceMonster->rule.enchantment?sourceMonster->rule.enchantment->attackRatingPercent:0))/100):damage.rating,targetPlayer?targetPlayer->persistent.player.level:targetMonster->rule.level,targetPlayer?targetPlayer->totals.character.defense:ports_.effects.unitDefense(targetMonster->id,tick.tick));
        };
        bool hit = running || int(limitedRandom(random, 100)) < chance();
        if (hit && targetPlayer && targetPlayer->totals.equipment.shield) {
            const int block = targetPlayer->totals.equipment.blockChance / (running ? 3 : 1);
            hit = int(limitedRandom(random, 100)) >= block;
        }
        if(hit && targetMonster && !targetMonster->owner && (targetMonster->shield || targetMonster->rule.blockWithoutShield) &&
            int(limitedRandom(random,100))<targetMonster->rule.blockChance) {
            const auto result=ports_.monsters.block(targetMonster->id,tick.tick);
            if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            commitRandom();it=state_.pending.erase(it);continue;
        }
        if(hit && targetPlayer) {
            const auto avoided=rollWeaponAvoidance(targetPlayer->totals.character.combat,targetPlayer->moving,false,random);
            if(avoided!=WeaponAvoidance::None) {
                const auto result=ports_.effects.avoidance({targetPlayer->player,targetPlayer->actor,targetPlayer->area,area->generation,0,tick.tick},avoided,damage.source);
                if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
                commitRandom();it=state_.pending.erase(it);continue;
            }
        }
        if(hit && targetMonster && targetMonster->amazonPet) {
            if(targetMonster->petStats.block>0 && int(limitedRandom(random,100))<targetMonster->petStats.block) hit=false;
            if(hit && rollWeaponAvoidance(targetMonster->petStats.attributes.combat,targetMonster->moving,false,random)!=WeaponAvoidance::None) hit=false;
        }
        if (monsterAttack && (!sourceMonster->owner || sourceMonster->conversion)) {
            const auto attack = sourceMonster->rule.attacks.find(damage.monsterMode);
            if (attack == sourceMonster->rule.attacks.end()) { it = state_.pending.erase(it); continue; }
            const auto &slot = attack->second;
            auto enchantmentDamage=sourceMonster->enchantmentDamage;
            MonsterHit rolled = hit ? rollMonsterHit(slot.minimum, slot.maximum, 0, slot.elements, 128, random) : MonsterHit{};
            if(hit) {
                const auto buffs=ports_.effects.unitModifiers(sourceMonster->id,tick.tick);
                const int percent=buffs.combat.damagePercent+(sourceMonster->rule.enchantment?sourceMonster->rule.enchantment->damagePercent:0);
                rolled.channels[0]=rolled.channels[0]*std::max(0,100+percent)/100;
                if(sourceMonster->rule.enchantment) addMonsterEnchantmentDamage(rolled,*sourceMonster->rule.enchantment,128,random,enchantmentDamage);
                monsterCritical(rolled,sourceMonster->rule.criticalChance,random);
            }
            auto hitClassCursor=state_.hitClassCursor;
            if(hit) rolled.hitClass=monsterDamageHitClass(rolled,sourceMonster->rule.hitClass,hitClassCursor);
            rolled.knockback=hit && sourceMonster->rule.knockbackOnHit;
            SpellPlan thorns;
            if(hit && rolled.channels[0]>0) {
                auto mods=targetPlayer?targetPlayer->totals.character.combat:ports_.effects.unitModifiers(targetMonster->id,tick.tick).combat;
                if(targetMonster && targetMonster->hireling) mergeCombatModifiers(mods,targetMonster->petStats.attributes.combat);
                if(mods.thornsPercent>0) {
                    EntityId owner=targetPlayer?targetPlayer->actor:EntityId{};
                    if(targetMonster && targetMonster->owner) if(const auto *p=ports_.players.find(*targetMonster->owner)) owner=p->actor;
                    if(targetMonster && targetMonster->hireling) owner=targetMonster->id;
                    const auto physical=targetPlayer?int64_t(mitigatePlayerDamage(float(rolled.channels[0])/256.f,DamageType::Physical,targetPlayer->totals.character).dealt*256.f):int64_t(mitigateMonsterDamage(float(rolled.channels[0])/256.f,ports_.effects.unitResistance(targetMonster->id,DamageType::Physical,tick.tick))*256.f);
                    if(owner && physical>0) {
                        SpellImpact reflection{damage.source,owner,damage.area,DamageType::Physical,physical*mods.thornsPercent/100,{damage.source}};
                        reflection.occurrence=(uint64_t(1)<<60)|damage.action;reflection.hitClass=0x8d;reflection.reaction=true;reflection.unblockable=true;
                        auto plan=prepareSpells({reflection});if(!plan) {blocked=true;++it;continue;}thorns=std::move(*plan.value);
                    }
                }
            }
            DomainResult<> result;
            if (targetPlayer) {
                const ActorContext actor{targetPlayer->player,targetPlayer->actor,targetPlayer->area,area->generation,0,tick.tick};
                const auto wear=hit?ports_.inventory.defensiveWear(actor.player,random):std::nullopt;
                const auto received = ports_.effects.receiveMonster(actor,damage.source,rolled,sourceMonster->rule.hitStates,wear);
                result = {received.status,received ? std::optional{std::monostate{}} : std::nullopt};
                if (received && hit) {ports_.effects.triggerMonsterCurse(damage.source,tick.tick);if(*received.value>0) ports_.effects.react(actor,damage.source,CombatEffectEvent::DamagedInMelee);}
            } else {
                int64_t amount = 0,absorbed=0;
                for(size_t i=0;i<5;++i) {const auto resolved=ports_.effects.unitDamage(targetMonster->id,rolled.channels[i],DamageType(i),tick.tick);amount+=int64_t(resolved.dealt*256.f);absorbed+=int64_t(resolved.absorbed*256.f);}
                const auto cold = rolled.coldFrames * unsigned(std::clamp(100-ports_.effects.unitResistance(targetMonster->id,DamageType::Cold,tick.tick),0,200))/(100u*unsigned(targetMonster->rule.coldDivisor));
                std::optional<PoisonApplication> poison;
                if (rolled.poisonFrames && rolled.channels[5]) poison = PoisonApplication{int64_t(mitigateMonsterDamage(float(rolled.channels[5])/256.f,ports_.effects.unitResistance(targetMonster->id,DamageType::Poison,tick.tick))*256.f),rolled.poisonFrames,sourceMonster->rule.hitStates.poison.id};
                result = ports_.monsters.damage(damage.target,damage.source,amount,tick.tick,cold,false,rolled.hitClass,poison,false,0,{},absorbed);
                if(result && hit && amount>0 && targetMonster->hireling) ports_.effects.hirelingReact(targetMonster->id,damage.source,CombatEffectEvent::DamagedInMelee,tick.tick);
            }
            if (result) {commitSpells(std::move(thorns));ports_.monsters.commitEnchantmentDamage(sourceMonster->id,std::move(enchantmentDamage));state_.hitClassCursor=hitClassCursor;commitRandom(); it = state_.pending.erase(it); }
            else if (result.status == DomainStatus::Capacity) { blocked = true; ++it; }
            else it = state_.pending.erase(it);
            continue;
        }
        if(petAttack) {
            const auto *owner=ports_.players.find(*sourceMonster->owner);
            if(!owner || !owner->entered || owner->area!=damage.area || owner->persistent.player.hp<=0 || !owner->rules.skills) {it=state_.pending.erase(it);continue;}
            int64_t amount=0;uint64_t cold=0;std::optional<PoisonApplication> poison;
            HirelingHitEffects extras;WeaponSkillDamage snapshot;
            if(hit) {
                SkillCastSpec ordinary=damage.skill.value_or(SkillCastSpec{});if(!ordinary.weapon) ordinary.weapon=WeaponSkillSpec{};
                const auto rolled=rollWeaponSkillDamage(*damage.weapon,damage.attackModifiers.value_or(sourceMonster->petStats.attributes.combat),ordinary,damage.level,false,random);
                snapshot=rolled;
                auto channels=targetWeaponChannels(rolled,targetMonster->rule.demon,targetMonster->rule.undead);
                for(auto &channel:channels) channel=channel*hirelingDamagePercent(*sourceMonster,*targetMonster)/100;
                if(sourceMonster->rule.criticalChance>0 && int(limitedRandom(random,100))<sourceMonster->rule.criticalChance) channels[0]*=2;
                for(size_t element=0;element<5;++element) amount+=int64_t(mitigateMonsterDamage(float(channels[element])/256.f,ports_.effects.unitResistance(targetMonster->id,DamageType(element),tick.tick))*256.f);
                if(sourceMonster->hireling) {
                    const int resistance=ports_.effects.unitResistance(targetMonster->id,DamageType::Physical,tick.tick);
                    const auto physical=int64_t(mitigateMonsterDamage(float(channels[0])/256.f,resistance)*256.f);
                    extras=hirelingHitEffects(rolled,*targetMonster,sourceMonster->id,damage.binding.credit,resistance,physical,amount,tick.tick);amount+=extras.crushing;
                }
                if(rolled.coldFrames>0 && ports_.effects.unitResistance(targetMonster->id,DamageType::Cold,tick.tick)<100) cold=uint64_t(rolled.coldFrames)*unsigned(std::clamp(100-ports_.effects.unitResistance(targetMonster->id,DamageType::Cold,tick.tick),0,200))/(100u*unsigned(owner->rules.skills->coldDivisor[size_t(targetMonster->rule.difficulty)]));
                if(channels[5]>0 && rolled.poisonFrames>0) poison=PoisonApplication{int64_t(mitigateMonsterDamage(float(channels[5])/256.f,ports_.effects.unitResistance(targetMonster->id,DamageType::Poison,tick.tick))*256.f),uint64_t(rolled.poisonFrames),owner->rules.skills->poisonState};
            }
            if(poison) poison->actualSource=sourceMonster->id;
            const auto committed=ports_.monsters.damage(damage.target,damage.binding.credit,amount,tick.tick,cold,snapshot.freeze,uint8_t(damage.weapon->hitClass),poison,false,uint64_t(std::max(0,snapshot.stunFrames)),extras.wound);
            if(committed.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            if(committed) {
                if(sourceMonster->hireling) {ports_.monsters.recordHirelingKill(damage.target,sourceMonster->id);ports_.monsters.heal(sourceMonster->id,extras.healing+(targetMonster->life<=0?int64_t(sourceMonster->petStats.attributes.combat.lifeOnKill)*256:0));}
                if(hit && snapshot.knockback && targetMonster->life>0) ports_.monsters.knockback(targetMonster->id,sourceMonster->position,tick.tick);
                commitRandom();
            }
            it=state_.pending.erase(it);continue;
        }
        int64_t amount = 0;
        if (hit) {
            if (damage.weapon && targetMonster) {
                // Single-player meleeDamage ordering and target-specific physical bonuses.
                const auto &weapon = *damage.weapon;
                const int targetBonus = (targetMonster->rule.demon ? std::max(0, weapon.target.demonDamage) : 0) +
                    (targetMonster->rule.undead ? std::max(0, weapon.target.undeadDamage + (weapon.blunt ? 50 : 0)) : 0);
                const int64_t bonus = std::max<int64_t>(-90, int64_t(weapon.damagePercent) + targetBonus);
                const auto minimum = int64_t(weapon.meleeBaseMinimum) + int64_t(weapon.meleeBaseMinimum) * (bonus + weapon.minimumDamagePercent) / 100;
                const auto maximum = int64_t(weapon.meleeBaseMaximum) + int64_t(weapon.meleeBaseMaximum) * (bonus + weapon.maximumDamagePercent) / 100;
                const auto low = std::clamp<int64_t>(minimum, 0, INT32_MAX), high = std::clamp<int64_t>(maximum, low, INT32_MAX);
                amount = low + limitedRandom(random, uint32_t(high - low));
                bool deadly = damage.criticalChance > 0 && int(limitedRandom(random, 100)) < std::min(damage.criticalChance, 100);
                if (!deadly && damage.deadlyChance > 0) deadly = int(limitedRandom(random, 100)) < std::min(damage.deadlyChance, 100);
                if (deadly) amount *= 2;
            } else {
                amount = damage.minimum + limitedRandom(random, uint32_t(damage.maximum - damage.minimum + 1));
                if (sourceMonster && int(limitedRandom(random, 100)) < sourceMonster->rule.criticalChance) amount *= 2;
            }
            if (targetMonster) {
                amount = int64_t(mitigateMonsterDamage(float(amount) / 256.f, ports_.effects.unitResistance(targetMonster->id,DamageType::Physical,tick.tick)) * 256.f);
                if(sourcePlayer) {
                const auto &mods=sourcePlayer->totals.character.combat;
                int low=mods.fireMinimum,high=mods.fireMaximum;
                if(auto own=mods.weapons.find(damage.weapon->item);own!=mods.weapons.end()) {low+=own->second.fireMinimum;high+=own->second.fireMaximum;}
                const auto mastery=sourcePlayer->rules.skills?skills::mastery(*sourcePlayer,sourcePlayer->rules.skills->fireMasteries):0;
                const int64_t fire=(int64_t(std::max(0,low))*256+limitedRandom(random,uint32_t(std::max(0,high-low))*256))*(100+mastery)/100;
                amount+=int64_t(mitigateMonsterDamage(float(fire)/256.f,targetMonster->rule.resistances[size_t(DamageType::Fire)])*256.f);
                }
            }
        }
        std::optional<effects::ItemEventPlan> itemEvents;
        if(sourcePlayer && hit) {
            auto prepared=ports_.effects.prepareItemEvents({sourcePlayer->player,sourcePlayer->actor,sourcePlayer->area,area->generation,0,tick.tick},{ItemSkillEvent::Hit,ItemSkillEvent::Kill},damage.target,targetMonster->position);
            if(!prepared) {blocked=true;++it;continue;}itemEvents=std::move(*prepared.value);
        }
        DomainResult<> committed{DomainStatus::Applied, std::monostate{}};
        if (targetMonster) committed = ports_.monsters.damage(damage.target, damage.source, amount, tick.tick,0,false,uint8_t(damage.weapon?damage.weapon->hitClass:0));
        else {
            ActorContext actor{targetPlayer->player, targetPlayer->actor, targetPlayer->area, area->generation, 0, tick.tick};
            const auto received=ports_.effects.receive(actor, amount, DamageType::Physical);
            committed={received.status,received?std::optional{std::monostate{}}:std::nullopt};
            if(received && hit && *received.value>0) ports_.effects.react(actor,damage.source,CombatEffectEvent::DamagedInMelee);
        }
        if (committed) { commitRandom();if(itemEvents) ports_.effects.commitItemEvents(std::move(*itemEvents),targetMonster->life<=0); it = state_.pending.erase(it); }
        else if (committed.status == DomainStatus::Capacity) { blocked = true; ++it; }
        else it = state_.pending.erase(it);
    }
    if (resolveSpells(tick) == StepStatus::Blocked) blocked = true;
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
