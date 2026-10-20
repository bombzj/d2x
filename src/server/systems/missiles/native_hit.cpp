#include "system.hpp"
#include "runtime.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "gameplay/monsters/types.hpp"
#include "gameplay/skills/amazon_missile.hpp"

namespace d2x::server::missiles {
std::unique_ptr<System::Advance> System::immediateHit(const Missile &source,EntityId target,uint64_t tick) const {
    auto missile=source;missile.executionTick=tick;
    auto result=std::make_unique<Advance>(Advance{missile,{},{},false});auto &plan=*result;
    plan.damageRolls=damageRolls(missile);
    plan.collisionFlight=Advance::CollisionFlight{missile.position,missile.position,{},0,false,true,false};
    plan.collisionFlight->forceRemove=true;
    if(target) {
        if(missile.nativeDamage->rule.lastCollide && target==(missile.lastHit?missile.lastHit:missile.owner)) return result;
        plan.next.lastHit=target;
        plan.addMissileContact(missile.enemy?enemyImpact(plan,{target}):impact(plan,{target}));
    } else {
        const auto native=nativeHit(plan,{});
        if(!native) {if(missile.enemy) throw std::logic_error("Unsupported native monster missile hit program");else projectileImpact(plan,{});}
        plan.finished=!native || !(*native&4);
    }
    return result;
}
std::optional<uint8_t> System::nativeHit(Advance &plan,EntityId target) const {
    auto &missile=plan.next;const auto &rule=missile.nativeDamage->rule;
    const auto child=rule.hitSubMissiles[0];
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(!participants.find(missile.owner).id() && (rule.serverHitFunction==1 || rule.serverHitFunction==29 ||
        rule.serverHitFunction==38 || rule.serverHitFunction==47 || rule.serverHitFunction==53 || rule.serverHitFunction==56 ||
        rule.serverHitFunction==58 || rule.serverHitFunction==59)) return 1;
    // Return the native bit mask: damage=2, remove=1, early continuation=4.
    // Null function slots leave the caller's initial CollideKill result intact.
    if(rule.serverHitFunction==0 || rule.serverHitFunction>=60 || rule.serverHitFunction==30 || rule.serverHitFunction==34 ||
        rule.serverHitFunction==41 || rule.serverHitFunction==42 || rule.serverHitFunction==46 || rule.serverHitFunction==49)
        return uint8_t((rule.explosion?0:2)|(rule.killOnHit?1:0));
    switch(rule.serverHitFunction) {
    case 54: {
        const auto *owner=ports_.monsters.find(missile.owner);
        if(!target && owner && owner->rule.formulas && owner->rule.baseSkillRanks.contains(missile.skill.sourceId)) {
            const auto skill=owner->rule.formulas->skillIds.find("baal monster spawn");
            if(skill!=owner->rule.formulas->skillIds.end() && skill->second==missile.skill.sourceId) {
                plan.operations.push_back({Advance::OperationKind::BaalWave,plan.baalWaves.size()});
                plan.baalWaves.push_back({missile.area,missile.position,owner->baalSpawnPreset});
            }
        }
        return 1;
    }
    case 39: {
        auto observer=missile.position;
        if(const auto live=state_.missiles.find(missile.id);live!=state_.missiles.end() && live->second.area==missile.area)
            observer=live->second.position;
        plan.operations.push_back({Advance::OperationKind::LevelSpawn,plan.levelSpawns.size()});
        plan.levelSpawns.push_back({missile.area,observer,missile.position,{}});return 1;
    }
    case 35:
        if(target || !ports_.objects.read().objects.contains(EntityId{uint32_t(missile.parameters[0])})) return 0;
        plan.operations.push_back({Advance::OperationKind::OrbMist,uint32_t(missile.parameters[0])});return 1;
    case 32:
        if(!target && !missile.parameters[0]) {
            missile.parameters[0]=1;plan.operations.push_back({Advance::OperationKind::CairnPortal,0});
        }
        return 0;
    case 33:
        if(!target) plan.operations.push_back({Advance::OperationKind::ReleaseRoom,0});
        return 0;
    case 57:
        if(!target && !missile.parameters[0]) plan.operations.push_back({Advance::OperationKind::BaalFx,0});
        return 0;
    case 4:case 11:
        for(const auto definition:rule.hitSubMissiles) if(definition>0) {
            auto &next=planNativeChild(plan,definition,missile.position,{});
            if(rule.hitParameters[0]>0) next.hitOnCreation=target;
        }
        return rule.serverHitFunction==4?3:1;
    case 2: {
        if(child<0 || !participants.find(missile.owner).id()) return 3;
        const auto point=target?unitPosition(missile,target):std::optional<Vec>{missile.position};
        if(!point) return 3;
        const auto nextRule=nativeChildRule(missile,child);
        for(const auto heading:poisonCloudDirections(std::max(rule.hitParameters[1],1),rule.hitParameters[0])) {
            auto &next=planNativeChild(plan,child,*point,heading.direction,0,rule.hitParameters[2]);
            const auto parameter=nextRule.serverParameters[heading.secondary?1:0];
            next.velocity=heading.direction.unit()*(float(poisonCloudVelocity(parameter))*25.f/4096.f);
        }
        return 3;
    }
    case 3:
        if(target) return 0;
        [[fallthrough]];
    case 44: {
        auto range=rule.hitParameters[0];
        if(!range) {const auto value=missileSkillFormula(plan,"aurarangecalc");if(!value) return 1;range=std::max(*value,1);}
        areaImpact(plan,missile.position,range,true);return 1;
    }
    case 5:
        if(child>=0 && participants.find(missile.owner).id())
            for(const auto direction:missileRingBurst(std::max(rule.hitParameters[0],1))) {
                auto &next=planNativeChild(plan,child,missile.position,direction);
                next.parameters={int32_t(direction.x),int32_t(direction.y)};
            }
        return 1;
    case 7: {
        const auto recipient=participants.find(target),owner=participants.find(missile.owner);
        if(!recipient.id()) return 1;
        const auto skill=rule.damageFormulas->skills.find(missile.skill.sourceId);
        const bool heal=rule.hitParameters[0] && owner.id() && skill!=rule.damageFormulas->skills.end() &&
            participants.skillFriendlyTarget(owner,recipient,skill->second.number("targetpet")!=0,skill->second.number("targetally")!=0);
        if(!heal) {
            if(!rule.hitParameters[1]) return 3;
            if(recipient.player) return 4;
            return (rule.hitParameters[1]==2?recipient.monster->rule.demon:recipient.monster->rule.undead)?3:4;
        }
        const auto minimum=missileSkillFormula(plan,"calc1"),maximum=missileSkillFormula(plan,"calc2");
        if(!minimum || !maximum) return 1;
        const auto low=nativeShiftLeft(*minimum,8),high=nativeShiftLeft(*maximum,8);
        auto amount=nativeAdd(low,int32_t(limitedRandom(plan.damageRolls->random,uint32_t(nativeSubtract(high,low)))));
        if(amount<=0) amount=rollMissileDamage(plan.damageRolls->data.physical,plan.damageRolls->random);
        const auto current=recipient.player?recipient.player->persistent.player.hp.raw():nativeChecked(recipient.monster->life);
        const auto limit=recipient.player?recipient.player->totals.character.maxLifeFixed.raw():nativeChecked(recipient.monster->maximumLife);
        plan.operations.push_back({Advance::OperationKind::Resource,plan.resources.size()});
        plan.resources.push_back({Advance::ResourceWrite::Kind::Life,target,std::min(nativeAdd(current,amount),limit),rule.programOverlay});
        return 1;
    }
    case 1: {
        auto radius=rule.hitParameters[0];
        if(radius<=0) {const auto value=missileSkillFormula(plan,"calc1");if(!value) return 1;radius=std::max(*value,1);}
        areaImpact(plan,missile.position,radius);return 1;
    }
    case 8: {
        if(target!=missile.owner) return 2;
        const auto blaze=rule.damageFormulas->skillIds.find("blaze");
        if(blaze==rule.damageFormulas->skillIds.end()) return 2;
        const auto skill=rule.damageFormulas->skills.find(blaze->second);
        const auto *states=ports_.effects.unitEffects(target);
        return skill!=rule.damageFormulas->skills.end() && states &&
            states->hasState(skill->second.auraState,missile.executionTick)?0:2;
    }
    case 9: {
        if(!rule.damageFormulas->skills.contains(missile.skill.sourceId)) return 1;
        if(child>=0) {
            missile.nativeDamage->data.coldLength=0;plan.damageRolls->data.coldLength=0;
            auto range=rule.hitParameters[0];
            if(range<=0) {const auto value=missileSkillFormula(plan,"calc1");if(!value) return 1;range=std::max(*value,1);}
            const auto frames=missileSkillFormula(plan,"shitcalc1");if(!frames) return 1;
            const auto &area=ports_.areas.at(missile.area);
            for(const auto offset:missileDiskOffsets(range)) {
                const auto point=missile.position+offset;
                if(area.definition.collision.missileSegment(point,point+offset,{4,1}))
                    planNativeChild(plan,child,point,{},*frames);
            }
        }
        auto range=rule.hitParameters[1];
        if(range<=0) {const auto value=missileSkillFormula(plan,"calc2");if(!value) return 1;range=std::max(*value,1);}
        areaImpact(plan,missile.position,range);return 3;
    }
    case 13: {
        if(!rule.damageFormulas->skills.contains(missile.skill.sourceId)) return 1;
        auto range=rule.hitParameters[0],length=rule.hitParameters[1];
        if(range<=0) {const auto value=missileSkillFormula(plan,"aurarangecalc");if(!value) return 1;range=std::max(*value,1);}
        if(length<=0) {const auto value=missileSkillFormula(plan,"auralencalc");if(!value) return 1;length=*value;}
        areaImpact(plan,missile.position,range,false,{},length);return 1;
    }
    case 12: {
        if(!target || !participants.find(missile.owner).id() || missile.parameters[0]<=1) return 3;
        auto range=rule.hitParameters[0];
        if(range<=0) {const auto value=missileSkillFormula(plan,"aurarangecalc");if(!value) return 3;range=std::max(*value,1);}
        const auto &area=ports_.areas.at(missile.area);std::vector<uint64_t> candidates;
        const auto targets=ports_.effects.nativeAuraCandidateRecipients({missile.owner,unitArea(missile.owner),range,0x88583u|0xA783u,false,
            missile.position+area.definition.origin,effects::NativeAuraTargetMode::TargetRange,missile.executionTick});
        for(const auto id:targets) candidates.push_back(id.value);
        const EntityId selected{missileChainSuccessor(target.value,candidates)};
        if(selected && selected!=target) if(const auto point=unitPosition(missile,selected)) {
            auto &next=planNativeChild(plan,missile.definition,missile.position,*point-missile.position);
            next.parameters[0]=nativeSubtract(missile.parameters[0],1);next.guidance=selected;
        }
        return 3;
    }
    case 14: {
        const auto skill=rule.damageFormulas->skills.find(missile.skill.sourceId);
        if(skill==rule.damageFormulas->skills.end() || !participants.find(missile.owner).id()) return 1;
        auto range=rule.hitParameters[0];
        if(range<=0) {const auto value=missileSkillFormula(plan,"aurarangecalc");if(!value) return 1;range=std::max(*value,1);}
        areaImpact(plan,missile.position,range,true);
        const auto frames=missile.skill.rank>0?nativeAdd(skill->second.number("param3"),
            nativeMultiply(nativeSubtract(missile.skill.rank,1),skill->second.number("param4"))):0;
        constexpr std::array<Vec,18> offsets{Vec{2,-2},Vec{-2,-2},Vec{0,2},Vec{0,5},Vec{-3,3},Vec{0,3},Vec{3,3},Vec{-1,2},Vec{1,1},Vec{-1,-1},Vec{2,-1},Vec{-4,-2},Vec{-3,-2},Vec{-1,-3},Vec{0,-4},Vec{1,-3},Vec{3,-3},Vec{4,-2}};
        if(child>=0) for(size_t index=0;index<offsets.size();index+=size_t(std::max(rule.hitParameters[1],1)))
            planNativeChild(plan,child,missile.position+offsets[index],{},std::max(frames,0));
        return 1;
    }
    case 15:
        if(child<0 || !participants.find(missile.owner).id()) return 1;
        planNativeChild(plan,child,missile.position,{});return 0;
    case 16:
        if(!participants.find(missile.owner).id()) return 1;
        if(!target) return 0;
        if(const auto skill=rule.damageFormulas->skills.find(missile.skill.sourceId);
            skill!=rule.damageFormulas->skills.end() && skill->second.auraTargetState>=0) {
            plan.addSkillState(missile.owner,target,missile.skill.sourceId,missile.skill.rank,5);
            plan.skillStates.back().procedure=effects::NativeSkillStateProcedure::SpiderGoo;return 0;
        }
        return 1;
    case 17: {
        const auto skill=rule.damageFormulas->skills.find(missile.skill.sourceId);
        if(skill==rule.damageFormulas->skills.end() || !participants.find(missile.owner).id()) return 1;
        const auto *unit=ports_.monsters.find(target);if(!unit) return 0;
        if(!unit->rule.states || !unit->rule.states->contains(skill->second.auraTargetState)) return 1;
        if(const auto *states=ports_.effects.unitEffects(target);states && states->hasState(skill->second.auraTargetState,missile.executionTick)) return 0;
        const auto level=withMissileOwnerFormula(plan,[](SkillFormulaValues &values){return values.stat("level");});
        const auto targetStats=ports_.effects.unitStatValues(target,missile.executionTick);
        if(!level || !targetStats || nativeAdd(nativeAdd(missile.skill.rank,skill->second.number("param2")),*level)<=
            targetStats->get({unit->rule.stats->names.at("level"),0})) return 0;
        const auto terror=rule.damageFormulas->skillIds.find("terror");if(terror==rule.damageFormulas->skillIds.end()) return 0;
        const auto frames=missile.skill.rank>0?nativeAdd(skill->second.number("param5"),
            nativeMultiply(nativeSubtract(missile.skill.rank,1),skill->second.number("param6"))):0;
        plan.operations.push_back({Advance::OperationKind::Fear,plan.fears.size()});
        plan.fears.push_back({target,missile.owner,unit->rule.states->at(skill->second.auraTargetState),terror->second,1,frames,true,{}});
        return 0;
    }
    case 18:
        if(!participants.find(missile.owner).id()) return 1;
        if(target && participants.auraAligned(participants.find(missile.owner),participants.find(target))) {
            plan.addSkillState(missile.owner,target,missile.skill.sourceId,missile.skill.rank,0);
            plan.skillStates.back().procedure=effects::NativeSkillStateProcedure::Warcry;
        }
        return 0;
    case 20:case 22: {
        const bool heaven=rule.serverHitFunction==22;
        if(child<0 || !participants.find(missile.owner).id()) return 1;
        const auto skill=rule.damageFormulas->skills.find(missile.skill.sourceId);
        if(skill==rule.damageFormulas->skills.end()) return 1;
        std::optional<combat::SpellImpact> primaryDamage;
        if(heaven) {
            const auto primary=participants.find(EntityId{uint32_t(missile.parameters[1])});
            if(!primary.id() || (primary.player?0:1)!=missile.parameters[0]) return 0;
            auto damage=missile.enemy?enemyImpact(plan,{primary.id()}):impact(plan,{primary.id()});
            damage.nativeDamageFlagsOnly=true;damage.collideFriend=true;damage.returnFire=false;
            damage.nativeNextHit.reset();damage.nextDelay=0;primaryDamage=std::move(damage);
        }
        auto range=rule.hitParameters[0],count=rule.hitParameters[1];
        if(range<=0) {const auto value=missileSkillFormula(plan,"aurarangecalc");if(!value) return 1;range=std::max(*value,1);}
        if(count<=0) {const auto value=missileSkillFormula(plan,heaven?"calc4":"calc1");if(!value) return 1;count=std::max(*value,1);}
        const auto filter=uint32_t(skill->second.number("aurafilter"));
        if(primaryDamage) plan.addImpact(std::move(*primaryDamage));
        plan.operations.push_back({Advance::OperationKind::TargetBurst,plan.targetBursts.size()});
        plan.targetBursts.push_back({child,range,count,filter?filter:heaven?0xA683u:0xA783u,!heaven});
        return heaven?1:3;
    }
    case 21: {
        if(!target || !participants.find(missile.owner).id()) return 1;
        const auto skill=rule.damageFormulas->skills.find(missile.skill.sourceId);
        const auto point=unitPosition(missile,target);
        if(skill==rule.damageFormulas->skills.end() || !point) return 1;
        const auto filter=skill->second.number("aurafilter")?0xA783u:0u;
        const auto &area=ports_.areas.at(missile.area);
        const auto eligible=ports_.effects.nativeAuraCandidateRecipients({missile.owner,unitArea(missile.owner),0,filter,false,
            *point+area.definition.origin,effects::NativeAuraTargetMode::TargetRange,missile.executionTick});
        if(std::find(eligible.begin(),eligible.end(),target)==eligible.end()) return 1;
        plan.addSkillState(missile.owner,target,missile.skill.sourceId,missile.skill.rank,0);
        plan.skillStates.back().procedure=effects::NativeSkillStateProcedure::BattleCry;
        return 0;
    }
    case 24: {
        if(!participants.find(missile.owner).id()) return 1;
        auto range=rule.hitParameters[0];
        if(range<=0) {const auto value=missileSkillFormula(plan,"calc1");if(!value) return 1;range=std::max(*value,1);}
        areaImpact(plan,missile.position,range,true);return 1;
    }
    case 25: {
        if(child<0 || !participants.find(missile.owner).id()) return 1;
        const auto point=target?unitPosition(missile,target):std::optional<Vec>{missile.position};
        if(!point) return 1;
        const auto nextRule=nativeChildRule(missile,child);
        constexpr std::array<Vec,8> offsets{Vec{0,2},Vec{2,2},Vec{2,0},Vec{2,-2},Vec{0,-2},Vec{-2,-2},Vec{-2,0},Vec{-2,2}};
        for(size_t index=0;index<offsets.size();index+=size_t(std::max(rule.hitParameters[0],1))) {
            auto &next=planNativeChild(plan,child,*point,offsets[index],0,nativeSubtract(missile.skill.rank,1));
            next.velocity=offsets[index].unit()*(float(poisonCloudVelocity(nextRule.serverParameters[0]))*25.f/4096.f);
        }
        return 3;
    }
    case 26: {
        if(child<0 || !participants.find(missile.owner).id() || !rule.damageFormulas->skills.contains(missile.skill.sourceId)) return 1;
        auto frames=rule.hitParameters[0];
        if(frames<=0) {const auto value=missileSkillFormula(plan,"calc1");if(!value) return 1;frames=std::max(*value,5);}
        planNativeChild(plan,child,missile.position,{},frames);return 1;
    }
    case 27:return 1;
    case 28: {
        if(!participants.find(missile.owner).id()) return 1;
        const auto skill=rule.damageFormulas->skills.find(missile.skill.sourceId);
        const auto *unit=ports_.monsters.find(target);const auto ward=state_.missiles.find(EntityId{uint32_t(missile.parameters[0])});
        if(skill==rule.damageFormulas->skills.end() || !unit || ward==state_.missiles.end() || !unit->rule.states ||
            !unit->rule.states->contains(skill->second.auraTargetState)) return 1;
        const auto origin=ward->second.position+ports_.areas.at(ward->second.area).definition.origin;
        const auto point=unit->position+ports_.areas.at(unit->area).definition.origin;
        const auto range=missile.skill.rank>0?nativeAdd(skill->second.number("param1"),
            nativeMultiply(nativeSubtract(missile.skill.rank,1),skill->second.number("param2"))):0;
        const auto dx=nativeSubtract(int32_t(point.x),int32_t(origin.x)),dy=nativeSubtract(int32_t(point.y),int32_t(origin.y));
        const auto terror=rule.damageFormulas->skillIds.find("terror");
        if(terror!=rule.damageFormulas->skillIds.end() && nativeAdd(nativeMultiply(dx,dx),nativeMultiply(dy,dy))<nativeMultiply(range,range)) {
            plan.operations.push_back({Advance::OperationKind::Fear,plan.fears.size()});
            plan.fears.push_back({target,ward->first,unit->rule.states->at(skill->second.auraTargetState),terror->second,1,skill->second.number("param6"),true,origin});
        }
        return 1;
    }
    case 40: {
        if(child<0 || !participants.find(missile.owner).id()) return 1;
        auto count=nativeAdd(rule.hitParameters[0],nativeMultiply(nativeSubtract(missile.skill.rank,1),rule.hitParameters[1]));
        if(count<=0) {const auto value=missileSkillFormula(plan,"calc4");if(!value) return 1;count=std::max(*value,1);}
        const auto point=unitPosition(missile,missile.guidance).value_or(missile.pathTarget);
        const auto &area=ports_.areas.at(missile.area);
        uint64_t random=(plan.damageRolls->random&0xffffffff00000000ULL)|uint32_t(int32_t(std::floor(point.x+area.definition.origin.x)));
        const auto range=count/4;
        if(count<=1 || range<2) planNativeChild(plan,child,missile.position,point-missile.position);
        else for(int index=0;index<count;++index) {
            const auto x=nativeSubtract(int32_t(limitedRandom(random,uint32_t(nativeMultiply(2,range)))),range);
            const auto y=nativeSubtract(int32_t(limitedRandom(random,uint32_t(nativeMultiply(2,range)))),range);
            const auto destination=point+Vec{float(x),float(y)};
            const auto delta=destination-missile.position;
            if(delta.x*delta.x+delta.y*delta.y>=4) planNativeChild(plan,child,missile.position,delta);
        }
        plan.damageRolls->random=random;missile.random=random;return 1;
    }
    case 52: {
        if(child<0 || !participants.find(missile.owner).id()) return 1;
        // Retail 1.13c (6FC5C030) checks HitSubMissile1 but leaves the
        // constructor's missile identity at zero. Preserve the native call.
        constexpr std::array<Vec,8> offsets{Vec{16,0},Vec{16,16},Vec{0,16},Vec{-16,16},Vec{-16,0},Vec{-16,-16},Vec{0,-16},Vec{16,-16}};
        for(size_t index=0;index<offsets.size();index+=size_t(std::max(rule.hitParameters[0],1))) {
            auto &next=planNativeChild(plan,0,missile.position,offsets[index]);
            next.parameters={int32_t(offsets[index].x),int32_t(offsets[index].y)};
        }
        return 1;
    }
    case 10: {
        if(ports_.areas.at(missile.area).definition.town) return 1;
        const auto flags=missile.parameters[0];
        if(!target && ((flags&4) || (plan.collisionFlight && plan.collisionFlight->wall))) return 1;
        if(flags&1) return target && target!=missile.guidance?4:3;
        if(!(flags&2)) return 3;
        if(missile.currentFrame()>0) return flags&4?3:4;
        if(flags&4) return 1;
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        if(!participants.find(missile.owner).id()) return 1;
        const auto &area=ports_.areas.at(missile.area);
        const auto targets=ports_.effects.nativeAuraCandidateRecipients({missile.owner,unitArea(missile.owner),rule.serverParameters[1],0xA783u|3u,false,
            missile.position+area.definition.origin,effects::NativeAuraTargetMode::TargetRange,missile.executionTick});
        EntityId selected;
        for(const auto id:targets) if(unitPosition(missile,id) && (!selected || id<selected)) selected=id;
        const auto &row=rule.damageFormulas->missiles.at(missile.definition);
        const auto frames=nativeAdd(row.number("range"),nativeMultiply(row.number("levrange"),nativeSubtract(missile.skill.rank,1)));
        missile.setCurrentFrame(frames);
        if(selected) {
            missile.guidance=selected;missile.parameters[0]=5;
            const auto point=*unitPosition(missile,selected);missile.pathTarget=point;++missile.pathRevision;
            if(missileDistance(missile.position,point)>=25) return 4;
        } else {
            const auto packed=uint32_t(missile.parameters[1]);
            missile.pathTarget=missile.position+Vec{float(int16_t(packed)),float(int16_t(packed>>16))};
            missile.parameters[0]=6;
            if(missileDistance(missile.position,missile.pathTarget)>=25) return 4;
        }
        missile.turnTarget=missile.pathTarget-missile.position;
        missile.velocity=missile.turnTarget.unit()*missile.velocity.length();return 4;
    }
    case 29:
        if(child<0) return 1;
        if(missile.currentFrame()!=0) return 2;
        for(const auto direction:missileRingBurst(std::max(rule.hitParameters[0],1))) {
            auto &next=planNativeChild(plan,child,missile.position,direction);
            next.parameters={int32_t(direction.x),int32_t(direction.y)};
        }
        return 3;
    case 19:
        if(!target || !participants.find(missile.owner).id()) return 1;
        if(const auto skill=rule.damageFormulas->skills.find(missile.skill.sourceId);
            skill!=rule.damageFormulas->skills.end() && skill->second.auraTargetState>=0) {
            plan.addSkillState(missile.owner,target,missile.skill.sourceId,missile.skill.rank,5);return 3;
        }
        return 1;
    case 31:
        if(!target || !participants.find(missile.owner).id()) return 1;
        plan.addSourceHeal(missile.owner);return rule.killOnHit?3:2;
    case 36:
        if(target) return 0;
        if(child>=0) planNativeChild(plan,child,missile.position,{}).parameters[0]=missile.parameters[0];
        return 1;
    case 37:return target?2:0;
    case 38:case 45: {
        if(child<0) return 1;
        auto count=rule.hitParameters[0];
        if(rule.serverHitFunction==38) {
            count=nativeAdd(count,nativeMultiply(rule.hitParameters[1],nativeSubtract(missile.skill.rank,1)));
            if(count<=0) {const auto value=missileSkillFormula(plan,"calc4");if(!value) return 1;count=std::max(*value,1);}
        } else if(count<=0) return 1;
        if((count-1)/5>7) throw std::logic_error("Native charged burst exceeds the original direction table");
        for(const auto direction:missileChargedBurstDirections(count)) {
            auto &next=planNativeChild(plan,child,missile.position,direction);
            if(rule.serverHitFunction==38 || rule.hitParameters[1]) next.pathInitialization.kind=MissilePathInitialization::Kind::ChargedBurst;
        }
        return 1;
    }
    case 43: {
        if(!target || !rule.damageFormulas->skills.contains(missile.skill.sourceId)) return 1;
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto recipient=participants.find(target);if(!recipient.id()) return 1;
        std::optional<std::pair<int32_t,int32_t>> range;
        const auto evaluated=withMissileOwnerFormula(plan,[&](SkillFormulaValues &values)->RuleNumber {
            range=values.physicalRange(missile.skill.sourceId,missile.skill.rank,true);return range?RuleNumber{1}:RuleNumber{};
        });
        if(!evaluated || !range) return 1;
        const auto amount=nativeAdd(range->first,int32_t(limitedRandom(plan.damageRolls->random,uint32_t(nativeSubtract(range->second,range->first)))));
        const auto current=recipient.player?recipient.player->persistent.player.hp.raw():nativeChecked(recipient.monster->life);
        const auto maximum=recipient.player?recipient.player->totals.character.maxLifeFixed.raw():nativeChecked(recipient.monster->maximumLife);
        plan.operations.push_back({Advance::OperationKind::Resource,plan.resources.size()});
        plan.resources.push_back({Advance::ResourceWrite::Kind::Life,target,std::min(nativeAdd(current,amount),maximum),current!=maximum?rule.programOverlay:-1});
        return uint8_t(rule.killOnHit);
    }
    case 56: {
        auto range=rule.hitParameters[0];
        if(range<=0) {const auto value=missileSkillFormula(plan,"aurarangecalc");if(!value) return 1;range=std::max(*value,1);}
        areaImpact(plan,missile.position,range,true);
        if(child>=0) planNativeChild(plan,child,missile.position,{});
        return 1;
    }
    case 55: {
        const auto *player=ports_.players.findActor(target);
        if(!player) return 2;
        if(rule.hitParameters[0]<=0 || player->persistent.player.mana<=0) return 1;
        plan.addManaDrain(target,std::clamp(rule.hitParameters[0],1,100));return 2;
    }
    case 47: {
        const auto *unit=ports_.monsters.find(target);
        if(!unit || !rule.damageTypes) return 2;
        const auto dimensions=rule.damageTypes->dimensions.find(unit->rule.nativeClass);
        if(dimensions==rule.damageTypes->dimensions.end() || !dimensions->second.second) return 2;
        auto range=rule.hitParameters[0];
        if(range<=0) {const auto value=missileSkillFormula(plan,"aurarangecalc");if(!value) return 1;range=std::max(*value,1);}
        areaImpact(plan,missile.position,range,true);
        constexpr std::array<Vec,18> offsets{Vec{2,-2},Vec{-2,-2},Vec{0,2},Vec{0,5},Vec{-3,3},Vec{0,3},Vec{3,3},Vec{-1,2},Vec{1,1},Vec{-1,-1},Vec{2,-1},Vec{-4,-2},Vec{-3,-2},Vec{-1,-3},Vec{0,-4},Vec{1,-3},Vec{3,-3},Vec{4,-2}};
        if(child>=0) for(size_t index=0;index<offsets.size();index+=size_t(std::max(rule.hitParameters[1],1)))
            planNativeChild(plan,child,missile.position+offsets[index],{},range>0?range:0);
        return 1; // sub_6FD10200 returns one even for an empty target list.
    }
    case 48:
        if(child>=0) planNativeChild(plan,child,missile.position,missile.path.empty()?missile.turnTarget:missile.path.front()-missile.position);
        return 1;
    case 50:return target && missile.currentFrame()>rule.hitParameters[0]?2:0;
    case 51:
        if(child>=0) {
            planNativeChild(plan,child,missile.position,{});
            for(size_t slot=1;slot<3;++slot) if(rule.hitSubMissiles[slot]>=0)
                planNativeChild(plan,rule.hitSubMissiles[slot],missile.position,{});
        }
        return 1;
    case 53: {
        if(!target) return 1;
        const auto remaining=nativeSubtract(missile.parameters[0],nativeNarrow(missile.executionTick));
        const auto maximum=missileSkillFormula(plan,"edln");
        if(!maximum || remaining<10 || remaining>*maximum) return 1;
        const auto skill=rule.damageFormulas->skills.find(missile.skill.sourceId);
        if(skill==rule.damageFormulas->skills.end() || skill->second.auraTargetState<0) return 2;
        const auto *effects=ports_.effects.unitEffects(target);
        if(effects && effects->hasState(skill->second.auraTargetState,missile.executionTick)) return 2;
        plan.addSkillState(missile.owner,target,missile.skill.sourceId,missile.skill.rank,0);
        auto &state=plan.skillStates.back();state.duration=remaining;state.seedCommitted=true;state.procedure=effects::NativeSkillStateProcedure::Infection;
        const auto posted=std::make_shared<bool>(false);state.posted=posted;
        const auto name=skill->second.reference("srvmissilea");
        const auto definition=rule.damageFormulas->missileIds.find(name);
        if(definition!=rule.damageFormulas->missileIds.end() && definition->second>0) {
            const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
            const auto recipient=participants.find(target),owner=participants.find(missile.owner);
            const auto point=unitPosition(missile,target);
            if(point && recipient.id()) {
                auto &carrier=planNativeChild(plan,definition->second,*point,{},remaining);
                carrier.owner=target;carrier.parameters={owner.player?0:1,nativeNarrow(missile.owner.value)};
                const auto index=plan.children.size()-1;auto build=std::move(plan.childBuilders.at(index));
                plan.childBuilders[index]=[posted,build=std::move(build)](const Missile &parameters,Advance::ChildBuildContext &context)->std::optional<Missile> {
                    if(!*posted) return {};
                    return build(parameters,context);
                };
            }
        }
        return 2;
    }
    case 58: {
        if(child<0) return 1;
        const auto &area=ports_.areas.at(missile.area);
        uint64_t random=(plan.damageRolls->random&0xffffffff00000000ULL)|uint32_t(int32_t(std::floor(missile.position.x+area.definition.origin.x)));
        const auto extent=std::max(rule.hitParameters[0],1);const auto span=uint32_t(nativeAdd(nativeMultiply(2,extent),1));
        const auto x=nativeSubtract(int32_t(limitedRandom(random,span)),extent),y=nativeSubtract(int32_t(limitedRandom(random,span)),extent);
        plan.damageRolls->random=random;missile.random=random;
        planNativeChild(plan,child,missile.position,{float(x),float(y)});return 1;
    }
    case 59: {
        if(child<0) return 1;
        const auto nextRule=nativeChildRule(missile,child);
        for(const auto heading:poisonCloudDirections(2,std::max(rule.hitParameters[0],1))) {
            auto &next=planNativeChild(plan,child,missile.position,heading.direction);
            const auto parameter=nextRule.serverParameters[heading.secondary?1:0];
            next.velocity=heading.direction.unit()*(float(poisonCloudVelocity(parameter))*25.f/4096.f);
        }
        return 1;
    }
    default:return {};
    }
}

}
