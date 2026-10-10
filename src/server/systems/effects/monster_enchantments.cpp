#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::effects {
DomainResult<> System::monsterAura(EntityId source,const AuraDefinition &a,EntityId id,uint64_t tick,uint64_t duration,bool owner) {
    const auto *m=ports_.monsters.find(source);if(!m) return {DomainStatus::InvalidActor,{}};
    const auto state=owner?a.ownerState:a.state;
    if(state.id<0) return {DomainStatus::Applied,std::monostate{}};
    CombatEffectSpec spec;spec.state=state;spec.modifiers=owner?auraOwnerModifiers(a):a.modifiers;spec.duration=duration;
    spec.source={CombatEffectSource::Monster,source,a.skill,a.rank};
    spec.stacking=EffectStacking::AuraLevel;
    for(const auto &[key,p]:ports_.players.all()) if(p.actor==id && p.entered && p.area==m->area && p.persistent.player.hp>0) {
        if(a.skill==123 && p.rules.skills && p.rules.skills->auras.contains(123)) {
            const auto state=p.rules.skills->auras.at(123).spec.ownerState.id;
            const auto effects=state_.players.find(p.actor);
            if(effects!=state_.players.end()) for(const auto &effect:effects->second.states.entries())
                if(effect.activeAt(tick) && effect.spec.state.id==state && effect.spec.source.level>a.rank) return {DomainStatus::Applied,std::monostate{}};
        }
        return apply({key,id,p.area,ports_.areas.at(p.area).generation,0,tick},std::move(spec));
    }
    const auto *target=ports_.monsters.find(id);
    if(!target || target->area!=m->area || target->life<=0) return {DomainStatus::InvalidActor,{}};
    if(state.curse && target->rule.enchantment && target->rule.enchantment->has(38)) return {DomainStatus::Applied,std::monostate{}};
    if(a.skill==123) {
        const auto previous=stateModifiers(id,state.id,tick),existing=unitModifiers(id,tick);
        if(target->rule.resistances[2]+existing.fireResist-previous.fireResist>=100) spec.modifiers.fireResist/=5;
        if(target->rule.resistances[3]+existing.lightningResist-previous.lightningResist>=100) spec.modifiers.lightningResist/=5;
        if(target->rule.resistances[4]+existing.coldResist-previous.coldResist>=100) spec.modifiers.coldResist/=5;
    }
    auto next=units_;auto &unit=next[id];unit.area=m->area;
    if(unit.states.size()>=128 && !unit.states.hasState(state.id,tick)) return {DomainStatus::Capacity,{}};
    if(!unit.states.apply(std::move(spec),tick).accepted) return {DomainStatus::Conflict,{}};
    std::vector<std::pair<int,int64_t>> stats;
    if(const auto found=m->rule.auraStats.find(a.skill);found!=m->rule.auraStats.end()) stats=owner?found->second.owner:found->second.targets;
    const auto previous=unit.nativeStats.find(state.id);
    const bool changed=previous==unit.nativeStats.end() || previous->second!=stats || !unitStates(id,tick).contains(state.id);
    unit.nativeStats[state.id]=stats;
    if(changed && !ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},m->area},{StateFact{id,1,m->area,state.id,true,std::move(stats)}}})) return {DomainStatus::Capacity,{}};
    ports_.monsters.velocityModifier(id,unit.states.modifiers(tick).velocityPercent);
    units_.swap(next);return {DomainStatus::Applied,std::monostate{}};
}
void System::triggerMonsterCurse(EntityId id,uint64_t tick) {
    const auto *m=ports_.monsters.find(id);
    if(!m || !m->rule.enchantment || !m->rule.enchantment->curse) return;
    auto [it,fresh]=uniqueCycles_.try_emplace(id);auto &cycle=it->second;
    if(fresh) cycle.random=m->combatRandom;
    // UMod7's native callback casts on three of four rolls around the owner.
    if(!(rollRandom(cycle.random)&3)) return;
    if(cycle.cursePending) return;
    const auto &a=*m->rule.enchantment->curse;const auto &area=ports_.areas.at(m->area).definition;
    const float radius=std::clamp(a.radius,1.f,40.f);cycle.curseTargets.clear();cycle.curseTarget=0;
    for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==m->area && p.persistent.player.hp>0 && (p.position-m->position).length()<=radius && area.activation.nearby(p.position,m->position)) cycle.curseTargets.push_back(p.actor);}
    for(const auto &[key,p]:ports_.monsters.read().actors) if(p.amazonPet && p.area==m->area && p.life>0 && (p.position-m->position).length()<=radius && area.activation.nearby(p.position,m->position)) cycle.curseTargets.push_back(key);
    cycle.cursePending=true;(void)tick;
}
StepStatus System::advanceMonsterEnchantments(uint64_t tick) {
    bool blocked=false;
    for(const auto &[id,m]:ports_.monsters.read().actors) {
        if(m.owner || !m.rule.enchantment) continue;
        const auto &mods=*m.rule.enchantment;const auto &area=ports_.areas.at(m.area);
        auto [entry,fresh]=uniqueCycles_.try_emplace(id);auto &cycle=entry->second;if(fresh) cycle.random=m.combatRandom;
        if(cycle.cursePending && mods.curse) {
            while(cycle.curseTarget<cycle.curseTargets.size()) {
                const auto result=monsterAura(id,*mods.curse,cycle.curseTargets[cycle.curseTarget],tick,uint64_t(std::max(1,mods.curse->periodFrames)));
                if(result.status==DomainStatus::Capacity) {blocked=true;break;}
                ++cycle.curseTarget;
            }
            if(cycle.curseTarget==cycle.curseTargets.size()) cycle.cursePending=false;
        }
        auto burst=[&](int definition,bool dead)->DomainResult<> {
            const auto found=m.rule.enchantmentMissiles.find(definition);if(found==m.rule.enchantmentMissiles.end()) return {DomainStatus::Unavailable,{}};
            MonsterAttackRule attack;attack.missile=found->second;
            missiles::MonsterSpawn spawn{id,m.area,area.generation,tick,m.position,m.position,attack,m.rule.hitStates,m.rule.level,0,0,cycle.random};spawn.postMortem=dead;
            if(definition==195) for(const auto &ray:monsterLightningRays()) spawn.aims.push_back(m.position+ray.offset);
            else for(const auto heading:missileRingBurst(2)) spawn.aims.push_back(m.position+heading);
            const auto result=ports_.missiles.spawnMonster(spawn);if(result) cycle.random=result.value->random;
            return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
        };
        const bool unique=m.identity.rank==MonsterRank::Unique || m.identity.rank==MonsterRank::SuperUnique;
        if(unique && mods.has(17) && m.damageOccurrence!=cycle.damage) {
            cycle.damage=m.damageOccurrence;
            const bool getHit=m.reactionUntil>tick && m.reactionMode==3;
            cycle.lightning=tick+(getHit?2:0);
        }
        if(cycle.lightning && tick>=cycle.lightning) {
            if(!ports_.events.hasCapacity(2)) {blocked=true;continue;}
            bool emitted=false;
            if(tick>=std::max(uint64_t(10),cycle.nextLightning)) {
                const auto result=burst(195,m.life<=0);if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
                emitted=bool(result);if(emitted) cycle.nextLightning=tick+10;
            }
            ports_.monsters.lightningEmission(id,emitted,tick);
            cycle.lightning=0;
        }
        if(m.life<=0 && m.deathOccurrence!=cycle.deathOccurrence) {cycle.deathOccurrence=m.deathOccurrence;cycle.death=m.deathTick+4;cycle.deathPhase=0;}
        if(cycle.death && tick>=cycle.death) {
            if(cycle.deathPhase==0) {
                if(mods.has(9)) {
                    auto random=cycle.random;
                    const int low=int(mods.corpseExplosionMinimum*4),high=int(mods.corpseExplosionMaximum*4);
                    const int64_t damage=int64_t(low+(high>low?limitedRandom(random,unsigned(high-low)):0))*64;
                    std::vector<EntityId> targets;
                    auto eligible=[&](Vec p){const Vec delta{std::floor(p.x)-std::floor(m.position.x),std::floor(p.y)-std::floor(m.position.y)};return delta.length()<=m.rule.difficulty+4 && area.definition.activation.nearby(p,m.position) && area.definition.collision.missileSegment(m.position,p,{0x0805,1});};
                    for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==m.area && p.persistent.player.hp>0 && eligible(p.position)) targets.push_back(p.actor);}
                    for(const auto &[key,p]:ports_.monsters.read().actors) if(p.amazonPet && p.life>0 && p.area==m.area && eligible(p.position)) targets.push_back(key);
                    combat::SpellImpact impact{id,id,m.area,DamageType::Physical,0,std::move(targets)};impact.occurrence=(uint64_t(1)<<62)|m.deathOccurrence;impact.reaction=true;impact.unblockable=true;
                    impact.monsterHit=MonsterHit{};impact.monsterHit->channels[0]=damage;impact.monsterHit->channels[2]=damage;impact.monsterStates=m.rule.hitStates;
                    auto plan=ports_.combat.prepareSpells({impact});if(!plan || !ports_.events.hasCapacity(1)) {blocked=true;continue;}
                    // Original fire-enchanted death uses the original visual missile.
                    if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},m.area},{MissileFact{id,1,m.area,117,1,0,m.position,m.position}}})) {blocked=true;continue;}
                    ports_.combat.commitSpells(std::move(*plan.value));cycle.random=random;
                }
                cycle.deathPhase=1;
            }
            if(cycle.deathPhase==1 && unique && mods.has(18)) {const auto result=burst(194,true);if(result.status==DomainStatus::Capacity) {blocked=true;continue;}}
            cycle.death=0;cycle.deathPhase=2;
        }
        if(m.life<=0 || !mods.aura) continue;
        const auto &a=*mods.aura;
        if(!cycle.auraPending && tick>=cycle.nextAura) {
            cycle.targets.clear();cycle.auraPending=true;
            cycle.auraTarget=0;
            if(a.hostile) {
                for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==m.area && p.persistent.player.hp>0 && (p.position-m.position).length()<=a.radius && area.definition.activation.nearby(p.position,m.position)) cycle.targets.push_back(p.actor);}
                for(const auto &[key,p]:ports_.monsters.read().actors) if(p.amazonPet && p.life>0 && p.area==m.area && (p.position-m.position).length()<=a.radius) cycle.targets.push_back(key);
            } else for(const auto &[key,p]:ports_.monsters.read().actors) if(!p.owner && p.life>0 && p.area==m.area && (p.position-m.position).length()<=a.radius && area.definition.activation.nearby(p.position,m.position)) cycle.targets.push_back(key);
            if(a.hostile) cycle.targets.push_back(id);
        }
        if(!cycle.auraPending) continue;
        while(cycle.auraTarget<cycle.targets.size()) {
            const auto result=monsterAura(id,a,cycle.targets[cycle.auraTarget],tick,uint64_t(std::max(1,a.periodFrames))*2,cycle.targets[cycle.auraTarget]==id);
            if(result.status==DomainStatus::Capacity) {blocked=true;break;}
            ++cycle.auraTarget;
        }
        if(cycle.auraTarget==cycle.targets.size()) {
            if(a.element>=0 && a.maximumDamage>0) {
                auto random=cycle.random;std::vector<EntityId> targets;
                for(const auto target:cycle.targets) if(target!=id) targets.push_back(target);
                const int64_t low=int64_t(a.minimumDamage*256),spread=int64_t((a.maximumDamage-a.minimumDamage)*256);
                combat::SpellImpact pulse{id,id,m.area,DamageType(a.element),low+(spread>0?limitedRandom(random,uint32_t(spread)):0),std::move(targets)};
                pulse.occurrence=(uint64_t(1)<<63)|++cycle.auraOccurrence;pulse.hitClass=uint8_t(a.hitClass);pulse.reaction=true;pulse.unblockable=true;
                auto plan=ports_.combat.prepareSpells({pulse});
                if(!plan) {--cycle.auraOccurrence;if(plan.status==DomainStatus::Capacity) blocked=true;continue;}
                ports_.combat.commitSpells(std::move(*plan.value));cycle.random=random;
            }
            cycle.targets.clear();cycle.auraPending=false;cycle.nextAura=tick+uint64_t(std::max(1,a.periodFrames));
        }
    }
    std::erase_if(uniqueCycles_,[&](const auto &entry){return !ports_.monsters.find(entry.first);});
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
