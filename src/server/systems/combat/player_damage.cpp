#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/combat/weapon_hit.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::combat {
DomainStatus System::resolvePlayerImpact(SpellImpact &impact,ParticipantView source,const PlayerState &owner,
    const PlayerState &target,uint64_t generation,TickContext tick) {
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(!ParticipantView{&target,nullptr}.aliveIn(impact.area) || !participants.canHarm(source,{&target,nullptr}) ||
       impact.undeadOnly || !owner.rules.skills || (impact.nextDelay && !ports_.effects.missileHitAllowed(target.actor,tick.tick))) return DomainStatus::InvalidActor;
    if(!ports_.events.hasCapacity(12,4) || !ports_.effects.reactionCapacity()) return DomainStatus::Capacity;
    if(!impact.targetRandom || impact.plannedTarget!=impact.next) {
        impact.plannedTarget=impact.next;
        impact.targetRandom=impact.contactRandom?childRandom(*impact.contactRandom):childRandom(ports_.random);
        impact.wearApplied=false;impact.frenzyApplied=false;
    }
    auto random=*impact.targetRandom;
    const ActorContext victim{target.player,target.actor,target.area,generation,0,tick.tick};
    const ActorContext attacker{owner.player,owner.actor,owner.area,generation,0,tick.tick};
    if(impact.weapon && !impact.weapon->projectile && !impact.reaction && impact.meleeReactionTarget!=target.actor) {
        ports_.effects.react(victim,impact.source,CombatEffectEvent::AttackedInMelee);
        impact.meleeReactionTarget=target.actor;
    }
    const bool running=target.moving && target.runningNow;
    if(impact.weapon && !(impact.weaponHit.has_value()?*impact.weaponHit:weaponContact(*impact.weapon,target.actor,tick.tick,random))) return DomainStatus::Applied;
    if(impact.monsterToHit && !running && limitedRandom(random,100)>=unsigned(physicalHitChance(impact.monsterLevel,impact.monsterRating,target.persistent.player.level,target.totals.character.defense))) return DomainStatus::Applied;
    const bool physicalMissile=impact.weapon || (impact.monsterHit?impact.monsterHit->channels[0]>0:impact.type==DamageType::Physical && impact.damage>0);
    if(!impact.unblockable && physicalMissile && !(impact.weapon && impact.weapon->smite) && target.totals.equipment.shield &&
       limitedRandom(random,100)<unsigned(target.totals.equipment.blockChance/(running?3:1))) return ports_.effects.block(victim).status;
    const bool missile=!impact.weapon || impact.weapon->projectile;
    const auto avoided=impact.unblockable?WeaponAvoidance::None:rollWeaponAvoidance(target.totals.character.combat,target.moving,missile,random);
    if(avoided!=WeaponAvoidance::None) return ports_.effects.avoidance(victim,avoided,impact.source).status;
    MonsterHit hit=impact.monsterHit.value_or(MonsterHit{});
    hit.coldFrames=std::max(hit.coldFrames,impact.coldFrames);hit.poisonFrames=std::max(hit.poisonFrames,impact.poisonFrames);hit.stunFrames=std::max(hit.stunFrames,impact.stunFrames);
    hit.knockback=hit.knockback || impact.knockback;hit.hitClass=impact.hitClass;
    if(impact.weapon) {
        hit.channels=targetWeaponChannels(*impact.weapon,false,false);
        hit.poisonFrames=uint64_t(std::max(0,impact.weapon->poisonFrames));
        hit.stunFrames=uint64_t(std::max(0,impact.weapon->stunFrames));
        hit.hitClass=impact.weapon->weapon.hitClass;hit.critical=impact.weapon->critical;
        hit.knockback=hit.knockback || impact.weapon->knockback;
    } else if(!impact.monsterHit) hit.channels[size_t(impact.type)]=impact.targetDamage.empty()?impact.damage:impact.targetDamage[impact.next];
    if(impact.staticPercent) {
        const auto life=int64_t(target.persistent.player.hp);
        if(life<1 || life<=int64_t(target.totals.character.maxLife)*impact.staticFloors.at(size_t(target.persistent.difficulty))/100) return DomainStatus::Applied;
        auto amount=std::max(impact.minimumStaticDamage,std::min(life-1,life*impact.staticPercent/100)*256);
        if(target.totals.character.lightningResist<0) amount=amount*100/(100-target.totals.character.lightningResist);
        hit.channels[size_t(DamageType::Lightning)]=amount;
    }
    // SUNITDMG_CalculateTotalDamage: 17%, before absorb/reduction/resistance;
    // duration channels do not receive this penalty. Other pets retain 100%.
    const int percent=source.player || (source.monster && (source.monster->hireling || source.monster->revived()))?17:100;
    for(auto &channel:hit.channels) channel=channel*percent/100;
    auto hitClass=state_.hitClassCursor;hit.hitClass=monsterDamageHitClass(hit,hit.hitClass,hitClass);
    effects::PlayerHitAdjustment adjustment;adjustment.credit=impact.binding.credit;
    adjustment.coldPierce=impact.coldPierce;adjustment.poisonPierce=impact.poisonPierce;
    WeaponHitResult extras;std::optional<WeaponSkillDamage> leech;
    const auto &rules=*owner.rules.skills;
    if(impact.weapon) {
        leech=*impact.weapon;leech->lifeLeech=leech->lifeLeech*percent/100;leech->manaLeech=leech->manaLeech*percent/100;
        extras=calculateWeaponHitEffects(*leech,{int64_t(target.persistent.player.hp*256.f),target.totals.character.combat.physicalResist,100,1,false,false,false,true},0,{},random);
        adjustment.crushing=extras.crushing;
        if(extras.woundRate>0) adjustment.wound=OpenWoundsApplication{extras.woundRate,impact.source,impact.binding.credit,tick.tick+200,tick.tick+1};
        if(impact.weapon->wearSkill.weapon && impact.weapon->wearSkill.weapon->frenzy && !impact.frenzyApplied) {
            const auto result=ports_.effects.frenzy(attacker,impact.weapon->wearSkill);if(!result) return result.status;
            impact.frenzyApplied=true;
        }
        const bool wear=impact.weapon->wearChance>0 && limitedRandom(random,100)<unsigned(impact.weapon->wearChance);
        if(source.player && wear && !impact.wearApplied) {
            auto cost=ports_.inventory.weaponCost(attacker,impact.weapon->weapon,impact.weapon->wearSkill,false,false,unsigned(std::max(0,impact.weapon->wearAmount)));
            if(!cost) return cost.status;
            const auto committed=ports_.transactions.commit(std::move(*cost.value));if(!committed) return committed.status;
            impact.wearApplied=true;
        }
    }
    const bool whirlwind=impact.weapon && impact.weapon->wearSkill.weapon && impact.weapon->wearSkill.weapon->whirlwind;
    auto events=source.player && !whirlwind?ports_.effects.prepareItemEvents(attacker,impact.weapon?std::initializer_list{ItemSkillEvent::Hit,ItemSkillEvent::Kill}:std::initializer_list{ItemSkillEvent::Kill},target.actor,target.position):DomainResult<effects::ItemEventPlan>{DomainStatus::Applied,effects::ItemEventPlan{}};
    if(!events) return events.status;
    const auto wear=impact.weapon && !impact.weapon->projectile?ports_.inventory.defensiveWear(target.player,random):std::nullopt;
    const auto remainingLife=std::max<int64_t>(0,int64_t(target.persistent.player.hp*256.f)-extras.crushing);
    SpellPlan reflected;
    if(!missile && !impact.reaction && hit.channels[0]>0) {
        const int thorns=target.totals.character.combat.thornsPercent;
        const int percent=source.player || (source.monster && source.monster->hireling)?(thorns+4)/8:thorns;
        const auto physical=ports_.effects.physicalContactDamage(target.actor,hit.channels[0],tick.tick);
        if(percent>0 && physical>0) {
            SpellImpact reflection{target.actor,target.actor,impact.area,DamageType::Physical,std::min<int64_t>(INT32_MAX,physical*percent/100),{impact.source}};
            if(state_.reflectionOccurrence>=(uint64_t(1)<<58)-1) return DomainStatus::Capacity;
            reflection.occurrence=(uint64_t(1)<<58)|(state_.reflectionOccurrence+1);reflection.reaction=true;reflection.unblockable=true;reflection.hitClass=0x8d;
            auto plan=prepareSpells({reflection});if(!plan) return plan.status;reflected=std::move(*plan.value);
        }
    }
    const auto received=ports_.effects.receiveMonster(victim,impact.source,hit,{rules.chilled,rules.poisoned,{}},wear,std::ref(random),adjustment);
    if(!received) return received.status;
    state_.hitClassCursor=hitClass;
    if(!reflected.spells.empty()) ++state_.reflectionOccurrence;
    commitSpells(std::move(reflected));
    if(!impact.reaction) ports_.effects.necroContact(impact.source,target.actor,received.value->physical,tick.tick,missile,remainingLife);
    ports_.effects.commitItemEvents(std::move(*events.value),target.persistent.player.hp<=0);
    if(impact.nextDelay) ports_.effects.missileHitDelay(target.actor,tick.tick+impact.nextDelay);
    if(!impact.reaction && received.value->dealt>0) ports_.effects.react(victim,impact.source,missile?CombatEffectEvent::HitByMissile:CombatEffectEvent::DamagedInMelee,impact.returnFire || !missile);
    if(leech) {
        leech->crushingBlow=leech->openWounds=0;
        const auto difficulty=size_t(owner.persistent.difficulty);
        const auto divisors=source.player?LeechDivisors{rules.lifeStealDivisor.at(difficulty),rules.manaStealDivisor.at(difficulty)}:LeechDivisors{};
        const auto gains=calculateWeaponHitEffects(*leech,{remainingLife,0,100,1,false,false,false,true},received.value->physical,divisors,random);
        impact.lifeGain=gains.life;impact.manaGain=gains.mana;
        impact.selfDamage=source.player?received.value->physical*leech->selfDamagePercent/100:0;
        if(gains.life && gains.mana) impact.leechOverlay=limitedRandom(random,2)?rules.manaStealOverlay:rules.lifeStealOverlay;
        else if(gains.life || gains.mana) impact.leechOverlay=gains.life?rules.lifeStealOverlay:rules.manaStealOverlay;
        if(extras.crushing>0) impact.contactOverlay=rules.crushingOverlay;
    }
    impact.conversion.reset(); // Conversion never changes a player's allegiance.
    impact.targetApplied=true;
    return DomainStatus::Applied;
}
}
