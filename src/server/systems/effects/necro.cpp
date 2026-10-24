#include "system.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/runtime/skill_rules.hpp"
#include "attribute_projection.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/event_frame.hpp"
#include "gameplay/combat/shield.hpp"
#include "gameplay/combat/reception.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/behavior.hpp"
#include "server/systems/combat/system.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/skills/common_actions.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/skills/curse_resolve.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <array>
#include "server/systems/skills/evaluation.hpp"
#include <algorithm>

namespace d2x::server::effects {
DomainResult<> System::nativeTerror(const ActorContext &actor,const SkillCastSpec &skill,EntityId id) {
    const auto *player=ports_.players.findActor(actor.actor);const auto *unit=ports_.monsters.find(id);
    if(!player || !player->rules.skills || !skill.curse || !unit || !unit->alive() || !unit->nativeStats || !unit->rule.stats)
        return {DomainStatus::Unavailable,{}};
    const auto &program=*skill.curse;
    const auto &rules=*player->rules.skills;const auto state=rules.terror;
    if(!unit->rule.switchAi || !unit->damageable() || unit->rule.boss || unit->identity.rank==MonsterRank::Unique ||
        unit->identity.rank==MonsterRank::SuperUnique || unit->identity.rank==MonsterRank::Champion ||
        unitStates(id,actor.tick).contains(unit->rule.uninterruptableState)) return {DomainStatus::Unavailable,{}};
    if(program.skipActive && unitStates(id,actor.tick).contains(program.state.id)) return {DomainStatus::Applied,std::monostate{}};
    if(program.targetLevelBonus && nativeAdd(nativeAdd(skill.rank,program.targetLevelBonus),player->persistent.player.level)<=unit->rule.level)
        return {DomainStatus::Applied,std::monostate{}};
    const NativeFear request{id,program.fearSource?program.fearSource:actor.actor,state,
        rules.formulas->skillIds.at("terror"),1,program.frames,true,program.fearOrigin};
    auto candidate=prepareFear(request,actor.tick);if(!candidate) return {candidate.status,{}};
    auto publication=std::move(*candidate.value);
    transactions::CharacterEdit debit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
    debit.player.mana-=skill.manaCost;debit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};debit.charge=skill.charge;
    auto prepared=ports_.transactions.prepare(std::move(debit),nullptr,{},std::move(publication));if(!prepared) return {prepared.status,{}};
    return ports_.transactions.commit(std::move(*prepared.value));
}
DomainResult<std::shared_ptr<AuraUnitPublication>> System::prepareFear(const NativeFear &request,uint64_t tick) const {
    const auto id=request.target,owner=request.owner;const auto state=request.state;
    const auto *unit=ports_.monsters.find(id);
    if(!owner || !unit || !unit->alive() || !unit->nativeStats || !unit->rule.stats || !unit->rule.switchAi ||
       !unit->damageable() || unit->rule.boss || nativeUniqueMonster(unit->identity.rank) ||
       unitStates(id,tick).contains(unit->rule.uninterruptableState)) return {DomainStatus::Unavailable,{}};
    if(!request.utility && (unit->alignment()==UnitAlignment::Neutral || !unit->rule.curseable)) return {DomainStatus::Unavailable,{}};
    UnitEffect recipient;if(const auto found=units_.find(id);found!=units_.end()) recipient=found->second;recipient.area=unit->area;
    MonsterResourceTrace resources(*unit,tick,&recipient.states);
    // AIUTIL deliberately uses Terror / level 1 and its ordinary state
    // removal callback, regardless of the originating Howl/GrimWard skill.
    const auto duration=curseStateDuration(request.frames,unitCurseResistance(id,tick),state.curse,
        ports_.monsters.skillPrograms() && ports_.monsters.skillPrograms()->pd2);
    auto publication=std::make_shared<AuraUnitPublication>();
    bool posted{};
    if(duration && unit->rule.stateTarget.acceptsCurseList()) {
        CombatEffectSpec spec;spec.state=state;spec.source={CombatEffectSource::Skill,owner,request.skill,request.rank};
        spec.curseAi=request.utility?CurseAi::None:CurseAi::Terror;
        spec.stacking=EffectStacking::CurseLevel;spec.nativeValues.emplace();spec.nativeState=true;spec.refreshPassivesOnRemoval=true;
        if(*duration) spec.duration=uint32_t(*duration);
        CurseSpec direct;direct.state=state;direct.targetProcedure=CurseSpec::TargetProcedure::DirectList;direct.nativeValues.emplace();direct.rejectZeroPrimary=false;
        if(!configurePd2Curse(spec,id,tick)) return {DomainStatus::Unavailable,{}};
        const auto installed=applyCurseState(recipient.states,direct,spec,{},*unit->rule.stats,tick,[&]{resources.sample(recipient.states,true);});
        if(installed.accepted) {
            posted=true;
            for(const auto &removed:installed.removed) if(!recipient.states.hasState(removed.effect.spec.state.id,tick)) {
                recipient.nativeStats.erase(removed.effect.spec.state.id);
                publication->facts.emplace_back(StateFact{id,1,unit->area,removed.effect.spec.state.id,false});
            }
            recipient.nativeStats[state.id]={};publication->facts.emplace_back(StateFact{id,1,unit->area,state.id,true});
        }
    }
    // Target selection and the one-frame idle happen even when curse posting
    // is refused by resistance or an existing protected Attract list.
    if(!request.utility && !posted) return {DomainStatus::Unavailable,{}};
    auto life=resources.take();
    auto origin=request.origin;
    if(origin) *origin=*origin-ports_.areas.at(unit->area).definition.origin;
    life.curseControl=monsters::CurseControl{CurseAi::Terror,owner,request.utility?owner:unit->movementTarget,UINT64_MAX,origin,state.id};
    life.controlCallbackEpoch=recipient.states.controlResetEpoch();life.reselectAi=true;
    if(request.utility) life.aiThinkAt=tick+1;
    life.stopMovement=request.utility;life.changed=true;
    prepareUnitEffects(*publication,id,recipient,std::move(life),tick);
    return {DomainStatus::Applied,std::move(publication)};
}
DomainResult<> System::nativeFear(const NativeFear &request,uint64_t tick) {
    auto prepared=prepareFear(request,tick);if(!prepared) return {prepared.status,{}};
    auto &publication=**prepared.value;
    if(!validateAuraUnits(publication)) return {DomainStatus::Conflict,{}};
    const auto *unit=ports_.monsters.find(request.target);if(!unit) return {DomainStatus::Stale,{}};
    const auto output=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},unit->area},std::move(publication.facts)});
    if(!output) return {output.status,{}};
    publishAuraUnits(publication);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::nativeAttract(const ActorContext &actor,const SkillCastSpec &skill,EntityId selected) {
    const auto *player=ports_.players.find(actor.player);const auto *monster=ports_.monsters.find(selected);
    if(!player || !skill.curse || !monster || !monster->rule.stateTarget.acceptsCurseList() || monster->alignment()!=UnitAlignment::Evil || !monster->alive() ||
       !skillAiControlAllowed(monster->identity.rank,monster->rule.switchAi,monster->rule.boss) ||
       ports_.areas.at(monster->area).definition.town) return {DomainStatus::Unavailable,{}};
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(!participants.canHarm(actor.actor,selected)) return {DomainStatus::Unavailable,{}};
    if(!monster->nativeStats || !monster->nativeStats->sources || !monster->rule.stats) return {DomainStatus::Unavailable,{}};
    const auto &program=*skill.curse;
    const int divisor=player->rules.skills->aiCurseDivisor.at(size_t(player->persistent.difficulty));
    const int32_t duration=(divisor?nativeDivide(program.frames,divisor):program.frames);
    const auto frame=nativeAdd(nativeNarrow(uint32_t(actor.tick)),duration);
    const auto deadline=nativeEventDeadline(actor.tick,frame);
    auto next=units_;auto &target=next[selected];target.area=monster->area;
    std::vector<monsters::LifeChange> changes;
    auto publication=std::make_shared<AuraUnitPublication>();
    // SrvDo059 changes alignment/target-node membership before searching.
    // It does not activate a new AITHINK function on the selected monster.
    MonsterResourceTrace initial(*monster,actor.tick,&target.states);
    initial.writeBaseStat("alignment",1,combatEffectStatSources(target.states.entries(),actor.tick,true));
    auto selectedLife=initial.take();selectedLife.specialTargetNode=true;selectedLife.changed=true;
    const auto leader=ports_.monsters.minionOwner(selected);
    if(leader==selected) {
        selectedLife.nativeMinions=std::vector<EntityId>{};
        for(const auto &[id,child]:ports_.monsters.read().actors) if(id!=selected && ports_.monsters.minionOwner(id)==selected) {
            auto &unit=next[id];unit.area=child.area;
            monsters::LifeChange life{id,child.revision,Fixed8::fromRaw(child.life),Fixed8::fromRaw(child.maximumLife),child.nativeStats,true};
            life.detachedMinion=true;changes.push_back(std::move(life));
        }
    } else if(const auto *parent=ports_.monsters.find(leader)) {
        selectedLife.detachedMinion=true;
        auto &unit=next[leader];unit.area=parent->area;
        monsters::LifeChange life{leader,parent->revision,Fixed8::fromRaw(parent->life),Fixed8::fromRaw(parent->maximumLife),parent->nativeStats,true};
        life.nativeMinions=parent->nativeMinions;std::erase(*life.nativeMinions,selected);
        changes.push_back(std::move(life));
    }
    publication->facts.emplace_back(StateFact{selected,1,monster->area,player->rules.skills->alignment.id,true,
        {{player->rules.skills->nativeStats.at("alignment"),1}}});
    const Vec center=monster->position+ports_.areas.at(monster->area).definition.origin;
    NativeAuraTargetQuery query{actor.actor,actor.area,program.radius,uint32_t(program.targetFilter),false,center,NativeAuraTargetMode::TargetRange,actor.tick};
    for(const auto id:nativeAuraCandidateRecipients(query)) {
        // The selected unit is already NEUTRAL in this synchronous candidate.
        if(id==selected) continue;
        const auto *recipient=ports_.monsters.find(id);
        if(!recipient || !recipient->alive() || recipient->alignment()!=UnitAlignment::Evil ||
           !skillAiControlAllowed(recipient->identity.rank,recipient->rule.switchAi,recipient->rule.boss) ||
           ports_.areas.at(recipient->area).definition.town || !participants.canHarm(actor.actor,id)) continue;
        auto &unit=next[id];unit.area=recipient->area;
        monsters::LifeChange life{id,recipient->revision,Fixed8::fromRaw(recipient->life),Fixed8::fromRaw(recipient->maximumLife),recipient->nativeStats,true};
        life.attraction=monsters::AttractionControl{selected,deadline.due};
        life.aiResetTimers=recipient->aiResetTimers;life.aiResetTimers->push_back({deadline.due,deadline.continuous,ports_.frameEvents.reserveIdentity()});
        const auto existing=std::find_if(changes.begin(),changes.end(),[&](const auto &change){return change.actor==id;});
        if(existing!=changes.end()) {retainPreparedUnitControl(life,*existing);*existing=std::move(life);} else changes.push_back(std::move(life));
    }
    // Curse allocation can fail (resistance/Attract protection) after the
    // neutral alignment and nearby selectors have already been prepared.
    const auto adjusted=curseStateDuration(duration,unitCurseResistance(selected,actor.tick),program.state.curse,player->rules.skills->pd2);
    if(adjusted && monster->rule.curseable) {
        auto shadow=*monster;shadow.nativeStats=selectedLife.intrinsic;shadow.life=selectedLife.life.raw();shadow.maximumLife=selectedLife.maximum.raw();
        MonsterResourceTrace resources(shadow,actor.tick,&target.states);
        CombatEffectSpec curse;curse.state=program.state;curse.source={CombatEffectSource::Skill,actor.actor,skill.sourceId,skill.rank};
        curse.stacking=EffectStacking::CurseLevel;curse.curseAi=CurseAi::Attract;curse.nativeValues.emplace();
        if(*adjusted) curse.duration=uint32_t(*adjusted);
        auto allocation=program;allocation.primaryStat.reset();allocation.nativeValues.emplace();allocation.nativeWrites.clear();
        auto applied=EffectApplication{{},{},false};
        if(configurePd2Curse(curse,selected,actor.tick))
            applied=applyCurseState(target.states,allocation,curse,{},*player->rules.skills->stats,actor.tick,
                [&]{resources.sample(target.states,true);});
        if(applied.accepted) {
            auto life=resources.take();retainPreparedUnitControl(life,selectedLife);selectedLife=std::move(life);
            for(const auto &removed:applied.removed) if(!target.states.hasState(removed.effect.spec.state.id,actor.tick)) {
                target.nativeStats.erase(removed.effect.spec.state.id);
                publication->facts.emplace_back(StateFact{selected,1,monster->area,removed.effect.spec.state.id,false});
            }
            const auto *installed=target.states.find(applied.handle);
            if(!installed) throw std::runtime_error("Missing selected Attract state");
            target.nativeStats[program.state.id]=nativeEffectStats(*player->rules.skills,installed->spec);
            publication->facts.emplace_back(StateFact{selected,1,monster->area,program.state.id,true,target.nativeStats[program.state.id]});
        }
    }
    changes.push_back(std::move(selectedLife));
    for(auto &life:changes) {const auto id=life.actor;prepareUnitEffects(*publication,id,next.at(id),std::move(life),actor.tick);}
    transactions::CharacterEdit debit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
    debit.player.mana-=skill.manaCost;debit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};debit.charge=skill.charge;
    auto prepared=ports_.transactions.prepare(std::move(debit),nullptr,{},std::move(publication));if(!prepared) return {prepared.status,{}};
    return ports_.transactions.commit(std::move(*prepared.value));
}
DomainResult<> System::curse(const ActorContext &actor,const SkillCastSpec &skill,Vec center,EntityId selected) {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    const auto *player=caster?caster->controller:nullptr;const auto *area=ports_.areas.find(actor.area);
    if(!caster || !player || !player->entered || caster->area()!=actor.area || (caster->player?player->persistent.player.hp<=0:!caster->monster->alive()) ||
       !area || area->generation!=actor.areaGeneration || !skill.curse || player->persistent.player.mana<skill.manaCost)
        return {DomainStatus::InvalidActor,{}};
    const auto &program=*skill.curse;
    const bool pd2=caster->programs() && caster->programs()->pd2;
    if(caster->monster && program.targetProcedure!=CurseSpec::TargetProcedure::Cloak) return {DomainStatus::Unavailable,{}};
    if(program.targetProcedure==CurseSpec::TargetProcedure::TerrorUtility) return nativeTerror(actor,skill,selected);
    if(program.ai==CurseAi::Attract) return nativeAttract(actor,skill,selected);
    const auto aiEligible=[](const monsters::Actor &m) {return skillAiControlAllowed(m.identity.rank,m.rule.switchAi,m.rule.boss);};
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    auto next=units_;auto nextPlayers=state_.players;
    std::vector<transactions::Change> changesToPrepare;
    std::vector<monsters::LifeChange> lifeChanges;
    const auto payment=caster->monster?ActorContext{player->player,player->actor,player->area,ports_.areas.at(player->area).generation,actor.sequence,actor.tick}:actor;
    transactions::CharacterEdit debit{payment,player->inventoryRevision,player->characterRevision,player->persistent.player};
    debit.player.mana-=skill.manaCost;debit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};debit.charge=skill.charge;
    auto publication=std::make_shared<AuraUnitPublication>();
    const bool cloak=program.targetProcedure==CurseSpec::TargetProcedure::Cloak;
    if(cloak) {
        const auto *current=unitEffects(caster->id());
        if(!skill.appliedEffect || (current && current->hasState(skill.appliedEffect->state.id,actor.tick)))
            return {DomainStatus::Unavailable,{}};
        if(caster->monster) {
            const auto &body=*caster->monster;auto &unit=next[body.id];unit.area=body.area;
            MonsterResourceTrace resources(body,actor.tick,&unit.states);
            auto own=*skill.appliedEffect;own.source={CombatEffectSource::Skill,body.id,skill.sourceId,skill.rank};
            const auto applied=applySkillState(unit.states,skill,own,*body.rule.stats,*body.rule.states,actor.tick,[&]{resources.sample(unit.states,true);});
            if(!applied.accepted) return {DomainStatus::Unavailable,{}};
            for(const auto &removed:applied.removed) if(!unit.states.hasState(removed.effect.spec.state.id,actor.tick))
                publication->facts.emplace_back(StateFact{body.id,1,body.area,removed.effect.spec.state.id,false});
            const auto *installed=unit.states.find(applied.handle);if(!installed) throw std::logic_error("Missing native monster Cloak owner list");
            auto stats=nativeEffectStats(*body.rule.stats,installed->spec);unit.nativeStats[own.state.id]=stats;
            publication->facts.emplace_back(StateFact{body.id,1,body.area,own.state.id,true,std::move(stats)});
            resources.sample(unit.states);lifeChanges.push_back(resources.take());
        } else {
        auto &states=nextPlayers[player->actor].states;
        PlayerResourceTrace resources(*player,actor.tick,skill.manaCost,ports_.areas.statTime(actor.area));
        const auto applied=applySkillState(states,skill,*skill.appliedEffect,*player->rules.skills->stats,*player->rules.character->states,actor.tick,
            [&]{resources.sample(states,true);});
        if(!applied.accepted) return {DomainStatus::Unavailable,{}};
        debit.player=resources.record();debit.resourceBase=resources.maximum();debit.transient=projectEffectAttributes(states,actor.tick);
        for(const auto &removed:applied.removed) if(!states.hasState(removed.effect.spec.state.id,actor.tick))
            debit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,removed.effect.spec.state.id,false});
        const auto *installed=states.find(applied.handle);if(!installed) throw std::logic_error("Missing native Cloak owner list");
        debit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,installed->spec.state.id,true,nativeEffectStats(*player->rules.skills,installed->spec)});
        }
    }
    Vec origin=center+area->definition.origin;
    if(selected) {
        const auto target=participants.find(selected);
        if(target.player) origin=target.position()+ports_.areas.at(target.player->area).definition.origin;
        else if(target.monster) origin=target.position()+ports_.areas.at(target.monster->area).definition.origin;
    }
    std::vector<EntityId> targets;
    if(program.singleTarget) {
        if(selected && program.targetProcedure==CurseSpec::TargetProcedure::DirectList) {
            const auto eligible=nativeAuraCandidateRecipients({actor.actor,actor.area,0,uint32_t(program.targetFilter),false,origin,NativeAuraTargetMode::TargetRange,actor.tick});
            if(std::find(eligible.begin(),eligible.end(),selected)!=eligible.end()) targets.push_back(selected);
        } else if(selected) targets.push_back(selected);
    }
    else targets=nativeAuraCandidateRecipients({actor.actor,actor.area,program.radius,uint32_t(program.targetFilter),cloak,
        cloak?std::optional<Vec>{}:origin,cloak?NativeAuraTargetMode::OwnerRange:NativeAuraTargetMode::TargetRange,actor.tick});
    for(const auto id:targets) {
        if(const auto *peer=ports_.players.findActor(id)) {
            const auto &recipient=*peer;
            if(!(program.targetFilter&1) || (!cloak && program.ai!=CurseAi::None) || !recipient.entered || recipient.persistent.player.hp<=0 ||
               !participants.canHarm(actor.actor,id)) continue;
            const auto duration=curseStateDuration(program.permanent?0:program.frames,unitCurseResistance(recipient.actor,actor.tick),program.state.curse,pd2);
            if(!duration) continue;
            auto &states=nextPlayers[recipient.actor].states;
            if(program.skipActive && states.hasState(program.state.id,actor.tick)) continue;
            CombatEffectSpec curse;curse.state=program.state;curse.stacking=EffectStacking::CurseLevel;
            curse.source={CombatEffectSource::Skill,actor.actor,skill.sourceId,skill.rank};curse.modifiers=program.modifiers;
            curse.nativeValues=program.nativeValues;
            if(*duration) curse.duration=uint32_t(*duration);
            if(!configurePd2Curse(curse,id,actor.tick)) continue;
            PlayerResourceTrace resources(recipient,actor.tick,{},ports_.areas.statTime(recipient.area));
            const auto applied=applyCurseState(states,program,curse,program.nativeWrites,*player->rules.skills->stats,actor.tick,
                [&]{resources.sample(states,true);});
            if(!applied.accepted) continue;
            transactions::CharacterEdit edit{{recipient.player,recipient.actor,recipient.area,ports_.areas.at(recipient.area).generation,0,actor.tick},recipient.inventoryRevision,recipient.characterRevision,resources.record()};
            edit.resourceBase=resources.maximum();
            edit.transient=projectEffectAttributes(states,actor.tick);
            for(const auto &removed:applied.removed) if(!states.hasState(removed.effect.spec.state.id,actor.tick))
                edit.publicFacts.emplace_back(StateFact{recipient.actor,0,recipient.area,removed.effect.spec.state.id,false});
            const auto *installed=states.find(applied.handle);
            if(!installed) throw std::runtime_error("Missing installed native player curse state");
            edit.publicFacts.emplace_back(StateFact{recipient.actor,0,recipient.area,program.state.id,true,nativeEffectStats(*player->rules.skills,installed->spec)});
            changesToPrepare.emplace_back(std::move(edit));
            continue;
        }
        const auto *unit=ports_.monsters.find(id);if(!unit) continue;
        const auto &monster=*unit;
        const bool necromancerFilter=program.targetProcedure==CurseSpec::TargetProcedure::Necromancer;
        if(!(program.targetFilter&2) || !participants.canHarm(actor.actor,id) || !monster.alive() || (necromancerFilter && !monster.rule.curseable) ||
           !monster.rule.stateTarget.acceptsCurseList() ||
           (necromancerFilter && monster.rule.enchantment && monster.rule.enchantment->has(38))) continue;
        if(!cloak && program.ai!=CurseAi::None && (!aiEligible(monster) || monster.alignment()==UnitAlignment::Neutral)) continue;
        const auto controlAi=cloak && !aiEligible(monster)?CurseAi::None:program.ai;
        if((program.ai==CurseAi::Confuse || program.ai==CurseAi::Attract) && monster.alignment()!=UnitAlignment::Evil) continue;
        if(program.targetLevelBonus && skill.rank+program.targetLevelBonus+player->persistent.player.level<=monster.rule.level) continue;
        if(program.skipActive && unitStates(id,actor.tick).contains(program.state.id)) continue;
        const auto resetTaunt=[&] {
            auto &recipient=next[id];recipient.area=monster.area;
            monsters::LifeChange life{id,monster.revision,Fixed8::fromRaw(monster.life),Fixed8::fromRaw(monster.maximumLife),monster.nativeStats,true};
            life.tauntPostActions=true;life.tauntTarget=actor.actor;life.aiThinkAt=actor.tick+1;
            lifeChanges.push_back(std::move(life));
        };
        const int resistance=unitCurseResistance(id,actor.tick);
        const int divisor=program.ai==CurseAi::None || program.ignoreDifficultyDivisor?1:player->rules.skills->aiCurseDivisor.at(size_t(player->persistent.difficulty));
        const auto duration=curseStateDuration(program.permanent?0:(divisor?nativeDivide(program.frames,divisor):program.frames),resistance,program.state.curse,pd2);
        if(!duration) {if(program.ai==CurseAi::Taunt) resetTaunt();continue;}
        auto &recipient=next[id];recipient.area=monster.area;
        CombatEffectSpec curse;curse.state=program.state;curse.stacking=EffectStacking::CurseLevel;
        curse.source={CombatEffectSource::Skill,actor.actor,skill.sourceId,skill.rank};
        if(*duration) curse.duration=uint32_t(*duration);
        curse.modifiers=program.modifiers;
        curse.curseAi=controlAi;
        // SkillNec tests STAT_UNITBASE independently for each immune channel.
        auto &modifiers=curse.modifiers;
        auto nativeWrites=program.nativeWrites;
        const auto baseResistance=[&](std::string_view name,size_t channel) {
            if(monster.nativeStats && monster.nativeStats->sources && monster.rule.stats)
                return baseStatValue(*monster.rule.stats,*monster.nativeStats->sources,{monster.rule.stats->names.at(std::string(name)),0});
            return monster.rule.resistances[channel];
        };
        modifiers.combat.physicalResist=curseResistanceAdjustment(modifiers.combat.physicalResist,baseResistance("damageresist",0),true,monster.hireling,pd2);
        modifiers.combat.magicResist=curseResistanceAdjustment(modifiers.combat.magicResist,baseResistance("magicresist",1),true,monster.hireling,pd2);
        modifiers.fireResist=curseResistanceAdjustment(modifiers.fireResist,baseResistance("fireresist",2),true,monster.hireling,pd2);
        modifiers.lightningResist=curseResistanceAdjustment(modifiers.lightningResist,baseResistance("lightresist",3),true,monster.hireling,pd2);
        modifiers.coldResist=curseResistanceAdjustment(modifiers.coldResist,baseResistance("coldresist",4),true,monster.hireling,pd2);
        modifiers.poisonResist=curseResistanceAdjustment(modifiers.poisonResist,baseResistance("poisonresist",5),true,monster.hireling,pd2);
        if(program.nativeValues) {
            curse.nativeValues=program.nativeValues;
            const std::array<std::string_view,6> names{"damageresist","magicresist","fireresist","lightresist","coldresist","poisonresist"};
            for(size_t channel=0;channel<names.size();++channel) {
                const auto stat=player->rules.skills->stats->names.at(std::string(names[channel]));
                if(auto value=curse.nativeValues->find({stat,0});value!=curse.nativeValues->end())
                    value->second=curseResistanceAdjustment(value->second,baseResistance(names[channel],channel),true,monster.hireling,pd2);
                for(auto &write:nativeWrites) if(write.key.id==stat)
                    write.value=curseResistanceAdjustment(write.value,baseResistance(names[channel],channel),true,monster.hireling,pd2);
            }
        }
        if(!configurePd2Curse(curse,id,actor.tick)) continue;
        std::optional<MonsterResourceTrace> resources;
        if(monster.nativeStats && monster.nativeStats->sources && monster.rule.stats) resources.emplace(monster,actor.tick,&recipient.states);
        const bool targetControl=program.ai==CurseAi::Confuse || program.ai==CurseAi::Attract;
        const auto applied=applyCurseState(recipient.states,program,curse,nativeWrites,*player->rules.skills->stats,actor.tick,
            [&]{if(resources) resources->sample(recipient.states,true);},[&](const EffectApplication &application) {
                if(resources && controlAi!=CurseAi::None && !targetControl) {
                    const auto *installed=recipient.states.find(application.handle);
                    if(!installed) throw std::runtime_error("Missing posted AI curse state");
                    resources->seedCurseControl({installed->spec.curseAi,installed->spec.source.entity,program.ai==CurseAi::Terror?monster.movementTarget:EntityId{},UINT64_MAX,program.fearOrigin,installed->spec.state.id},
                        recipient.states.controlResetEpoch());
                }
            });
        if(!applied.accepted) {if(program.ai==CurseAi::Taunt) resetTaunt();continue;}
        // Confuse enters the neutral alignment after the curse stat writes.
        if(resources && program.ai==CurseAi::Confuse)
            resources->writeBaseStat("alignment",1,combatEffectStatSources(recipient.states.entries(),actor.tick,true));
        auto life=resources?resources->take():monsters::LifeChange{id,monster.revision,Fixed8::fromRaw(monster.life),Fixed8::fromRaw(monster.maximumLife),monster.nativeStats,false};
        if(controlAi!=CurseAi::None && !targetControl) {life.reselectAi=true;life.changed=true;}
        if(program.ai==CurseAi::Confuse) {life.targetCallbackEpoch=recipient.states.targetResetEpoch();life.specialTargetNode=true;life.changed=true;}
        const auto *installed=recipient.states.find(applied.handle);
        if(!installed) throw std::runtime_error("Missing installed native unit curse");
        // DimVision/Terror/Taunt are released by their list callback. Confuse
        // separately schedules AIRESET from the pre-resistance duration.
        if(program.ai==CurseAi::Confuse) {
            const auto frame=nativeAdd(nativeNarrow(uint32_t(actor.tick)),(divisor?nativeDivide(program.frames,divisor):program.frames));
            const auto deadline=nativeEventDeadline(actor.tick,frame);
            life.aiResetTimers=monster.aiResetTimers;life.aiResetTimers->push_back({deadline.due,deadline.continuous,ports_.frameEvents.reserveIdentity()});
        }
        if(targetControl || (!resources && controlAi!=CurseAi::None)) {
            life.curseControl=monsters::CurseControl{installed->spec.curseAi,installed->spec.source.entity,program.ai==CurseAi::Terror?monster.movementTarget:EntityId{},UINT64_MAX,program.fearOrigin,installed->spec.state.id};
            life.controlCallbackEpoch=recipient.states.controlResetEpoch();life.changed=true;
        }
        if(program.ai==CurseAi::Taunt) {life.tauntPostActions=true;life.tauntTarget=actor.actor;life.aiThinkAt=actor.tick+1;life.changed=true;}
        if(skill.missileNextDelay>0) {life.hitDelay=actor.tick+uint64_t(skill.missileNextDelay);life.changed=true;}
        lifeChanges.push_back(std::move(life));
        for(const auto &removed:applied.removed) if(!recipient.states.hasState(removed.effect.spec.state.id,actor.tick)) {
            recipient.nativeStats.erase(removed.effect.spec.state.id);
            publication->facts.emplace_back(StateFact{id,1,monster.area,removed.effect.spec.state.id,false});
        }
        auto &stats=recipient.nativeStats[program.state.id];stats.clear();
        for(const auto &effect:recipient.states.entries()) if(effect.activeAt(actor.tick) && effect.spec.state.id==program.state.id)
            stats=nativeEffectStats(*player->rules.skills,effect.spec);
        publication->facts.emplace_back(StateFact{id,1,monster.area,program.state.id,true,stats});
    }
    for(const auto &change:lifeChanges) if(!ports_.monsters.canCommitLife(change)) return {DomainStatus::Conflict,{}};
    changesToPrepare.emplace_back(std::move(debit));
    for(auto &life:lifeChanges) {
        const auto id=life.actor;prepareUnitEffects(*publication,id,next.at(id),std::move(life),actor.tick);
    }
    auto prepared=ports_.transactions.prepareGroup(std::move(changesToPrepare),std::move(publication));if(!prepared) return {prepared.status,{}};
    auto plans=std::move(*prepared.value);
    const auto result=plans.size()==1?ports_.transactions.commit(std::move(plans.front())):ports_.transactions.commitCharacters(std::move(plans));
    if(result) publishPlayers(nextPlayers);
    return result;
}
}
