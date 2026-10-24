#include "../system.hpp"
#include "../runtime.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "../native_birth.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace d2x::server::missiles {
DomainResult<> System::pd2SkillMissileEvent(const FrameEvent &event,TickContext tick) {
    const auto entry=runtime_->pd2DelayedSkills.find(event.handle);
    if(entry==runtime_->pd2DelayedSkills.end()) return {DomainStatus::Applied,std::monostate{}};
    auto &shot=entry->second;
    const auto finish=[&] {runtime_->pd2DelayedSkills.erase(event.handle);ports_.frameEvents.cancel(event.handle);return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,shot.owner);
    const auto *area=ports_.areas.find(shot.area);
    if(!caster || !caster->programs() || caster->area()!=shot.area || !area || area->generation!=shot.generation) return finish();
    // Current callback 0x304680 always uses Exploding Arrow (16), with
    // its current assigned level, rather than the impacting missile's level.
    constexpr int skill=16;const auto &rules=*caster->programs();
    const auto row=rules.formulas->skills.find(skill);if(row==rules.formulas->skills.end()) return finish();
    auto input=caster->formulaInput(ports_.effects,tick.tick);
    const auto found=input.referenceRanks->effective.find(skill);
    const auto rank=found==input.referenceRanks->effective.end()?1:found->second;
    constexpr std::array fields{"srvmissilea","srvmissileb","srvmissilec"};
    // The native callback calls the creator independently for A/B/C; only A
    // remains in the world. Mixed transient flags cannot share a publication.
    // Retain the cursor so backpressure does not create an earlier slot twice.
    while(shot.next<fields.size()) {
        const auto definition=nativeDefinition(row->second.reference(fields[shot.next]));
        if(!definition) {++shot.next;continue;}
        NativeSpawn birth{shot.owner,shot.area,shot.generation,tick.tick,shot.position,{},*definition,skill,rank};
        birth.copyMonsterAttackBonus=true;birth.transient=shot.next!=0;
        auto prepared=prepareNativeBirth(std::span{&birth,size_t{1}});
        if(!prepared) {
            if(prepared.status==DomainStatus::Unavailable) {++shot.next;continue;}
            return {prepared.status,{}};
        }
        const auto born=commitNativeBirth(std::move(*prepared.value));if(!born) return {born.status,{}};
        if(birth.transient) removeTransient(*born.value);
        ++shot.next;
    }
    return finish();
}
std::optional<uint8_t> System::pd2Hit(Advance &plan,EntityId target) const {
    auto &missile=plan.next;const auto &rule=missile.nativeDamage->rule;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(rule.serverHitFunction==63) {
        const auto owner=participants.find(missile.owner);if(!owner.id()) return 1;
        for(const auto definition:rule.hitSubMissiles) if(definition>0)
            planNativeChild(plan,definition,missile.position,{});
        if(!target) return 3;
        const auto victim=unitPosition(missile,target),origin=unitPosition(missile,missile.owner);
        if(!victim || !origin) return 1;
        const double dx=double(victim->x)-origin->x,dy=double(victim->y)-origin->y;
        const double length=std::sqrt(dx*dx+dy*dy);if(!length) return 1;
        if(rule.linkedSkill<0 || !rule.damageFormulas->skills.contains(rule.linkedSkill)) return 1;
        const auto formula=[&](std::string_view field) {
            return withMissileOwnerFormula(plan,[&](SkillFormulaValues &values) {
                return values.calculation(FormulaDomain::Skill,rule.linkedSkill,missile.skill.rank,field);
            });
        };
        const auto distance=formula("calc3"),count=formula("calc2"),delay=formula("calc4");
        if(!distance || !count || !delay) return 1;
        if(*count<=0) return 3;
        if(*count>512) throw std::logic_error("PD2 Exploding Arrow fan exceeds native capacity");
        const auto &skill=rule.damageFormulas->skills.at(rule.linkedSkill);
        const auto global=*victim+ports_.areas.at(missile.area).definition.origin;
        const double nx=dx/length,ny=dy/length;
        const double cx=global.x-nx*double(*distance),cy=global.y-ny*double(*distance);
        const double half=double(skill.number("param4"))*0.5;
        const double bx=cx-ny*half,by=cy+nx*half,ex=cx+ny*half,ey=cy-nx*half;
        auto wait=*delay;
        for(int index=0;index<*count;++index) {
            // 0x37F3E0 consumes three CRT draws: triangle barycentrics,
            // then a squared-distance stratum shared by each pair of rays.
            const double first=std::sqrt(double(pd2CrtRandom(ports_.pd2Random))/32767.0);
            const double second=double(pd2CrtRandom(ports_.pd2Random))/32767.0;
            const double third=double(pd2CrtRandom(ports_.pd2Random))/32767.0;
            const double denominator=double(*count/2);
            const double low=double(index/2)/denominator,high=double(index/2+1)/denominator;
            const double radius=std::sqrt(third*(high*high-low*low)+low*low);
            const double x=double(global.x)+radius*((1.0-first)*(bx-global.x)+first*second*(ex-global.x));
            const double y=double(global.y)+radius*((1.0-first)*(by-global.y)+first*second*(ey-global.y));
            wait=nativeAdd(wait,2);
            plan.operations.push_back({Advance::OperationKind::Pd2DelayedSkill,plan.pd2DelayedSkills.size()});
            plan.pd2DelayedSkills.push_back({missile.owner,missile.area,missile.generation,missile.executionTick+uint64_t(std::max(1,wait)),
                Vec{float(int(x)),float(int(y))}-ports_.areas.at(missile.area).definition.origin});
        }
        return 3;
    }
    if(rule.serverHitFunction==34) {
        const auto owner=participants.find(missile.owner);
        if(!owner.id() || rule.hitSubMissiles[0]<0) return 0;
        const auto &record=rule.damageFormulas->missiles.at(missile.definition);
        if(!target) {
            const auto frames=missile.currentFrame();
            if(missile.parameters[0]>=record.number("clthitpar3")) return 3;
            if(missile.nativeActivateFrame<frames || frames<2 || missile.nativeTotalFrames<=frames) return 0;
            const auto &area=ports_.areas.at(missile.area);
            const auto global=missile.position+area.definition.origin;
            const int x=int(std::floor(global.x)),y=int(std::floor(global.y));
            const auto room=ports_.areas.roomAtPosition(missile.area,missile.position,x,y);
            if(!room) return 1;
            const auto sample=ports_.spatial.nativeCollisionSample(0x806);
            const int size=missile.collision.size;
            if(!ports_.areas.nativeCollisionMask(*room,x,y,size+1,0x806,sample)) return 0;
            const auto origin=owner.position()+ports_.areas.at(unitArea(owner.id())).definition.origin;
            const int dx=x-int(std::floor(origin.x)),dy=y-int(std::floor(origin.y));
            const float length=float(std::sqrt(double(dx)*dx+double(dy)*dy));
            if(!length) return 0;
            const auto count=rule.hitParameters[0]?rule.hitParameters[0]:1;
            const auto step=rule.hitParameters[1]?rule.hitParameters[1]:1;
            const auto decay=rule.hitParameters[2]?rule.hitParameters[2]:1;
            if(count>512) throw std::logic_error("PD2 Blade Fury reflection exceeds native capacity");
            int offset=-((count-1)/2)*step;
            for(int index=0;index<count;++index,offset+=step) for(int axis=0;axis<2;++axis) {
                const float reflectedX=axis?float(dx)/length:-float(dx)/length;
                const float reflectedY=axis?-float(dy)/length:float(dy)/length;
                float angle=float(double(std::atan2(reflectedY,reflectedX))*10.185916357881302);
                if(angle<0) angle+=64.f;
                const auto direction=missileRingDirection((int(angle)+offset)&63);
                const float distance=direction.length();
                const int px=int(float(x)+direction.x/distance*float(size));
                const int py=int(float(y)+direction.y/distance*float(size));
                if(ports_.areas.nativeCollisionMask(*room,px,py,size,0x806,sample)) continue;
                auto &child=planNativeChild(plan,rule.hitSubMissiles[0],missile.position,direction);
                child.parameters[0]=nativeAdd(missile.parameters[0],1);
                auto remaining=std::max(0,nativeSubtract(frames,decay));
                if(!missile.parameters[0]) remaining=std::max(remaining,record.number("clthitpar1"));
                if(remaining<record.number("clthitpar2")) remaining=0;
                child.setCurrentFrame(remaining);break;
            }
            missile.setCurrentFrame(0);return 0;
        }
        if(missile.parameters[0]<record.number("clthitpar3")) {
            const auto count=rule.hitParameters[0]?rule.hitParameters[0]:1;
            const auto step=rule.hitParameters[1]?rule.hitParameters[1]:1;
            const auto decay=rule.hitParameters[2]?rule.hitParameters[2]:1;
            auto roll=pd2CrtRandom(ports_.pd2Random);
            if(int(roll%100)<std::clamp(rule.serverParameters[0],0,100)) {
                if(count>512) throw std::logic_error("PD2 Blade Fury fan exceeds native capacity");
                int offset=-((count-2)/2)*step;
                for(int index=0;index<count-1;++index,offset+=step,roll+=0x11) {
                    auto &child=planNativeChild(plan,rule.hitSubMissiles[0],missile.position,missileRingDirection(int((roll+uint32_t(offset))&63u)));
                    child.parameters[0]=nativeAdd(missile.parameters[0],1);
                    auto frames=std::max(0,nativeSubtract(missile.currentFrame(),decay));
                    if(frames<record.number("clthitpar2")) frames=0;
                    child.setCurrentFrame(frames);
                    child.enemy->rule.clientSend=true;
                }
            }
        }
        return 3;
    }
    if(rule.serverHitFunction==61) {
        if(!target || !participants.find(missile.owner).id()) return 0;
        const auto &record=rule.damageFormulas->missiles.at(missile.definition);
        effects::NativeSkillStateValues values;values.state=rule.hitParameters[0];
        values.stats={{{rule.hitParameters[2],record.number("clthitpar1")},
            {record.number("clthitpar2"),record.number("clthitpar3")}}};
        plan.addSkillState(missile.owner,target,rule.linkedSkill,missile.skill.rank,0);
        auto &state=plan.skillStates.back();state.procedure=effects::NativeSkillStateProcedure::Pd2Debuff;
        state.duration=rule.hitParameters[1];state.seedCommitted=true;state.values=std::move(values);
        return rule.killOnHit?3:2;
    }
    if(rule.serverHitFunction==30) {
        const auto owner=participants.find(missile.owner);
        if(!target || !owner.id() || !rule.damageFormulas->skills.contains(missile.skill.sourceId)) return 1;
        if(missile.parameters[0]<=1 || ports_.areas.at(unitArea(owner.id())).definition.town) return 3;
        auto radius=rule.hitParameters[0];
        if(radius<1) {const auto value=missileSkillFormula(plan,"aurarangecalc");if(!value) return 3;radius=*value;}
        const auto &area=ports_.areas.at(missile.area);
        const auto targets=ports_.effects.nativeAuraCandidateRecipients({missile.owner,unitArea(missile.owner),radius,0x8a783,false,
            missile.position+area.definition.origin,effects::NativeAuraTargetMode::TargetRange,missile.executionTick});
        std::vector<uint64_t> candidates;for(const auto id:targets) candidates.push_back(id.value);
        const EntityId selected{missileChainSuccessor(target.value,candidates)};
        if(selected) if(const auto point=unitPosition(missile,selected)) {
            auto &child=planNativeChild(plan,missile.definition,missile.position,*point-missile.position);
            child.parameters[0]=nativeSubtract(missile.parameters[0],1);
            // 0x3A583A adds Calc1 to the OLD missile, after constructing
            // the bounce. The child only receives the remaining count.
            if(const auto bonus=missileSkillFormula(plan,"calc1")) {
                missile.nativeDamage->data.damagePercent=nativeAdd(missile.nativeDamage->data.damagePercent,*bonus);
                plan.damageRolls->data.damagePercent=nativeAdd(plan.damageRolls->data.damagePercent,*bonus);
            }
        }
        return 3;
    }
    if(rule.serverHitFunction==50) {
        if(!target || !participants.find(missile.owner).id()) return 0;
        if(missile.currentFrame()<=rule.hitParameters[0]) return 0;
        if(rule.linkedSkill>=0) {
            plan.addSkillState(missile.owner,target,rule.linkedSkill,missile.skill.rank,0);
            plan.skillStates.back().procedure=effects::NativeSkillStateProcedure::Pd2Debuff;
        }
        return 2;
    }
    if(rule.serverHitFunction==65) {
        // Current core RVA 0x3A6C30: the outward Split Throw keeps its
        // ordinary hit while moving faster than 0x80 path velocity units.
        if(target && uint32_t(int32_t(std::lround(missile.velocity.length()*4096.f/25.f)))>0x80) return 3;
        const auto owner=participants.find(missile.owner);
        const auto point=owner.id()?unitPosition(missile,owner.id()):std::optional<Vec>{};
        if(!point || rule.hitSubMissiles[0]<0 || rule.linkedSkill<0) return 1;
        auto &child=planNativeChild(plan,rule.hitSubMissiles[0],missile.position,*point-missile.position);
        child.skill.sourceId=rule.linkedSkill;child.guidance=owner.id();child.suppressSourceEvents=true;
        child.enemy->rule.clientSend=true;
        return 1;
    }
    if(rule.serverHitFunction==1) {
        // Current core RVA 0x3A51D0 retains GetDamageValue's area roll,
        // and allows a CollideKill==0 contact to continue with direct damage.
        if(!participants.find(missile.owner).id()) return 1;
        auto radius=rule.hitParameters[0];
        if(radius<=0) {const auto value=missileSkillFormula(plan,"calc1");if(!value) return 1;radius=std::max(*value,1);}
        areaImpact(plan,missile.position,radius);
        return target && !rule.killOnHit?6:1;
    }
    if(rule.serverHitFunction!=62) return {};
    // RVA 0x3A62E0: Fire/Ice Bolt splash rolls the linked skill on the
    // actual owner's seed, at missile rank + HitPar2, once for the area.
    const auto owner=participants.find(missile.owner);if(!owner.id() || rule.linkedSkill<0) return 1;
    const auto skill=rule.linkedSkill,rank=nativeAdd(missile.skill.rank,rule.hitParameters[1]);
    if(!rule.damageFormulas->skills.contains(skill)) return 1;
    MissileHitDamage damage;
    const auto sampled=withMissileOwnerRandom(plan,[&](SkillFormulaValues &values,uint64_t &random)->RuleNumber {
        damage=rollNativeSkillDamage(values,skill,rank,random);return 1;
    });
    if(!sampled) return 1;
    const auto &area=ports_.areas.at(missile.area);
    const auto targets=ports_.effects.nativeAuraCandidateRecipients({missile.owner,unitArea(missile.owner),rule.hitParameters[0],0x8583,false,
        missile.position+area.definition.origin,effects::NativeAuraTargetMode::TargetRange,missile.executionTick});
    auto rolled=std::make_shared<combat::MissileAreaRoll>();rolled->value=damage;
    const auto &definition=rule.damageFormulas->missiles.at(missile.definition);
    for(const auto id:targets) {
        auto hit=impact(plan,{id});hit.area=unitArea(id);
        hit.nativeAura=hit.synchronous=hit.retainDeadSource=hit.nativeDamageFlagsOnly=hit.unblockable=true;
        hit.areaDamage=combat::MissileAreaDamage{};hit.areaDamage->rolled=rolled;
        hit.areaDamage->hitFlags=uint32_t(definition.number("hitflags"));
        hit.areaDamage->resultFlags=uint16_t(definition.number("resultflags"))|1;
        hit.weapon.reset();hit.returnFire=hit.monsterToHit=false;hit.nativeNextHit.reset();hit.nextDelay=0;
        hit.occurrence=(uint64_t(missile.ageFrames)<<32)|(uint64_t{1}<<31)|plan.impacts.size();
        plan.addImpact(std::move(hit));
    }
    return 1;
}
}
