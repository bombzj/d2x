#include "system.hpp"
#include "attribute_projection.hpp"
#include "native_area.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/skills/curse_resolve.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/system.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
namespace d2x::server::effects {
DomainResult<> System::monsterBloodlust(EntityId source,EntityId target,int skill,int rank,uint64_t tick) {
    const auto *unit=ports_.monsters.find(source);if(!unit || unit->rule.bloodlustState<0) return {DomainStatus::InvalidActor,{}};
    AuraDefinition aura;aura.skill=skill;aura.rank=rank;aura.state=unit->rule.states->at(unit->rule.bloodlustState);
    return castMonsterCurse(source,aura,target,tick,true);
}
DomainResult<> System::castMonsterCurse(EntityId source,const AuraDefinition &a,EntityId target,uint64_t tick,bool unique,const SkillFormulaSnapshot *sourceInput,const MonsterCursePulse *prepared) {
    const auto *emitter=ports_.monsters.find(source);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto recipient=participants.find(target);
    if(!emitter || !emitter->rule.stats || !emitter->rule.formulas || !participants.canHarm(source,target)) return {DomainStatus::InvalidActor,{}};
    CurseSpec program;int32_t length{};
    if(prepared) {program=prepared->program;length=prepared->length;}
    else {
        const auto sampled=unitStatValues(source,tick);if(!sampled) return {DomainStatus::Unavailable,{}};
        auto context=sourceInput?sourceInput->input(*emitter->rule.stats):skills::formulaInput(*emitter,*sampled);
        SkillFormulaValues formulas(*emitter->rule.formulas,context);
        const auto duration=formulas.calculation(FormulaDomain::Skill,a.skill,a.rank,"auralencalc");
        if(!duration) return {DomainStatus::Unavailable,{}};
        length=*duration;
        program.state=a.state;program.rejectZeroPrimary=!unique;program.nativeValues.emplace();program.modifiers=a.modifiers;
        program.primaryStat=formulas.auraStatKey(a.skill,1);
        program.nativeWrites=formulas.auraStats(a.skill,a.rank,6,false,true);
    }
    for(auto &write:program.nativeWrites) {
        if(!unique && recipient.monster && recipient.monster->nativeStats && recipient.monster->rule.stats) {
            const auto &rules=*recipient.monster->rule.stats;
            const auto &name=rules.definitions.at(write.key.id).name;
            if(name=="damageresist" || name=="magicresist" || name=="fireresist" || name=="lightresist" || name=="coldresist" || name=="poisonresist")
                write.value=curseResistanceAdjustment(write.value,baseStatValue(rules,*recipient.monster->nativeStats->sources,write.key),true,recipient.monster->hireling,
                    ports_.monsters.skillPrograms() && ports_.monsters.skillPrograms()->pd2);
        }
        program.nativeValues->insert_or_assign(write.key,write.value);
    }
    program.modifiers=nativeCharacterModifiers(StatSnapshot{*program.nativeValues},*emitter->rule.stats,a.modifiers);
    CombatEffectSpec spec;spec.state=a.state;spec.source={CombatEffectSource::Skill,source,a.skill,a.rank};
    spec.stacking=EffectStacking::CurseLevel;spec.modifiers=program.modifiers;spec.nativeValues=program.nativeValues;
    const auto duration=curseStateDuration(length,unitCurseResistance(target,tick),a.state,
        ports_.monsters.skillPrograms() && ports_.monsters.skillPrograms()->pd2);
    if(!duration) return {DomainStatus::Unavailable,{}};
    if(*duration) spec.duration=uint32_t(*duration);
    if(!configurePd2Curse(spec,target,tick)) return {DomainStatus::Unavailable,{}};
    if(recipient.player) {
        const auto &player=*recipient.player;auto next=state_.players;auto &states=next[target].states;
        PlayerResourceTrace resources(player,tick,{},ports_.areas.statTime(player.area));
        const auto application=applyCurseState(states,program,spec,program.nativeWrites,*emitter->rule.stats,tick,[&]{resources.sample(states,true);});
        if(!application.accepted) return {DomainStatus::Unavailable,{}};
        transactions::CharacterEdit edit{{player.player,target,player.area,ports_.areas.at(player.area).generation,0,tick},player.inventoryRevision,player.characterRevision,resources.record()};
        edit.resourceBase=resources.maximum();edit.transient=projectEffectAttributes(states,tick);
        for(const auto &removed:application.removed) if(!states.hasState(removed.effect.spec.state.id,tick)) edit.publicFacts.emplace_back(StateFact{target,0,player.area,removed.effect.spec.state.id,false});
        const auto *installed=states.find(application.handle);if(!installed) throw std::runtime_error("Missing native monster curse recipient");
        edit.publicFacts.emplace_back(StateFact{target,0,player.area,a.state.id,true,nativeEffectStats(*player.rules.skills,installed->spec)});
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) publishPlayers(next);return result;
    }
    const auto *monster=recipient.monster;
    if(!monster || !monster->alive() || (!unique && !monster->rule.curseable) || !monster->rule.stateTarget.acceptsCurseList() || !monster->nativeStats || !monster->rule.stats ||
       (!unique && monster->rule.enchantment && monster->rule.enchantment->has(38))) return {DomainStatus::Unavailable,{}};
    auto next=units_;auto &unit=next[target];unit.area=monster->area;
    MonsterResourceTrace resources(*monster,tick,&unit.states);
    const auto applied=applyCurseState(unit.states,program,spec,program.nativeWrites,*emitter->rule.stats,tick,[&]{resources.sample(unit.states,true);});
    if(!applied.accepted) return {DomainStatus::Unavailable,{}};
    auto publication=std::make_shared<AuraUnitPublication>();
    for(const auto &removed:applied.removed) if(!unit.states.hasState(removed.effect.spec.state.id,tick)) {
        unit.nativeStats.erase(removed.effect.spec.state.id);publication->facts.emplace_back(StateFact{target,1,monster->area,removed.effect.spec.state.id,false});
    }
    const auto *installed=unit.states.find(applied.handle);if(!installed) throw std::runtime_error("Missing native monster curse state");
    const auto values=combatEffectNativeValues(installed->spec);std::vector<StatValue> stats;
    if(values) for(const auto &[key,value]:*values) stats.emplace_back(key,value);
    unit.nativeStats[a.state.id]=stats;publication->facts.emplace_back(StateFact{target,1,monster->area,a.state.id,true,std::move(stats)});
    prepareUnitEffects(*publication,target,unit,resources.take(),tick);
    if(!validateAuraUnits(*publication)) return {DomainStatus::Conflict,{}};
    const auto accepted=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},monster->area},std::move(publication->facts)});
    if(!accepted) return {accepted.status,{}};
    publishAuraUnits(*publication);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::monsterCurse(EntityId source,int skill,int rank,Vec center,uint64_t tick,std::shared_ptr<MonsterCursePulse> &retained) {
    if(!retained) retained=std::make_shared<MonsterCursePulse>();
    auto &pulse=*retained;
    if(pulse.complete) return {DomainStatus::Applied,std::monostate{}};
    const auto *unit=ports_.monsters.find(source);if(!unit || !unit->rule.stats || !unit->rule.formulas || !unit->rule.states) return {DomainStatus::InvalidActor,{}};
    if(!pulse.prepared) {
        const auto stats=unitStatValues(source,tick);if(!stats) return {DomainStatus::Unavailable,{}};
        const auto expected=unit->combatRandom;auto random=expected;
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        auto input=skills::formulaInput(*unit,*stats,draw);SkillFormulaValues values(*unit->rule.formulas,input);
        const bool randomCurse=unit->rule.formulas->skills.at(skill).number("srvdofunc")==112;
        int selectedSkill=skill,index=-1;
        if(randomCurse) {
            // Retail 1.13c removed Iron Maiden from this five-entry table,
            // but retained the original index-based callback jump table.
            // Do112 selects native skill IDs. PD2 renames 66 to AmpDmg
            // and 91 to LowRes, so retail display names are not identities.
            constexpr std::array skills{66,72,82,87,91};
            index=int(limitedRandom(random,uint32_t(skills.size())));
            selectedSkill=skills[size_t(index)];
        }
        const auto &row=unit->rule.formulas->skills.at(selectedSkill);const auto state=unit->rule.states->find(row.auraTargetState);
        if(state==unit->rule.states->end()) return {DomainStatus::Unavailable,{}};
        const auto range=values.calculation(FormulaDomain::Skill,selectedSkill,rank,"aurarangecalc");
        const auto length=values.calculation(FormulaDomain::Skill,selectedSkill,rank,"auralencalc");
        if(!range || !length) return {DomainStatus::Unavailable,{}};
        CurseSpec program;program.state=state->second;program.rejectZeroPrimary=true;program.nativeValues.emplace();
        if(randomCurse) {
            const auto write=[&](std::string_view name,int32_t value) {program.nativeWrites.emplace_back(StatKey{unit->rule.stats->names.at(std::string(name)),0},value);};
            switch(index) {
            case 0:write("damageresist",-100);break;
            case 1:write("damagepercent",-50);break;
            case 2:program.unitEvents={{5,4}};break;
            case 3:program.unitEvents={{1,5},{2,5}};break;
            case 4:
                write("velocitypercent",row.number("param7"));write("damagepercent",-50);
                write("damageresist",-50);write("attackrate",row.number("param7"));break;
            }
            if(!program.nativeWrites.empty()) program.primaryStat=program.nativeWrites.front().key;
        } else {
            program.primaryStat=values.auraStatKey(skill,1);program.nativeWrites=values.auraStats(skill,rank,6,false,true);
        }
        for(const auto &write:program.nativeWrites) program.nativeValues->insert_or_assign(write.key,write.value);
        const auto targets=nativeAuraCandidateRecipients({source,unit->area,*range,randomCurse?3u:uint32_t(row.number("aurafilter")),false,
            center+ports_.areas.at(unit->area).definition.origin,NativeAuraTargetMode::TargetRange,tick});
        const auto consumed=ports_.monsters.consumeRandom(source,expected,random);if(!consumed) return consumed;
        pulse.aura.skill=selectedSkill;pulse.aura.rank=rank;pulse.aura.state=state->second;pulse.length=*length;
        pulse.program=std::move(program);pulse.targets=targets;pulse.tick=tick;pulse.prepared=true;
    }
    while(pulse.next<pulse.targets.size()) {
        const auto result=castMonsterCurse(source,pulse.aura,pulse.targets[pulse.next],pulse.tick,false,nullptr,&pulse);
        if(isDeferred(result.status) || result.status==DomainStatus::Conflict) return {DomainStatus::Capacity,{}};
        ++pulse.next;
    }
    pulse.complete=true;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::monsterAura(EntityId source,const AuraDefinition &a,EntityId id,uint64_t tick,uint64_t duration,bool owner) {
    const auto *m=ports_.monsters.find(source);if(!m) return {DomainStatus::InvalidActor,{}};
    if(a.ownerWrites || a.targetWrites)
        return paladinAura({{},source,m->area,ports_.areas.at(m->area).generation,0,tick},a,id,duration,owner,false,source);
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
    std::optional<MonsterResourceTrace> resources;
    if(target->nativeStats && target->nativeStats->sources && target->rule.stats) resources.emplace(*target,tick,&unit.states);
    const auto application=unit.states.apply(std::move(spec),tick,[&]{if(resources) resources->sample(unit.states,true);});
    if(!application.accepted) return {DomainStatus::Conflict,{}};
    if(resources) {
        resources->sample(unit.states);
        if(!ports_.monsters.canCommitLife(resources->read())) return {DomainStatus::Conflict,{}};
    }
    std::vector<StatValue> stats;
    if(const auto found=m->rule.auraStats.find(a.skill);found!=m->rule.auraStats.end()) stats=owner?found->second.owner:found->second.targets;
    const auto previous=unit.nativeStats.find(state.id);
    const bool changed=previous==unit.nativeStats.end() || previous->second!=stats || !unitStates(id,tick).contains(state.id);
    unit.nativeStats[state.id]=stats;
    std::vector<DomainFact> facts;
    for(const auto &removed:application.removed) if(!unit.states.hasState(removed.effect.spec.state.id,tick))
        facts.emplace_back(StateFact{id,1,m->area,removed.effect.spec.state.id,false});
    if(changed) facts.emplace_back(StateFact{id,1,m->area,state.id,true,std::move(stats)});
    auto timers=ports_.frameEvents.prepareEdit({FrameEventUnit::Monster,id},FrameEventType::RemoveState,unit.states.expirationEvents());
    if(!facts.empty() && !ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},m->area},std::move(facts)})) return {DomainStatus::Capacity,{}};
    ports_.frameEvents.publish(std::move(timers));units_.swap(next);
    if(resources && !ports_.monsters.commitLife(resources->take())) throw std::logic_error("Native monster aura resource commit lost its reservation");
    ports_.monsters.velocityModifier(id,units_.at(id).states.modifiers(tick).velocityPercent);
    return {DomainStatus::Applied,std::monostate{}};
}
void System::triggerMonsterCurse(EntityId id,uint64_t tick) {
    const auto *m=ports_.monsters.find(id);
    if(!m || !m->rule.enchantment || !m->rule.enchantment->curse) return;
    // UMod7's native callback casts on three of four rolls around the owner.
    auto random=m->combatRandom;const bool cast=(rollRandom(random)&3)!=0;
    if(!ports_.monsters.consumeRandom(id,m->combatRandom,random)) return;
    if(!cast) return;
    if(!m->rule.stats || !m->rule.formulas) return;
    const auto values=unitStatValues(id,tick);if(!values) return;
    auto aura=*m->rule.enchantment->curse;
    aura.rank=std::max(1,logicalStatValue(*values,*m->rule.stats,"level")/5+1);
    auto context=skills::formulaInput(*m,*values);SkillFormulaValues formulas(*m->rule.formulas,context);
    const auto range=formulas.calculation(FormulaDomain::Skill,aura.skill,aura.rank,"aurarangecalc");
    if(!range) return;
    const auto &area=ports_.areas.at(m->area);Vec origin=m->position+area.definition.origin;
    if(m->movementTarget) {
        const auto target=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(m->movementTarget);
        if(target.player) origin=target.position()+ports_.areas.at(target.player->area).definition.origin;
        else if(target.monster) origin=target.position()+ports_.areas.at(target.monster->area).definition.origin;
    } else origin=m->movementGoal+area.definition.origin;
    PendingMonsterCurse pending{aura,captureSkillFormulaInput(context),
        nativeAuraCandidateRecipients({id,m->area,std::clamp(*range,1,40),3,false,origin,NativeAuraTargetMode::TargetRange}),0,tick};
    monsterCurses_.emplace_back(id,std::move(pending));
}
StepStatus System::advanceMonsterCurses(uint64_t) {
    while(!monsterCurses_.empty()) {
        auto &[source,curse]=monsterCurses_.front();
        while(curse.next<curse.targets.size()) {
            const auto result=castMonsterCurse(source,curse.aura,curse.targets[curse.next],curse.tick,true,&curse.source);
            if(isDeferred(result.status)) return StepStatus::Blocked;
            ++curse.next;
        }
        monsterCurses_.pop_front();
    }
    return StepStatus::Complete;
}
namespace {
std::vector<int> modifierCallbacks(const monsters::Actor &unit) {
    auto result=unit.rule.nativeModifiers;
    if(unit.rule.enchantment) result.insert(result.end(),unit.rule.enchantment->ids.begin(),unit.rule.enchantment->ids.end());
    const auto append=[&](int id) {if(std::ranges::find(result,id)==result.end()) result.push_back(id);};
    if(unit.rule.hitBurst) append(20);
    if(unit.necroPet && unit.necroPet->kind==NecroSummonKind::Fire) append(31);
    return result;
}
}
void System::prepareMonsterModifierEvents(monsters::Actor &unit,const monsters::Actor *previous) const {
    if(!previous || unit.questNpc || !unit.initialModifierTimers.empty()) return;
    const bool death=previous->alive() && !unit.alive();
    const bool getHit=unit.alive() && unit.interruption!=previous->interruption && unit.hitOccurrence!=previous->hitOccurrence &&
        unit.reactionMode==3 && unit.reactionUntil>ports_.frameEvents.currentFrame() && unit.knockedUntil<=ports_.frameEvents.currentFrame();
    const bool unique=nativeUniqueMonster(unit.identity.rank);
    const auto schedule=[&](uint64_t due,int callback=0) {
        unit.initialModifierTimers.push_back(std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
            {{},{FrameEventUnit::Monster,unit.id},FrameEventType::MonsterModifier,due,false,monsterModifierTimerHandler,callback})));
    };
    for(const auto modifier:modifierCallbacks(unit)) {
        if(death && modifier==34 && unit.alignment()==UnitAlignment::Evil && unit.rule.selfResurrection &&
            int(limitedRandom(unit.combatRandom,100))<unit.rule.selfResurrection->chance)
            unit.initialModifierTimers.push_back(std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
                {{},{FrameEventUnit::Monster,unit.id},FrameEventType::MonsterModifier,
                    unit.deathTick+uint64_t(unit.rule.selfResurrection->delay),false,0,34})));
        if(death && ((modifier==9 && unique) || (modifier==18 && unique) || modifier==10 || modifier==31 || modifier==33)) schedule(unit.deathTick+4);
        if((death || getHit) && modifier==20) schedule((death?unit.deathTick:ports_.frameEvents.currentFrame())+2);
        if(getHit && unique && modifier==17) schedule(ports_.frameEvents.currentFrame()+2);
    }
    // QueenDeath's mode callback is separate from the MONUMOD slot walk.
    if(death && unit.rule.deathBurst) schedule(unit.deathTick+1,23);
}
DomainResult<> System::executeMonsterModifier(EntityId id,int modifier,uint64_t tick,ModifierExecution &execution) {
    if(modifier==33) return suicideDeathEffect(id,tick,execution);
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto *unit=ports_.monsters.find(id);if(!unit) return done();
    const auto &m=*unit;const auto &area=ports_.areas.at(m.area);
    const bool unique=nativeUniqueMonster(m.identity.rank);
    const auto values=unitStatValues(id,tick);if(!values || !m.rule.stats) return {DomainStatus::Unavailable,{}};
    const auto level=logicalStatValue(*values,*m.rule.stats,"level");
    const auto spawn=[&](int definition,int rank,bool charged,bool ring)->DomainResult<> {
        std::vector<missiles::NativeSpawn> births;
        const auto add=[&](Vec target,int branch) {
            missiles::NativeSpawn request{id,m.area,area.generation,tick,m.position,target,definition,0,rank};
            if(charged) {request.initialization=missiles::NativeSpawn::Initialization::ChargedSkill;request.initializationIndex=branch;}
            if(ring) {
                const auto frost=ports_.missiles.nativeDefinition("frostnova");
                if(frost) request.nativeVelocity=m.rule.formulas->missiles.at(*frost).number("vel");
            }
            births.push_back(request);
        };
        if(charged) for(const auto &ray:monsterLightningRays()) add(m.position+ray.offset,ray.pathIndex);
        else if(ring) for(const auto heading:missileRingBurst(1)) add(m.position+heading,0);
        else add(m.position,0);
        auto publication=ports_.missiles.prepareNativeBirth(births);if(!publication) return {publication.status,{}};
        const auto result=ports_.missiles.commitNativeBirth(std::move(*publication.value));
        return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
    };
    if(modifier==9 || modifier==31) {
        if(!execution.missile) {
            const auto definition=ports_.missiles.nativeDefinition("monstercorpseexplode");if(!definition) return {DomainStatus::Unavailable,{}};
            const auto born=ports_.missiles.spawnNative({id,m.area,area.generation,tick,m.position,m.position,*definition,0,1});
            if(!born) return {born.status,{}};
            execution.missile=*born.value;
        }
        if(!execution.damagePrepared) {
            NativeAreaSpec spec;spec.source=id;spec.area=m.area;spec.center=m.position;
            spec.radius=modifier==31?6:m.rule.difficulty+4;spec.filter=modifier==31?0x583:0x581;spec.resultFlags=1;spec.rank=1;spec.radialBarrier=true;
            const auto contact=ports_.missiles.nativeAreaContact(execution.missile);if(!contact) return {DomainStatus::Unavailable,{}};
            spec.missileContact=std::make_shared<combat::SpellImpact>(*contact);
            int32_t damage=0x6400;
            if(modifier==9) {
                const auto table=m.rule.lifeTables.at(size_t(m.rule.difficulty));if(!table) return {DomainStatus::Unavailable,{}};
                const auto bounds=monsterLifeBounds(*table,level,m.rule.difficulty,m.rule.statGameType);
                const auto high=fireEnchantedDeathMaximum(bounds.maximum,m.rule.corpseExplosionPercent,m.rule.difficulty),low=nativePercentage(high,60,100);
                const auto expected=m.combatRandom;auto random=expected;
                damage=nativeShiftLeft(nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeSubtract(high,low))))),6);
                const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return consumed;
            }
            spec.damage.channels[0]=spec.damage.channels[2]=damage;
            execution.area=std::make_shared<NativeAreaPulse>();execution.area->spec=std::move(spec);execution.damagePrepared=true;
        }
        return nativeArea(execution.area->spec,tick,execution.area);
    }
    if(modifier==17) {
        if(!unique) return done();
        auto &cycle=uniqueCycles_[id];
        if(cycle.emissionPending) {
            const auto posted=ports_.monsters.lightningEmission(id,true,tick);if(posted) cycle.emissionPending=false;return posted;
        }
        if(tick<std::max(uint64_t{10},cycle.nextLightning)) return ports_.monsters.lightningEmission(id,false,tick);
        const auto definition=ports_.missiles.nativeDefinition("lightunique");if(!definition) return {DomainStatus::Unavailable,{}};
        if(!ports_.events.hasCapacity(2)) return {DomainStatus::Capacity,{}};
        const auto result=spawn(*definition,std::max(level/2,1),true,false);if(!result) return result;
        cycle.nextLightning=tick+10;cycle.emissionPending=true;
        const auto posted=ports_.monsters.lightningEmission(id,true,tick);if(posted) cycle.emissionPending=false;return posted;
    }
    if(modifier==18) {
        if(!unique) return done();
        const auto definition=ports_.missiles.nativeDefinition("coldunique");
        return definition?spawn(*definition,std::max(level/2,1),false,true):DomainResult<>{DomainStatus::Unavailable,{}};
    }
    if(modifier==10) {
        const auto definition=ports_.missiles.nativeDefinition("corpsepoisoncloud");
        return definition?spawn(*definition,std::max(level,1),false,false):DomainResult<>{DomainStatus::Unavailable,{}};
    }
    if(modifier==20 && m.rule.hitBurst) return spawn(*m.rule.hitBurst,level,true,false);
    if(modifier==23 && m.rule.deathBurst) return spawn(*m.rule.deathBurst,level,true,false);
    return done();
}
DomainResult<> System::monsterModifierEvent(const FrameEvent &event,uint64_t tick) {
    if(event.customId==23) return monsterDeathEffect(event.owner.unit,tick);
    auto &execution=modifierExecutions_[event.handle];execution.owner=event.owner.unit;
    if(!ports_.monsters.find(execution.owner)) {modifierExecutions_.erase(event.handle);return {DomainStatus::Applied,std::monostate{}};}
    // MONSTERMODE_EventHandler consumes a one-shot MONUMOD without calling
    // it while its living owner is frozen. An entered callback must finish.
    if(!execution.captured && ports_.monsters.frozen(execution.owner,tick)) {
        modifierExecutions_.erase(event.handle);return {DomainStatus::Applied,std::monostate{}};
    }
    if(!execution.captured) {
        execution.callbacks=event.customId?std::vector<int>{event.customId}:modifierCallbacks(*ports_.monsters.find(execution.owner));
        execution.captured=true;
    }
    while(execution.next<execution.callbacks.size()) {
        const auto result=executeMonsterModifier(execution.owner,execution.callbacks[execution.next],tick,execution);
        if(isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale) return {DomainStatus::Capacity,{}};
        if(!result) return result;
        ++execution.next;execution.missile={};execution.damagePrepared=false;execution.area.reset();
    }
    modifierExecutions_.erase(event.handle);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::monsterDamageEffect(EntityId id,uint64_t tick) {
    const auto suicide=suicideModeChange(id,tick);if(!suicide) return suicide;
    const auto death=monsterDeathEffect(id,tick);if(!death) return death;
    const auto *m=ports_.monsters.find(id);
    if(!m || !m->rule.enchantment || !nativeUniqueMonster(m->identity.rank) || !m->rule.enchantment->has(17) ||
       (m->reactionUntil>tick && m->reactionMode==3)) return {DomainStatus::Applied,std::monostate{}};
    ModifierExecution execution;return executeMonsterModifier(id,17,tick,execution);
}
DomainResult<> System::monsterDeathEffect(EntityId id,uint64_t tick) {
    const auto *unit=ports_.monsters.find(id);
    if(unit && !unit->alive() && unit->rule.ai.kind==MonsterAiKind::SiegeBeast) {
        const auto ejected=ports_.skills.ejectImp(id,tick);if(!ejected) return ejected;
    }
    if(!unit || unit->alive() || (!unit->rule.deathBurst && !unit->rule.boneDeathExplosion)) return {DomainStatus::Applied,std::monostate{}};
    auto &cycle=uniqueCycles_[id];if(cycle.queenDeath==unit->interruption) return {DomainStatus::Applied,std::monostate{}};
    if(!cycle.deathExecution) cycle.deathExecution=std::make_shared<ModifierExecution>();
    const auto result=unit->rule.boneDeathExplosion?boneDeathEffect(id,tick,*cycle.deathExecution):executeMonsterModifier(id,23,tick,*cycle.deathExecution);
    if(result) {cycle.queenDeath=unit->interruption;cycle.deathExecution.reset();}return result;
}
DomainResult<> System::boneDeathEffect(EntityId id,uint64_t tick,ModifierExecution &execution) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    if(!execution.missile) {
        const auto definition=ports_.missiles.nativeDefinition("monstercorpseexplode");if(!definition) return {DomainStatus::Unavailable,{}};
        const auto born=ports_.missiles.spawnNative({id,unit.area,area.generation,tick,unit.position,unit.position,*definition,0,1});
        if(!born) return {born.status,{}};
        execution.missile=*born.value;
    }
    if(!execution.damagePrepared) {
        const auto table=unit.rule.lifeTables.at(size_t(unit.rule.difficulty));if(!table) return {DomainStatus::Unavailable,{}};
        const auto values=unitStatValues(id,tick);if(!values) return {DomainStatus::Unavailable,{}};
        const int level=logicalStatValue(*values,*unit.rule.stats,"level");
        // Retail death mode passes Game.dwGameType (Ladder) alone here,
        // unlike initial HP, which passes nGameType || dwGameType.
        const auto bounds=monsterLifeBounds(*table,level,unit.rule.difficulty,ports_.monsters.gameSettings().ladder);
        const int high=nativePercentage(bounds.maximum,unit.rule.corpseExplosionPercent,100),low=nativePercentage(high,60,100);
        auto random=unit.combatRandom;
        const int damage=nativeShiftLeft(nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeSubtract(high,low))))),7);
        const auto contact=ports_.missiles.nativeAreaContact(execution.missile);if(!contact) return {DomainStatus::Unavailable,{}};
        NativeAreaSpec spec;spec.source=id;spec.area=unit.area;spec.center=unit.position;
        spec.radius=5;spec.filter=0x581;spec.resultFlags=1;spec.rank=1;spec.radialBarrier=true;
        spec.damage.channels[0]=damage;spec.missileContact=std::make_shared<combat::SpellImpact>(*contact);
        auto pulse=std::make_shared<NativeAreaPulse>();pulse->spec=std::move(spec);
        const auto consumed=ports_.monsters.consumeRandom(id,unit.combatRandom,random);if(!consumed) return consumed;
        execution.area=std::move(pulse);execution.damagePrepared=true;
    }
    return nativeArea(execution.area->spec,tick,execution.area);
}
}
