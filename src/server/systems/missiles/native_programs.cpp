#include "system.hpp"
#include "runtime.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/missiles/sequence.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace d2x::server::missiles {
void System::initializeGuidance(Missile &missile,EntityId target,Vec point) const {
    if(!missile.nativeDamage || missile.nativeDamage->rule.serverDoFunction!=7) return;
    missile.guidance=target;missile.parameters[0]=target?1:2;
    const auto dx=uint16_t(int32_t(std::floor(point.x))-int32_t(std::floor(missile.position.x)));
    const auto dy=uint16_t(int32_t(std::floor(point.y))-int32_t(std::floor(missile.position.y)));
    // Retail Do10 sign-extends both WORD deltas before SHL/ADD. A negative
    // X consequently borrows from the packed Y word; preserve that bit pattern.
    missile.parameters[1]=nativeAdd(int16_t(dx),nativeShiftLeft(int16_t(dy),16));
    if(!target) {
        const auto velocity=int32_t(std::lround(missile.velocity.length()*4096.f/25.f));
        missile.setCurrentFrame(velocity>0?nativeMultiply(std::max(missileDistance(missile.position,point),1),4096)/velocity:0);
    }
}
void System::initializePath(Missile &missile) const {
    if(missile.pathInitialization.kind==MissilePathInitialization::Kind::None) return;
    const auto &area=ports_.areas.at(missile.area);
    const auto target=missile.position+missile.turnTarget;
    const auto low=uint32_t(int32_t(std::floor(target.x+area.definition.origin.x))+missile.pathInitialization.index);
    missile.random=initialRandom(low);
    int distance{};
    const bool wall=missile.pathInitialization.kind==MissilePathInitialization::Kind::DiabWall;
    if(missile.pathInitialization.kind==MissilePathInitialization::Kind::ChargedSkill || wall) {
        distance=wall?missile.nativeTotalFrames:int(uint8_t(missile.nativeTotalFrames));
        if(distance>=78) {distance=77;missile.nativeTotalFrames=77;missile.setCurrentFrame(77);}
    } else distance=std::min(missile.nativeTotalFrames,255);
    missile.expires=missile.created+uint64_t(missile.lifetimeFrames);
    if(wall && rollRandom(missile.random)%100<20) return;
    const auto path=chargedBoltPath(missile.position,target,distance,missile.random);
    missile.path.assign(path.begin(),path.end());
}
RuleNumber System::withMissileOwnerFormula(Advance &plan,const std::function<RuleNumber(SkillFormulaValues &)> &evaluate) const {
    return withMissileOwnerRandom(plan,[&](SkillFormulaValues &values,uint64_t &)->RuleNumber {return evaluate(values);});
}
RuleNumber System::withMissileOwnerRandom(Advance &plan,const std::function<RuleNumber(SkillFormulaValues &,uint64_t &)> &evaluate) const {
    const auto &missile=plan.next;const auto &rule=missile.nativeDamage->rule;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto owner=participants.find(missile.owner);
    if(!rule.damageFormulas) return {};
    if(!owner.id()) {
        const std::map<int,int> ranks;const StatSnapshot stats;
        SkillFormulaInput input{ranks,ranks};input.statRules=rule.damageStats.get();input.stats=&stats;
        auto random=uint64_t{};SkillFormulaValues formulas(*rule.damageFormulas,input);return evaluate(formulas,random);
    }
    auto expected=owner.player?owner.player->combatRandom:owner.monster->combatRandom;
    for(size_t index=plan.nextOperation;index<plan.operations.size();++index) {
        const auto operation=plan.operations[index];
        if(operation.kind==Advance::OperationKind::OwnerSeed && plan.formulaSeeds.at(operation.index).owner==owner.id())
            expected=plan.formulaSeeds.at(operation.index).next;
    }
    auto random=expected;
    const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
        if(low>=high) return low;
        return nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
    };
    const auto sampled=owner.monster?ports_.effects.unitStatValues(owner.id(),missile.executionTick):std::optional<StatSnapshot>{};
    if(owner.monster && !sampled) throw std::logic_error("Missing native missile formula owner stats");
    auto input=owner.player?skills::formulaInput(*owner.player,draw):skills::formulaInput(*owner.monster,*sampled,draw);
    SkillFormulaValues formulas(*rule.damageFormulas,input);
    const auto value=evaluate(formulas,random);
    if(random!=expected) {
        plan.operations.push_back({Advance::OperationKind::OwnerSeed,plan.formulaSeeds.size()});
        plan.formulaSeeds.push_back({owner.id(),expected,random});
    }
    return value;
}
std::optional<int32_t> System::missileSkillFormula(Advance &plan,std::string_view field) const {
    const auto &missile=plan.next;const auto &catalog=*missile.nativeDamage->rule.damageFormulas;
    const bool missileFormula=field=="srvcalc1" || field=="shitcalc1";
    if(!missileFormula && !catalog.skills.contains(missile.skill.sourceId)) return {};
    return withMissileOwnerFormula(plan,[&](SkillFormulaValues &formulas) {
        if(field=="edln") return formulas.parameter(FormulaDomain::Skill,missile.skill.sourceId,missile.skill.rank,field);
        return formulas.calculation(missileFormula?FormulaDomain::Missile:FormulaDomain::Skill,
            missileFormula?missile.definition:missile.skill.sourceId,missile.skill.rank,field);
    });
}
std::optional<int32_t> System::missileSelfFormula(Advance &plan,std::string_view field,int skill,int rank) const {
    const auto &missile=plan.next;const auto &rule=missile.nativeDamage->rule;
    StatList root;root.ownerKind=StatOwnerKind::Missile;root.hasOwner=root.hasUnit=root.unitRoot=true;
    root.source={StatSourceKind::Base,missile.id,missile.definition,0};
    for(const auto &write:missileDamageStatWrites(plan.damageRolls->data,missile.skill.rank,{},*rule.damageStats))
        root.base.insert_or_assign(write.key,write.value);
    root.base.insert_or_assign({rule.damageStats->names.at("hitpoints"),0},missile.nativeResources[0]);
    root.base.insert_or_assign({rule.damageStats->names.at("mana"),0},missile.nativeResources[1]);
    const StatSnapshot stats{root.base};const std::map<int,int> ranks;
    const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
        if(low>=high) return low;
        return nativeAdd(low,int32_t(limitedRandom(plan.damageRolls->random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
    };
    SkillFormulaInput input{ranks,ranks};input.actor=&root;input.stats=&stats;input.statRules=rule.damageStats.get();input.random=draw;
    SkillFormulaValues values(*rule.damageFormulas,input);
    return values.calculation(FormulaDomain::Skill,skill<0?missile.skill.sourceId:skill,rank<0?missile.skill.rank:rank,field);
}
Missile &System::planNativeChild(Advance &plan,int definition,Vec position,Vec direction,int frames,int loops) const {
    const auto rule=nativeChildRule(plan.next,definition);
    auto child=plan.next;child.id={};child.definition=definition;child.position=position;
    child.ageFrames=0;child.parameters={};child.lastHit={};child.guidance={};child.guidanceSearched=false;
    child.initialDamagePercent=0;
    child.pathInitialization={};
    child.movedCell=false;child.motion.reset();child.pathRevision=0;
    child.hitOnCreation.reset();
    child.path.clear();child.weaponContacts.clear();child.weapon.reset();child.impact.reset();
    child.nativeProgram=true;child.nativeDamage.reset();child.damageRange.reset();
    child.lifetimeFrames=frames?frames:rule.frames;
    const auto &source=rule.damageFormulas->missiles.at(definition);
    if(!frames && source.number("subloop")) child.lifetimeFrames=nativeAdd(child.lifetimeFrames,
        nativeMultiply(loops,nativeSubtract(source.number("substop"),source.number("substart"))));
    child.initializeFrames(child.lifetimeFrames,rule.activate);
    child.velocity=(direction.length()?direction:Vec{1,1}).unit()*rule.speed;child.turnTarget=direction;
    child.pathTarget=position+(direction.length()?direction:Vec{1,1});
    child.collision=rule.collision;child.acceleration=rule.acceleration;child.maximumVelocity=rule.maximumVelocity;
    child.remainingHits=0;child.pierces=0;child.damage=0;child.skill.missileId=definition;
    child.enemy=EnemyProjectile{rule,{},plan.next.enemy?plan.next.enemy->states:MonsterHitStates{},0,0,{}};
    return planMonsterChild(plan,std::move(child));
}

void System::planCollisionChild(Advance &plan,int radius,int period,int definition,uint16_t mask) const {
    auto &missile=plan.next;
    if(!period || missile.elapsedFrames()%period || definition<0 ||
        !combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(missile.owner).id()) return;
    const auto &origin=ports_.areas.at(missile.area).definition.origin;
    const auto x=int32_t(std::floor(missile.position.x+origin.x)),y=int32_t(std::floor(missile.position.y+origin.y));
    auto random=plan.damageRolls->random;
    const auto offset=blizzardOffset(uint32_t(x),missile.elapsedFrames(),radius,false,random);
    missile.random=random;plan.damageRolls->random=random;
    const int atX=nativeAdd(x,int(offset.x)),atY=nativeAdd(y,int(offset.y));
    const auto room=ports_.areas.roomAtPosition(missile.area,missile.position,atX,atY);if(!room) return;
    const auto &area=ports_.areas.at(room->area);
    const Vec point{float(atX)-area.definition.origin.x,float(atY)-area.definition.origin.y};
    if(area.definition.collision.movementMask(int(point.x),int(point.y))&mask) return;
    auto &child=planNativeChild(plan,definition,point,{});
    child.area=room->area;child.generation=area.generation;
}
std::unique_ptr<System::Advance> System::advanceNative(const Missile &original) const {
    if(!original.nativeDamage) return {};
    auto function=original.nativeDamage->rule.serverDoFunction;
    constexpr std::array commonHits{0,1,2,3,4,5,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59};
    const bool common=function==1 && std::find(commonHits.begin(),commonHits.end(),original.nativeDamage->rule.serverHitFunction)!=commonHits.end();
    const bool dedicated=common || function==0 || function==4 || function>=38 || function==2 || function==3 || function==5 || function==6 || function==7 || function==8 || function==9 || function==10 || function==11 || function==12 || function==13 || function==14 || function==15 || function==16 || function==17 || function==19 || function==20 || function==21 || function==22 || function==23 || function==24 || function==25 || function==26 || function==27 || function==28 || function==29 || function==30 || function==31 || function==32 || function==33 || function==34 || function==35 || function==36 || function==37;
    if(!original.nativeProgram && !dedicated && function!=18) return {};
    if(function!=0 && function!=1 && function!=3 && function!=5 && function!=6 && function!=18 && !dedicated)
        throw std::logic_error("Native missile requires its dedicated server program");
    auto result=std::make_unique<Advance>(Advance{original,{},{},false});auto &plan=*result;auto &missile=plan.next;
    plan.damageRolls=damageRolls(original);const auto &rule=missile.nativeDamage->rule;
    const auto countdown=missile.currentFrame(),elapsed=missile.elapsedFrames();
    const auto &area=ports_.areas.at(missile.area);
    if(original.rules && original.rules->pd2 && function>=38 && function<=40) {
        if(!preparePd2Program(plan)) return result;
        function=1; // The PD2 callbacks finish through HandleMissileCollision.
    }
    if(function==0 || function==4 || function>=38) return result; // Null native Do slots leave the missile untouched.
    if(function==18) {
        if(countdown==1) plan.operations.push_back({Advance::OperationKind::TowerSound,uint32_t(missile.parameters[0])});
        if(countdown==nativeSubtract(rule.frames,rule.serverParameters[0])) {
            plan.operations.push_back({Advance::OperationKind::TowerOpen,uint32_t(missile.parameters[0])});
            missile.parameters[1]=1;
        }
        if(missile.parameters[1] && countdown%std::max(nativeMultiply(4,rule.serverParameters[1]),1)==0) {
            const auto radius=rule.serverParameters[2],width=nativeAdd(nativeMultiply(2,radius),1);
            const auto global=missile.position+area.definition.origin;
            const auto column=nativeAdd(int32_t(std::floor(global.x)),nativeSubtract(int32_t(limitedRandom(plan.damageRolls->random,uint32_t(width))),radius));
            const auto row=nativeAdd(int32_t(std::floor(global.y)),nativeSubtract(int32_t(limitedRandom(plan.damageRolls->random,uint32_t(width))),radius));
            plan.operations.push_back({Advance::OperationKind::TowerGold,plan.towerGold.size()});
            plan.towerGold.push_back({missile.area,missile.position,Vec{float(column),float(row)}});
        }
    }
    const auto markField=[&](int size) {
        plan.operations.push_back({Advance::OperationKind::MissileField,plan.missileFields.size()});
        plan.missileFields.push_back({missile.area,missile.position,size});
    };
    if(function==3 && !missile.velocity.length()) markField(1);
    if(function==5) {
        markField(missile.collision.size);
        const auto &source=rule.damageFormulas->missiles.at(missile.definition);
        missile.nativeSequenceFrame=firewallMissileSequence(missile.nativeSequenceFrame,countdown,
            source.number("substart"),source.number("substop"),plan.damageRolls->random);
        missile.random=plan.damageRolls->random;
    }
    if(function==36 && !missile.parameters[0] && countdown<=100) {
        missile.parameters[0]=1;plan.operations.push_back({Advance::OperationKind::BaalFx,0});
    }
    const auto skill=rule.damageFormulas->skills.find(missile.skill.sourceId);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto owner=participants.find(missile.owner);
    if(function==13) {
        if(!owner.id() || !missile.parameters[1]) {plan.finished=true;return result;}
        if(missile.movedCell) {
            if(skill==rule.damageFormulas->skills.end()) {plan.finished=true;return result;}
            plan.operations.push_back({Advance::OperationKind::BoneSegment,plan.boneSegments.size()});
            plan.boneSegments.push_back({EntityId{uint32_t(missile.parameters[0])},missile.area,missile.position});
        }
    }
    bool ownerMovedCell{};
    const auto follow=[&](EntityId id,Vec point) {
        const auto destination=unitArea(id);
        const auto &origin=ports_.areas.at(missile.area).definition.origin;
        const auto &targetOrigin=ports_.areas.at(destination).definition.origin;
        ownerMovedCell=std::floor(missile.position.x+origin.x)!=std::floor(point.x+targetOrigin.x) ||
            std::floor(missile.position.y+origin.y)!=std::floor(point.y+targetOrigin.y);
        missile.area=destination;missile.position={std::floor(point.x)+.5f,std::floor(point.y)+.5f};
        missile.generation=ports_.areas.at(destination).generation;missile.motion.reset();
    };
    if(function==37) for(size_t slot=0;slot<rule.subMissiles.size();++slot)
        if(rule.subMissiles[slot]>0 && elapsed==rule.serverParameters[slot])
            planNativeChild(plan,rule.subMissiles[slot],missile.position,{});
    if(function==19 && countdown>0 && countdown<25) {
        const auto id=rule.damageFormulas->skillIds.find("redemption");
        if(id==rule.damageFormulas->skillIds.end()) throw std::logic_error("Missing native Redemption callback");
        const auto &redemption=rule.damageFormulas->skills.at(id->second);
        const auto point=missile.position+area.definition.origin;
        const auto targets=ports_.effects.nativeAuraCandidateRecipients({missile.id,missile.area,redemption.number("param1"),0x3002,false,
            point,effects::NativeAuraTargetMode::TargetRange,missile.executionTick,point});
        for(const auto corpse:targets) {
            plan.operations.push_back({Advance::OperationKind::Redemption,plan.redemptions.size()});
            plan.redemptions.push_back({corpse,id->second,countdown==1});
        }
    }
    if(function==14 && elapsed%std::max(rule.serverParameters[0],1)==0) {
        if(missile.skill.sourceId<=0 || missile.skill.rank<=0) {plan.finished=true;return result;}
        const bool pd2BattleCry=missile.rules && missile.rules->pd2 && rule.serverParameters[1]==65;
        if(rule.serverParameters[1]!=30 && !pd2BattleCry) throw std::logic_error("Unported skill Do callback from Grim Ward controller");
        if(skill!=rule.damageFormulas->skills.end()) {
            const auto range=missileSelfFormula(plan,"aurarangecalc"),duration=missileSelfFormula(plan,"auralencalc");
            if(!range || !duration) throw std::logic_error("Missing Grim Ward missile formula context");
            const auto difficulty=owner.player?int(owner.player->persistent.difficulty):owner.monster?owner.monster->rule.difficulty:0;
            const auto divisor=rule.aiCurseDivisor.at(size_t(difficulty));const auto frames=divisor?nativeDivide(*duration,divisor):*duration;
            const auto targets=ports_.effects.nativeAuraCandidateRecipients({missile.owner,missile.area,*range,
                uint32_t(skill->second.number("aurafilter")),false,missile.pathTarget+area.definition.origin,effects::NativeAuraTargetMode::TargetRange,
                missile.executionTick,missile.position+area.definition.origin});
            for(const auto id:targets) if(const auto *unit=ports_.monsters.find(id);unit && unit->rule.states && unit->rule.states->contains(skill->second.auraTargetState)) {
                if(pd2BattleCry) {
                    plan.addSkillState(missile.owner,id,missile.skill.sourceId,missile.skill.rank,0);
                    auto &state=plan.skillStates.back();state.procedure=effects::NativeSkillStateProcedure::BattleCry;
                    state.duration=*duration;state.seedCommitted=true;
                } else {
                    plan.operations.push_back({Advance::OperationKind::Fear,plan.fears.size()});
                    plan.fears.push_back({id,missile.id,unit->rule.states->at(skill->second.auraTargetState),missile.skill.sourceId,missile.skill.rank,frames,false,{}});
                }
            }
        }
    }
    if(function==34) {
        size_t count{};
        while(count<rule.subMissiles.size() && rule.subMissiles[count]>=0 && rule.serverParameters[count+1]>0) ++count;
        if(!count) {plan.finished=true;return result;}
        if(countdown>=rule.serverParameters[0]) {
            uint64_t random=(plan.damageRolls->random&0xffffffff00000000ULL)|uint32_t(int32_t(std::floor(missile.position.x+area.definition.origin.x)));
            const auto index=limitedRandom(random,uint32_t(count));plan.damageRolls->random=random;missile.random=random;
            if(countdown%rule.serverParameters[index+1]==0)
                planNativeChild(plan,rule.subMissiles[index],missile.position,{}).hitOnCreation=EntityId{};
        }
        if(!missile.velocity.length()) markField(1);
    }
    if((function==6 || function==31) && (!owner.id() || rule.subMissiles[0]<0)) {plan.finished=true;return result;}
    if(function==20 || function==21) {
        if(function==21 && rule.subMissiles[0]>0 && missile.movedCell) planNativeChild(plan,rule.subMissiles[0],missile.position,{});
        if(!owner.id() || !owner.aliveIn(unitArea(owner.id()))) {plan.finished=true;return result;}
        follow(owner.id(),owner.position());
        missile.setCurrentFrame(10);
    }
    if(function==29 || function==33) {
        if(skill==rule.damageFormulas->skills.end()) {plan.finished=true;return result;}
        if(owner.id() && owner.aliveIn(unitArea(owner.id())) && elapsed==rule.serverParameters[0]) {
            const auto values=ports_.effects.unitStatValues(owner.id(),missile.executionTick);
            if(!values) throw std::logic_error("Missing recycler resource view");
            const auto current=values->get({rule.damageStats->names.at(function==29?"hitpoints":"mana"),0})>>8;
            const auto maximum=values->get({rule.damageStats->names.at(function==29?"maxhp":"maxmana"),0})>>8;
            if(current<maximum) {
                const auto percentage=missileSkillFormula(plan,"calc1");if(!percentage) throw std::logic_error("Missing recycler restoration formula");
                const auto next=nativeMultiply(std::min(nativeAdd(current,nativePercentage(maximum,*percentage,100)),maximum),256);
                plan.operations.push_back({Advance::OperationKind::Resource,plan.resources.size()});
                plan.resources.push_back({function==29?Advance::ResourceWrite::Kind::Life:Advance::ResourceWrite::Kind::Mana,owner.id(),next,rule.programOverlay});
            }
        }
    }
    if(function==15) {
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        if(rule.subMissiles[0]<0 || !participants.find(missile.owner).id()) {plan.finished=true;return result;}
        if(elapsed%std::max(rule.serverParameters[0],1)<=0) {
            const auto index=std::abs(missile.parameters[0]%64);
            const auto direction=missileRingDirection(index);
            missile.parameters[0]=nativeAdd(index,rule.serverParameters[1])%64;
            planNativeChild(plan,rule.subMissiles[0],missile.position,direction);
        }
    } else if(function==16 && elapsed<rule.serverParameters[0] && elapsed%std::max(rule.serverParameters[1],1)==0) {
        const auto x=nativeSubtract(missile.parameters[0],missile.parameters[1])/2,y=nativeAdd(missile.parameters[0],missile.parameters[1])/2;
        missile.parameters={x,y};missile.turnTarget={float(x),float(y)};missile.pathTarget=missile.position+missile.turnTarget;++missile.pathRevision;
        missile.velocity=missile.turnTarget.unit()*missile.velocity.length();
    }
    if(function==30) {
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto infected=participants.find(missile.owner),infector=participants.find(EntityId{uint32_t(missile.parameters[1])});
        const bool validType=infector.player?missile.parameters[0]==0:infector.monster && missile.parameters[0]==1;
        if(!infected.id() || !infector.id() || !validType || !infected.aliveIn(unitArea(infected.id())) || countdown<0 ||
            rule.subMissiles[0]<0 || skill==rule.damageFormulas->skills.end()) {plan.finished=true;return result;}
        follow(infected.id(),infected.position());
        if(elapsed%std::max(rule.serverParameters[0],1)==0) {
            const auto expected=infector.player?infector.player->combatRandom:infector.monster->combatRandom;auto random=expected;
            const auto span=uint32_t(nativeAdd(nativeMultiply(2,rule.serverParameters[1]),1));
            const auto x=nativeSubtract(int32_t(limitedRandom(random,span)),rule.serverParameters[1]);
            const auto y=nativeSubtract(int32_t(limitedRandom(random,span)),rule.serverParameters[1]);
            if(random!=expected) {plan.operations.push_back({Advance::OperationKind::OwnerSeed,plan.formulaSeeds.size()});plan.formulaSeeds.push_back({infector.id(),expected,random});}
            auto &contagion=planNativeChild(plan,rule.subMissiles[0],missile.position,{float(x),float(y)});
            contagion.owner=infector.id();contagion.emitter=infector.id();contagion.emitterType=infector.player?0:1;
            contagion.player=infector.player?infector.player->player:infector.monster->owner.value_or(PlayerId{});
            contagion.playerActor=infector.player?infector.id():EntityId{};
            if(skill->second.auraTargetState>0) if(const auto *states=ports_.effects.unitEffects(infected.id()))
                for(const auto &entry:states->entries()) if(entry.spec.state.id==skill->second.auraTargetState && entry.nativeExpireFrame) {
                    contagion.parameters[0]=*entry.nativeExpireFrame;break;
                }
        }
    }
    if(function==8 || function==10 || function==25) {
        const auto divided=missile.skill.rank/std::max(rule.serverParameters[2],1);
        const auto range=function==8?std::optional<int32_t>{nativeAdd(std::max(divided,2),rule.serverParameters[0])}:missileSkillFormula(plan,"calc1");
        const auto period=function==8?std::optional<int32_t>{std::max(nativeSubtract(rule.serverParameters[1],divided),3)}:missileSkillFormula(plan,"calc2");
        if(!range || !period) {plan.finished=true;return result;}
        planCollisionChild(plan,*range,*period,rule.subMissiles[0],uint16_t(function==25?0x45:5));
    } else if(function==17) {
        const auto rawRange=rule.damageFormulas->missiles.at(missile.definition).number("range");
        if(rule.subMissiles[0]>0 && countdown<nativeSubtract(rawRange,rule.serverParameters[0]) && countdown>rule.serverParameters[0])
            planCollisionChild(plan,rule.serverParameters[2],rule.serverParameters[1],rule.subMissiles[0],5);
        if(rule.serverParameters[3]>=0 && !missile.parameters[0] && countdown<=nativeAdd(rule.serverParameters[0],rule.serverParameters[4])) {
            missile.parameters[0]=1;plan.operations.push_back({Advance::OperationKind::CairnPortal,0});
        }
    } else if(function==27) {
        if(skill==rule.damageFormulas->skills.end()) {plan.finished=true;return result;}
        auto period=rule.serverParameters[0];
        if(period<=0) {const auto value=missileSkillFormula(plan,"calc4");if(!value) throw std::logic_error("Missing Tornado period");period=std::max(*value,1);}
        if(elapsed%period==0) {
            auto range=rule.serverParameters[1];
            if(range<=0) {const auto value=missileSkillFormula(plan,"aurarangecalc");if(!value) throw std::logic_error("Missing Tornado radius");range=std::max(*value,1);}
            areaImpact(plan,missile.position,range,true,uint32_t(skill->second.number("aurafilter")));
        }
    } else if(function==28 && rule.subMissiles[0]>=0) {
        if(skill==rule.damageFormulas->skills.end()) {plan.finished=true;return result;}
        auto range=rule.serverParameters[1],period=rule.serverParameters[0];
        if(range<=0) {const auto value=missileSkillFormula(plan,"aurarangecalc");if(!value) throw std::logic_error("Missing Volcano radius");range=std::max(*value,1);}
        if(period<=0) {const auto value=missileSkillFormula(plan,"calc4");if(!value) throw std::logic_error("Missing Volcano period");period=std::max(*value,1);}
        if(elapsed>rule.serverParameters[2] && elapsed<rule.serverParameters[3] && elapsed%period==0) {
            uint64_t random=(missile.random&0xffffffff00000000ULL)|uint32_t(missile.parameters[0]);
            const auto span=uint32_t(nativeAdd(nativeMultiply(2,range),1));
            const auto x=nativeSubtract(int32_t(limitedRandom(random,span)),range),y=nativeSubtract(int32_t(limitedRandom(random,span)),range);
            missile.parameters[0]=int32_t(uint32_t(random));missile.random=random;plan.damageRolls->random=random;
            planNativeChild(plan,rule.subMissiles[0],missile.position,{float(x),float(y)});
        }
    }
    if(function==7 || function==32) {
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto owner=participants.find(missile.owner);
        if(!owner.id() || !owner.aliveIn(unitArea(owner.id())) || ports_.areas.at(missile.area).definition.town) {plan.finished=true;return result;}
        if(missile.parameters[0]&1) {
            const auto point=unitPosition(missile,missile.guidance);
            const auto period=rule.serverParameters[0]>0?rule.serverParameters[0]:5;
            if(point && participants.canHarm(missile.owner,missile.guidance))
                if(const auto direction=missileGuidedDirection(missile.position,*point,countdown,period)) {
                    missile.pathTarget=*point;++missile.pathRevision;missile.turnTarget=*point-missile.position;
                    missile.velocity=*direction*missile.velocity.length();
                }
        }
    } else if(function==11) {
        auto target=unitPosition(missile,missile.guidance);
        // Raw Do101 does not install a missile target. SrvDo11 falls back
        // to its owner's current path target on each callback, so a later
        // retarget changes the projectile's next homing step as well.
        if(!target && owner.monster) target=unitPosition(missile,owner.monster->movementTarget);
        const auto period=rule.serverParameters[0]>0?rule.serverParameters[0]:5;
        if(target && countdown%period==0 && missileDistance(missile.position,*target)<=rule.serverParameters[1]) {
            const auto step=float(std::max(rule.serverParameters[2],1));
            const auto delta=*target-missile.position;
            const Vec direction{delta.x<0?-step:delta.x>0?step:0,delta.y<0?-step:delta.y>0?step:0};
            missile.velocity=direction.unit()*missile.velocity.length();missile.turnTarget=direction;missile.pathTarget=missile.position+direction;++missile.pathRevision;
        }
    } else if(function==35 && elapsed%std::max(rule.serverParameters[0],1)==0) {
        // SEED_InitLowSeed preserves the native high word (unlike InitSeed).
        uint64_t random=(missile.random&0xffffffff00000000ULL)|uint32_t(missile.parameters[0]);
        const auto packed=uint32_t(missile.parameters[1]);
        const auto x=int32_t(int16_t(packed)),y=int32_t(int16_t(packed>>16));
        auto xPart=x,yPart=y;if(rollRandom(random)&1) xPart=-xPart;else yPart=-yPart;
        auto nextX=nativeAdd(yPart,nativeMultiply(4,x))/4,nextY=nativeAdd(xPart,nativeMultiply(4,y))/4;
        if(!nextX) nextX=1;
        if(!nextY) nextY=1;
        missile.parameters={int32_t(uint32_t(random)),int32_t(uint32_t(uint16_t(nextX))|(uint32_t(nextY)<<16))};
        missile.random=random;plan.damageRolls->random=random;
        missile.turnTarget={float(nextX),float(nextY)};missile.velocity=missile.turnTarget.unit()*missile.velocity.length();missile.pathTarget=missile.position+missile.turnTarget;++missile.pathRevision;
    }
    if(function==26 && rule.subMissiles[0]>=0 && elapsed%std::max(rule.serverParameters[0],1)==0)
        planNativeChild(plan,rule.subMissiles[0],missile.position,{});
    if(const auto changed=advanceMissileVelocity(missile.velocity.length(),missile.acceleration,missile.maximumVelocity,missile.ageFrames+1)) {
        missile.velocity=missile.velocity.unit()*changed->speed;missile.acceleration=changed->acceleration;
    }
    const bool changedBefore=missile.movedCell;
    const auto moved=moveMissile(missile);auto next=moved.position;
    if(function==20 || function==21 || function==30) missile.movedCell=ownerMovedCell;
    const bool wall=moved.blocked;
    // Path's changed-cell flag is sampled by the native Do program before
    // HandleMissileCollision decrements CurrentFrame afterwards.
    if(function==22 && elapsed<2) {
        const auto x=int32_t(std::floor(missile.pathTarget.x))-int32_t(std::floor(missile.position.x));
        const auto y=int32_t(std::floor(missile.pathTarget.y))-int32_t(std::floor(missile.position.y));
        missile.parameters={-y,x};
    }
    if(changedBefore && rule.subMissiles[0]>=0) {
        if(function==2) {
            const auto loops=missileSkillFormula(plan,"srvcalc1");
            if(!loops) throw std::logic_error("Missing poison trail loop formula");
            // The native helper uses the calculation only to set flag 8;
            // the actual loop count is then overwritten with 2*(rank-1).
            planNativeChild(plan,rule.subMissiles[0],missile.position,{},0,*loops>0?nativeMultiply(2,nativeSubtract(missile.skill.rank,1)):0);
        } else if(function==6 || function==9 || function==12 || function==23 || function==24 || function==32)
            planNativeChild(plan,rule.subMissiles[0],missile.position,{},0,function==23 || function==24?std::max(rule.serverParameters[0],0):0);
        else if(function==22 || function==31) {
            const Vec direction{float(missile.parameters[0]),float(missile.parameters[1])};
            planNativeChild(plan,rule.subMissiles[0],missile.position,direction,0,function==22?rule.serverParameters[0]:0);
            planNativeChild(plan,rule.subMissiles[0],missile.position,direction*-1.f,0,function==22?rule.serverParameters[0]:0);
        }
    }
    if(!wall && !moved.pathFinished) ++missile.ageFrames;
    const bool expired=missile.ageFrames>=missile.lifetimeFrames || moved.pathFinished;
    plan.collisionFlight=Advance::CollisionFlight{missile.position,next,{},0,bool(wall),true,false};
    plan.collisionFlight->nativeExpired=expired;
    // HandleMissileCollision runs after the Do callback's synchronous child,
    // state and world publications. Select against those actual live values,
    // then retain the selected contact if a later operation hits backpressure.
    plan.operations.push_back({Advance::OperationKind::NativeCollision,0});
    return result;
}
}
