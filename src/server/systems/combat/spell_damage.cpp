#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/combat/avoidance.hpp"
#include "core/random.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/inventory/system.hpp"
#include <algorithm>
namespace d2x::server::combat {
// Read-only contact planning lets native missile callbacks depend on accuracy
// without committing damage or advancing the live RNG before outbox admission.
bool System::weaponContact(const WeaponSkillDamage &attack,EntityId id,uint64_t tick,uint64_t &random) const {
    const auto *target=ports_.monsters.find(id);
    if(!target || target->life<=0 || target->owner) return false;
    if(attack.automatic) return true;
    const auto &weapon=attack.weapon;
    return limitedRandom(random,100)<unsigned(weaponHitChance(attack.level,weapon.baseAttackRating,
        weapon.attackRatingPercent,weapon.target,{target->rule.level,ports_.effects.unitDefense(id,tick),target->rule.demon,target->rule.undead,false},target->identity.rank));
}
DomainResult<SpellPlan> System::prepareSpells(std::vector<SpellImpact> impacts) const {
    SpellPlan plan;
    for (auto &impact : impacts) {
        if (!impact.projectile || !impact.source || impact.next || impact.damage < 0 || impact.damage > INT32_MAX ||
            impact.targets.size() > 65536 || std::any_of(impact.coldDivisor.begin(), impact.coldDivisor.end(), [](int n) { return n <= 0; }) ||
            std::any_of(impact.freezeDivisor.begin(), impact.freezeDivisor.end(), [](int n) { return n <= 0; }))
            return {DomainStatus::InvalidRequest, {}};
        if(!impact.targetDamage.empty() && (impact.targetDamage.size()!=impact.targets.size() ||
            std::any_of(impact.targetDamage.begin(),impact.targetDamage.end(),[](int64_t n){return n<0 || n>INT32_MAX;}))) return {DomainStatus::InvalidRequest,{}};
        if(impact.sourceHeal<0 || impact.sourceHeal>INT32_MAX || (impact.monsterHit &&
            (impact.monsterHit->mana<0 || impact.monsterHit->stamina<0 ||
             std::any_of(impact.monsterHit->channels.begin(),impact.monsterHit->channels.end(),[](auto n){return n<0 || n>INT32_MAX;})))) return {DomainStatus::InvalidRequest,{}};
        if (impact.targets.empty()) continue;
        const auto same = [&](const auto &previous) { return previous.projectile == impact.projectile && previous.occurrence == impact.occurrence; };
        if (std::any_of(state_.spells.begin(), state_.spells.end(), same) || std::any_of(plan.spells.begin(), plan.spells.end(), same))
            return {DomainStatus::Stale, {}};
        if (impact.targets.size() > 65536 - state_.spellTargets - plan.targets || state_.spells.size() + plan.spells.size() >= 256)
            return {DomainStatus::Capacity, {}};
        plan.targets += impact.targets.size(); plan.spells.push_back(std::move(impact));
    }
    return {DomainStatus::Applied, std::move(plan)};
}
void System::commitSpells(SpellPlan &&plan) noexcept {
    state_.spellTargets += plan.targets; state_.spells.splice(state_.spells.end(), plan.spells);
}
DomainResult<> System::enqueue(SpellImpact impact) {
    std::vector<SpellImpact> values; values.push_back(std::move(impact));
    auto plan = prepareSpells(std::move(values)); if (!plan) return {plan.status, {}};
    commitSpells(std::move(*plan.value)); return {DomainStatus::Applied, std::monostate{}};
}
StepStatus System::resolveSpells(TickContext tick) {
    bool blocked = false;
    for (auto it = state_.spells.begin(); it != state_.spells.end();) {
        auto &impact = *it;
        const PlayerState *owner = nullptr;
        for (const auto &[id, player] : ports_.players.all()) {
            (void)id; if (player.actor == impact.source) { owner = &player; break; }
        }
        const auto *area = ports_.areas.find(impact.area);
        const auto *monsterSource=ports_.monsters.find(impact.source);
        const bool hirelingSource=monsterSource && monsterSource->hireling && monsterSource->owner;
        if(hirelingSource) owner=ports_.players.find(*monsterSource->owner);
        if ((!owner || !owner->entered || owner->area!=impact.area) && (!monsterSource || monsterSource->owner || monsterSource->area!=impact.area)) impact.next=impact.targets.size();
        if(!area || area->definition.town) impact.next=impact.targets.size();
        while (impact.next < impact.targets.size()) {
            if(!owner && monsterSource) {
                const PlayerState *player=nullptr;
                for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.actor==impact.targets[impact.next]) {player=&p;break;}}
                const auto *pet=ports_.monsters.find(impact.targets[impact.next]);
                if(player && (!player->entered || player->area!=impact.area || player->persistent.player.hp<=0)) player=nullptr;
                if(pet && ((!pet->amazonPet && !pet->hireling) || pet->area!=impact.area || pet->life<=0)) pet=nullptr;
                if ((!player || !player->entered || player->area!=impact.area || player->persistent.player.hp<=0) &&
                    (!pet || (!pet->amazonPet && !pet->hireling) || pet->area!=impact.area || pet->life<=0)) {++impact.next;continue;}
                if(impact.nextDelay && ((player && !ports_.effects.missileHitAllowed(player->actor,tick.tick)) || (pet && pet->nextHitTick>tick.tick))) {++impact.next;continue;}
                if(!ports_.effects.reactionCapacity()) {blocked=true;break;}
                if(!ports_.events.hasCapacity(4,2)) {blocked=true;break;}
                auto random=impact.contactRandom.value_or(ports_.random);
                const auto commitRandom=[&]{if(impact.contactRandom) impact.contactRandom=random;else ports_.random=random;};
                const bool stateOnly=impact.monsterHit && impact.monsterHit->slowFrames &&
                    std::all_of(impact.monsterHit->channels.begin(),impact.monsterHit->channels.end(),[](auto n){return n==0;});
                bool hit=true;
                if(impact.monsterToHit) {
                    const bool running=player && player->moving && player->runningNow;
                    hit=running || int(limitedRandom(random,100))<physicalHitChance(impact.monsterLevel,impact.monsterRating,
                        player?player->persistent.player.level:pet->rule.level,player?player->totals.character.defense:ports_.effects.unitDefense(pet->id,tick.tick));
                }
                if(hit && !stateOnly && !impact.unblockable && player && player->totals.equipment.shield) hit=int(limitedRandom(random,100))>=player->totals.equipment.blockChance/(player->moving && player->runningNow?3:1);
                if(hit && !stateOnly && !impact.unblockable && pet && pet->petStats.block>0) hit=int(limitedRandom(random,100))>=pet->petStats.block;
                if(!hit) {commitRandom();++impact.next;continue;}
                const auto avoided=(stateOnly || impact.unblockable)?WeaponAvoidance::None:rollWeaponAvoidance(player?player->totals.character.combat:pet->petStats.attributes.combat,player?player->moving:pet->moving,true,random);
                if(avoided!=WeaponAvoidance::None) {
                    if(player) {
                        const auto result=ports_.effects.avoidance({player->player,player->actor,player->area,area->generation,0,tick.tick},avoided,impact.source);
                        if(result.status==DomainStatus::Capacity) {blocked=true;break;}
                    }
                    commitRandom();++impact.next;continue;
                }
                MonsterHit rolled=impact.monsterHit.value_or(MonsterHit{});
                if(!impact.monsterHit) rolled.channels[size_t(impact.type)]=impact.targetDamage.empty()?impact.damage:impact.targetDamage[impact.next];
                auto hitClassCursor=state_.hitClassCursor;
                if(!stateOnly) rolled.hitClass=monsterDamageHitClass(rolled,impact.hitClass,hitClassCursor);
                DomainResult<> result;
                if(player) {
                    const ActorContext actor{player->player,player->actor,player->area,area->generation,0,tick.tick};
                    const auto received=ports_.effects.receiveMonster(actor,impact.source,rolled,impact.monsterStates);
                    result={received.status,received?std::optional{std::monostate{}}:std::nullopt};
                    if(received && !stateOnly) {if(!impact.reaction) ports_.effects.triggerMonsterCurse(impact.source,tick.tick);ports_.effects.react(actor,impact.source,CombatEffectEvent::HitByMissile,impact.returnFire);}
                } else {
                    if(rolled.slowFrames) {
                        result=ports_.monsters.slow(pet->id,impact.monsterStates.slow.id,rolled.slowPercent,rolled.slowFrames,tick.tick);
                        if(result.status==DomainStatus::Capacity) {blocked=true;break;}
                        if(result) commitRandom();
                        ++impact.next;continue;
                    }
                    int64_t amount=0;
                    for(size_t channel=0;channel<5;++channel) amount+=int64_t(mitigateMonsterDamage(float(rolled.channels[channel])/256.f,ports_.effects.unitResistance(pet->id,DamageType(channel),tick.tick))*256.f);
                    const auto cold=rolled.coldFrames*unsigned(std::clamp(100-ports_.effects.unitResistance(pet->id,DamageType::Cold,tick.tick),0,200))/(100u*unsigned(pet->rule.coldDivisor));
                    std::optional<PoisonApplication> poison;
                    if(rolled.poisonFrames && rolled.channels[5]>0) poison=PoisonApplication{int64_t(mitigateMonsterDamage(float(rolled.channels[5])/256.f,ports_.effects.unitResistance(pet->id,DamageType::Poison,tick.tick))*256.f),rolled.poisonFrames,impact.monsterStates.poison.id};
                    result=ports_.monsters.damage(pet->id,impact.source,amount,tick.tick,cold,false,rolled.hitClass,poison);
                    if(result && rolled.knockback) ports_.monsters.knockback(pet->id,monsterSource->position,tick.tick);
                }
                if(result.status==DomainStatus::Capacity) {blocked=true;break;}
                if(result) {if(impact.nextDelay) {if(player) ports_.effects.missileHitDelay(player->actor,tick.tick+impact.nextDelay);else ports_.monsters.hitDelay(pet->id,tick.tick+impact.nextDelay);}
                    state_.hitClassCursor=hitClassCursor;commitRandom();if(impact.sourceHeal>0) ports_.monsters.heal(impact.source,impact.sourceHeal);}
                ++impact.next;continue;
            }
            const auto *target = ports_.monsters.find(impact.targets[impact.next]);
            if (!target || target->owner || target->life <= 0 || target->area != impact.area ||
                (impact.nextDelay && target->nextHitTick > tick.tick)) { ++impact.next; continue; }
            const int raw = ports_.effects.unitResistance(target->id,impact.type,tick.tick);
            const int resistance = impact.type == DamageType::Cold && raw < 100 ? std::max(-100, raw - impact.coldPierce) : raw;
            int64_t amount = impact.targetDamage.empty()?impact.damage:impact.targetDamage[impact.next];
            auto random=ports_.random;
            if(hirelingSource && impact.monsterHit) {
                if(!ports_.events.hasCapacity(4,2)) {blocked=true;break;}
                random=impact.contactRandom.value_or(ports_.random);
                const auto commitRandom=[&]{if(impact.contactRandom) impact.contactRandom=random;else ports_.random=random;};
                if(impact.monsterToHit && int(limitedRandom(random,100))>=physicalHitChance(impact.monsterLevel,impact.monsterRating,target->rule.level,ports_.effects.unitDefense(target->id,tick.tick))) {commitRandom();++impact.next;continue;}
                if((target->shield || target->rule.blockWithoutShield) && int(limitedRandom(random,100))<target->rule.blockChance) {
                    const auto result=ports_.monsters.block(target->id,tick.tick);if(result.status==DomainStatus::Capacity) {blocked=true;break;}
                    commitRandom();++impact.next;continue;
                }
                const auto &hit=*impact.monsterHit;amount=0;
                for(size_t channel=0;channel<5;++channel) amount+=int64_t(mitigateMonsterDamage(float(hit.channels[channel])/256.f,ports_.effects.unitResistance(target->id,DamageType(channel),tick.tick))*256.f);
                const auto cold=hit.coldFrames*unsigned(std::clamp(100-ports_.effects.unitResistance(target->id,DamageType::Cold,tick.tick),0,200))/(100u*unsigned(target->rule.coldDivisor));
                std::optional<PoisonApplication> poison;
                if(hit.poisonFrames && hit.channels[5]) poison=PoisonApplication{int64_t(mitigateMonsterDamage(float(hit.channels[5])/256.f,ports_.effects.unitResistance(target->id,DamageType::Poison,tick.tick))*256.f),hit.poisonFrames,owner->rules.skills->poisonState};
                const auto result=ports_.monsters.damage(target->id,owner->actor,amount,tick.tick,cold,false,impact.hitClass,poison);
                if(result.status==DomainStatus::Capacity) {blocked=true;break;}
                if(result && impact.nextDelay) ports_.monsters.hitDelay(target->id,tick.tick+impact.nextDelay);
                if(result) commitRandom();
                ++impact.next;continue;
            }
            if(impact.weapon) {
                const auto &attack=*impact.weapon;
                if(!ports_.events.hasCapacity(8,4)) {blocked=true;break;}
                if(!(impact.weaponHit.has_value()?*impact.weaponHit:weaponContact(attack,target->id,tick.tick,random))) {
                    ports_.random=random;++impact.next;continue;
                }
                if((target->shield || target->rule.blockWithoutShield) && int(limitedRandom(random,100))<target->rule.blockChance) {
                    const auto result=ports_.monsters.block(target->id,tick.tick);
                    if(result.status==DomainStatus::Capacity) {blocked=true;break;}
                    ports_.random=random;++impact.next;continue;
                }
                const auto channels=targetWeaponChannels(attack,target->rule.demon,target->rule.undead);
                amount=0;
                for(size_t channel=0;channel<5;++channel) amount+=int64_t(mitigateMonsterDamage(float(channels[channel])/256.f,ports_.effects.unitResistance(target->id,DamageType(channel),tick.tick))*256.f);
            }
            if (impact.staticPercent) {
                const auto floor = std::max<int64_t>(256, target->maximumLife * impact.staticFloors.at(size_t(target->rule.difficulty)) / 100);
                amount = std::max(impact.minimumStaticDamage, target->life * impact.staticPercent / 100);
                amount = std::min(std::max<int64_t>(0, target->life - floor), amount * std::clamp(100 - raw, 0, 100) / 100);
            } else if(!impact.weapon) amount = int64_t(mitigateMonsterDamage(float(amount) / 256.f, resistance) * 256.f);
            uint64_t cold = 0;
            const int coldResistance=impact.weapon?ports_.effects.unitResistance(target->id,DamageType::Cold,tick.tick):raw;
            if (impact.coldFrames && coldResistance < 100) {
                const int divisor = (impact.freeze ? impact.freezeDivisor : impact.coldDivisor).at(size_t(target->rule.difficulty));
                cold = impact.coldFrames * uint64_t(std::clamp(100 - coldResistance, 0, 200)) / (100u * unsigned(divisor));
            }
            std::optional<PoisonApplication> poison;
            const auto rate=impact.weapon?impact.weapon->channels[5]:impact.type==DamageType::Poison?(impact.targetDamage.empty()?impact.damage:impact.targetDamage[impact.next]):0;
            const auto frames=impact.weapon?uint64_t(std::max(0,impact.weapon->poisonFrames)):impact.poisonFrames;
            if(rate>0 && frames) poison=PoisonApplication{int64_t(mitigateMonsterDamage(float(rate)/256.f,ports_.effects.unitResistance(target->id,DamageType::Poison,tick.tick))*256.f),frames,owner->rules.skills->poisonState};
            if(impact.type==DamageType::Poison) amount=0;
            if(impact.weapon && impact.weapon->wearChance>0 &&
                limitedRandom(random,100)<unsigned(impact.weapon->wearChance)) {
                const auto &attack=*impact.weapon;
                const ActorContext actor{owner->player,owner->actor,owner->area,area->generation,0,tick.tick};
                auto wear=ports_.inventory.weaponCost(actor,attack.weapon,attack.wearSkill,false,false,unsigned(std::max(0,attack.wearAmount)));
                if(!wear) {if(wear.status==DomainStatus::Capacity) {blocked=true;break;} ++impact.next;continue;}
                const auto result=ports_.transactions.commit(std::move(*wear.value));
                if(!result) {if(result.status==DomainStatus::Capacity) {blocked=true;break;} ++impact.next;continue;}
            }
            const ActorContext itemActor{owner->player,owner->actor,owner->area,area->generation,0,tick.tick};
            auto itemEvents=impact.weapon?ports_.effects.prepareItemEvents(itemActor,{ItemSkillEvent::Hit,ItemSkillEvent::Kill},target->id,target->position):ports_.effects.prepareItemEvents(itemActor,{ItemSkillEvent::Kill},target->id,target->position);
            if(!itemEvents) {blocked=true;break;}
            const auto result = ports_.monsters.damage(target->id, impact.source, amount, tick.tick, cold, impact.freeze,uint8_t(impact.weapon?impact.weapon->weapon.hitClass:impact.hitClass),poison,impact.type==DamageType::Poison && !impact.weapon);
            if (result.status == DomainStatus::Capacity) { blocked = true; break; }
            if(result) {if(impact.weapon) ports_.random=random;ports_.effects.commitItemEvents(std::move(*itemEvents.value),target->life<=0);}
            if (result && impact.nextDelay) ports_.monsters.hitDelay(target->id, tick.tick + impact.nextDelay);
            if(result && impact.knockback && target->life>0) {ports_.monsters.knockback(target->id,owner->position,tick.tick);cancel(target->id);}
            ++impact.next;
        }
        if (impact.next == impact.targets.size()) { state_.spellTargets -= impact.targets.size(); it = state_.spells.erase(it); }
        else ++it;
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
