#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/units/resources.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/event_frame.hpp"
#include "server/systems/skills/native_caster.hpp"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <memory>
#include <utility>
namespace d2x::server::effects {
void prepareUnitControl(const monsters::Actor &actor,const CombatEffectSet &before,const CombatEffectSet &after,monsters::LifeChange &life,uint64_t tick) {
    const auto observed=life.controlCallbackEpoch.value_or(before.controlResetEpoch());
    if(observed!=after.controlResetEpoch()) {life.curseControl=monsters::CurseControl{};life.reselectAi=true;life.changed=true;}
    life.controlCallbackEpoch=after.controlResetEpoch();
    const auto targetObserved=life.targetCallbackEpoch.value_or(before.targetResetEpoch());
    // sub_6FCBDD30 refuses EVIL for UNITFLAG_ISREVIVE, except Hydra.
    if(targetObserved!=after.targetResetEpoch() && (!actor.revived() || actor.hydra()) && life.intrinsic && life.intrinsic->sources && actor.rule.stats) {
        auto shadow=actor;shadow.nativeStats=life.intrinsic;shadow.life=life.life.raw();shadow.maximumLife=life.maximum.raw();
        MonsterResourceTrace resources(shadow,tick,&before);
        const auto sources=combatEffectStatSources(after.entries(),tick,true);
        resources.writeBaseStat("alignment",0,sources);
        life.intrinsic=resources.read().intrinsic;life.life=resources.read().life;life.maximum=resources.read().maximum;life.changed=true;
    }
    if(targetObserved!=after.targetResetEpoch()) {life.specialTargetNode=false;life.changed=true;}
    life.targetCallbackEpoch=after.targetResetEpoch();
    const auto deadline=[&](int state)->std::optional<uint64_t> {
        for(const auto &entry:after.entries()) if(entry.spec.nativeState && entry.spec.state.id==state)
            return entry.expiresAt;
        return {};
    };
    const auto project=[&](int state,uint64_t current,std::optional<uint64_t> &result) {
        if(const auto until=deadline(state)) {if(*until!=current) {result=*until;life.changed=true;}}
        else if(current && before.hasState(state,tick) && !after.hasState(state,tick)) {result=0;life.changed=true;}
    };
    if(actor.alive()) {
        project(actor.rule.coldState,actor.chilledUntil,life.chilledUntil);
        project(actor.rule.frozenState.id,actor.frozenUntil,life.frozenUntil);
        project(actor.rule.stunState,actor.stunnedUntil,life.stunnedUntil);
    }
    if(actor.alive() && before.hasState(actor.rule.coldState,tick) && !after.hasState(actor.rule.coldState,tick)) {
        life.shatter=false;life.changed=true;
    }
    if(actor.alive() && before.hasState(actor.rule.frozenState.id,tick) && !after.hasState(actor.rule.frozenState.id,tick)) {
        life.restoreNeutral=true;life.aiThinkAt=tick+uint64_t(actor.rule.neutralDelayTicks);life.changed=true;
    }
    const auto poisonState=actor.rule.hitStates.poison.id;
    const ActiveCombatEffect *poisonList=nullptr;
    for(const auto &entry:after.entries()) if(entry.spec.nativeState && entry.spec.state.id==poisonState) {poisonList=&entry;break;}
    if(poisonList && poisonList->spec.nativeValues && actor.rule.stats) {
        const auto key=StatKey{actor.rule.stats->names.at("hpregen"),0};
        const auto value=poisonList->spec.nativeValues->find(key);
        const auto rate=value==poisonList->spec.nativeValues->end()?0:nativeSubtract(0,value->second);
        const auto until=poisonList->expiresAt.value_or(UINT64_MAX);const auto source=poisonList->spec.source.entity;
        if(!actor.poison || actor.poison->damage.rate!=rate || actor.poison->until!=until || actor.poison->damage.actualSource!=source || actor.poison->nativeExpireFrame!=poisonList->nativeExpireFrame) {
            auto poison=actor.poison.value_or(PoisonStatus{});poison.damage.rate=rate;poison.damage.state=poisonState;
            poison.damage.actualSource=source;poison.damage.frames=until>tick?until-tick:0;
            poison.source=source;poison.until=until;poison.nativeExpireFrame=poisonList->nativeExpireFrame;
            if(poison.nativeExpireFrame) poison.removalDue=nativeEventDeadline(tick,*poison.nativeExpireFrame).due;
            poison.next=tick+1;life.poison=std::optional{std::move(poison)};life.changed=true;
        }
    } else if(actor.poison) {life.poison=std::optional<PoisonStatus>{};life.changed=true;}
    if(actor.wound && !after.hasState(actor.rule.openWoundsState,tick)) {life.wound=std::optional<OpenWoundsApplication>{};life.changed=true;}
}
void retainPreparedUnitControl(monsters::LifeChange &next,const monsters::LifeChange &previous) {
    if(!next.curseControl) {next.curseControl=previous.curseControl;next.controlCallbackEpoch=previous.controlCallbackEpoch;}
    next.stopMovement|=previous.stopMovement;
    if(previous.hitDelay) next.hitDelay=std::max(next.hitDelay.value_or(0),*previous.hitDelay);
    if(!next.attraction) next.attraction=previous.attraction;
    if(!next.aiResetTimers) next.aiResetTimers=previous.aiResetTimers;
    if(!next.targetCallbackEpoch) next.targetCallbackEpoch=previous.targetCallbackEpoch;
    if(!next.specialTargetNode) next.specialTargetNode=previous.specialTargetNode;
    if(!next.detachedMinion) next.detachedMinion=previous.detachedMinion;
    if(!next.nativeMinions) next.nativeMinions=previous.nativeMinions;
    next.tauntPostActions|=previous.tauntPostActions;next.reselectAi|=previous.reselectAi;
    if(!next.chilledUntil) next.chilledUntil=previous.chilledUntil;
    if(!next.frozenUntil) next.frozenUntil=previous.frozenUntil;
    if(!next.stunnedUntil) next.stunnedUntil=previous.stunnedUntil;
    next.restoreNeutral|=previous.restoreNeutral;
    if(!next.wound) next.wound=previous.wound;
    if(!next.tauntTarget) next.tauntTarget=previous.tauntTarget;
    if(!next.aiThinkAt) next.aiThinkAt=previous.aiThinkAt;
    next.changed|=previous.changed;
}
int System::unitCurseResistance(EntityId id,uint64_t tick) const {
    if(const auto *player=ports_.players.findActor(id)) {
        const auto value=player->totals.character.combat.curseResistance;
        return player->rules.skills && player->rules.skills->pd2?std::min(value,75):value;
    }
    const auto *unit=ports_.monsters.find(id);if(!unit) return 0;
    if(const auto values=unitStatValues(id,tick)) return logicalStatValue(*values,*unit->rule.stats,"curse_resistance");
    return nativeAdd(unit->petStats.attributes.combat.curseResistance,unitModifiers(id,tick).combat.curseResistance);
}
std::set<int> System::unitStates(EntityId id,uint64_t tick) const {
    std::set<int> states;
    if(const auto *player=ports_.players.findActor(id)) states=player->transient.states;
    if(const auto *effects=unitEffects(id)) {
        const auto native=effects->stateIds(tick);states.insert(native.begin(),native.end());
    }
    const auto *m=ports_.monsters.find(id);if(!m) return states;
    const auto add=[&](int state){if(state>=0) states.insert(state);};
    states.insert(m->aiStates.begin(),m->aiStates.end());
    states.insert(m->corpse.states.begin(),m->corpse.states.end());
    if(m->corpse.searched) add(m->rule.corpseNoSelectState);
    if(m->amazonPet) add(m->amazonPet->state.id);
    if(m->revived()) add(m->necroPet->reviveState.id);
    if(m->alive() && m->shatterActive) add(m->rule.shatterState.id);
    if(m->skillMotion && m->skillMotion->moveState) add(m->rule.skillMoveState);
    if(m->rule.alignmentStat>=0) add(m->rule.alignmentState);
    if(m->conversion) {add(m->conversion->state);add(m->conversion->alignment);}
    return states;
}
DomainResult<> System::convert(const ActorContext &actor,EntityId id,const WeaponSkillSpec &skill) {
    return convert(actor,id,NativeConversionSpec{skill.conversionState,skill.conversionSaveState,skill.conversionFrames,NativeConversionProcedure::Paladin});
}
DomainResult<> System::convert(const ActorContext &actor,EntityId id,const NativeConversionSpec &skill) {
    const auto *monster=ports_.monsters.find(id);
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!monster || !caster || !caster->programs()) return {DomainStatus::InvalidActor,{}};
    if(!monster->nativeStats || !monster->nativeStats->sources || !monster->rule.stats || skill.saveState.id<0)
        return {DomainStatus::Unavailable,{}};
    auto next=units_;auto &unit=next[id];unit.area=monster->area;
    MonsterResourceTrace resources(*monster,actor.tick,&unit.states);
    const auto sources=[&]{return combatEffectStatSources(unit.states.entries(),actor.tick,true);};
    const auto observe=[&] {resources.sample(unit.states,true);};
    resources.writeBaseStat("alignment",2,sources());
    std::vector<DomainFact> facts;std::vector<EffectHandle> removed;
    for(const auto &effect:unit.states.entries())
        if(effect.spec.state.aura) removed.push_back(effect.handle);
    for(const auto handle:removed) for(const auto &effect:unit.states.remove(handle,observe)) {
        const int state=effect.effect.spec.state.id;unit.nativeStats.erase(state);
        if(!unit.states.hasState(state,actor.tick)) facts.emplace_back(StateFact{id,1,actor.area,state,false});
    }
    resources.sample(unit.states);
    const auto sampled=resources.values(combatEffectStatSources(unit.states.entries(),actor.tick));
    const int targetLevel=nativeStatValue(sampled,*monster->rule.stats,"level");
    const int casterLevel=caster->level();
    std::optional<ConversionResourceSave> saved;
    if(const auto change=conversionResources(resources.read().life,resources.read().maximum,targetLevel,casterLevel)) {
        saved=change->saved;
        auto source=std::make_shared<StatList>();source->extended=false;source->ownerKind=StatOwnerKind::Monster;
        source->source={StatSourceKind::Effect,id,skill.saveState.id,0};
        setNativeStat(*source,*monster->rule.stats,"conversion_level",saved->level,0,true);
        setNativeStat(*source,*monster->rule.stats,"conversion_maxhp",saved->maximumWhole,0,true);
        resources.postIntrinsicSource(std::move(source),sources());
        resources.writeBaseStat("level",casterLevel,sources());
        resources.seedLife(change->life);
        resources.writeBaseStat("maxhp",change->maximum.raw(),sources());
    }
    auto timers=ports_.frameEvents.prepareEdit({FrameEventUnit::Monster,id},FrameEventType::RemoveState,unit.states.expirationEvents());
    const auto result=ports_.monsters.convert(id,actor,skill,resources.take(),saved,std::move(facts));if(!result) return result;
    ports_.frameEvents.publish(std::move(timers));unit.converted=true;units_.swap(next);
    ports_.monsters.velocityModifier(id,units_.at(id).states.modifiers(actor.tick).velocityPercent);
    return result;
}
DomainResult<> System::endConversion(EntityId id,uint64_t tick) {
    const auto *monster=ports_.monsters.find(id);
    if(!monster || !monster->conversion) return {DomainStatus::InvalidActor,{}};
    if(!monster->nativeStats || !monster->nativeStats->sources || !monster->rule.stats) return {DomainStatus::Unavailable,{}};
    const auto conversion=*monster->conversion;
    auto next=units_;auto &unit=next[id];unit.area=monster->area;
    MonsterResourceTrace resources(*monster,tick,&unit.states);
    const auto sources=[&]{return combatEffectStatSources(unit.states.entries(),tick,true);};
    resources.writeBaseStat("alignment",0,sources());
    std::vector<DomainFact> facts;
    if(conversion.savedResources) {
        const auto &saved=*conversion.savedResources;
        const auto life=conversion.procedure==NativeConversionProcedure::MindBlast?
            mindBlastRestoredLife(saved,resources.read().life,resources.read().maximum):conversionRestoredLife(saved,resources.read().life,resources.read().maximum);
        resources.writeBaseStat("level",saved.level,sources());
        resources.seedLife(life);
        // Retail 1.13c Paladin callback RVA 98A1A writes the saved whole
        // MAXHP directly. Mind Blast's other callback shifts it; do not merge.
        resources.writeBaseStat("maxhp",conversion.procedure==NativeConversionProcedure::MindBlast?nativeShiftLeft(saved.maximumWhole,8):saved.maximumWhole,sources());
        resources.withdrawIntrinsicSource(StatSourceKind::Effect,conversion.savedState,sources());
    }
    if(conversion.savedResources || conversion.procedure==NativeConversionProcedure::MindBlast) {
        std::vector<EffectHandle> handles;
        for(const auto &effect:unit.states.entries()) if(effect.spec.state.aura) handles.push_back(effect.handle);
        for(const auto handle:handles) for(const auto &removed:unit.states.remove(handle,[&]{resources.sample(unit.states,true);})) {
            const int state=removed.effect.spec.state.id;unit.nativeStats.erase(state);
            if(!unit.states.hasState(state,tick)) facts.emplace_back(StateFact{id,1,monster->area,state,false});
        }
    }
    resources.sample(unit.states);
    auto timers=ports_.frameEvents.prepareEdit({FrameEventUnit::Monster,id},FrameEventType::RemoveState,unit.states.expirationEvents());
    const auto result=ports_.monsters.finishConversion(id,tick,resources.take(),std::move(facts));if(!result) return result;
    ports_.frameEvents.publish(std::move(timers));unit.converted=false;units_.swap(next);
    ports_.monsters.velocityModifier(id,units_.at(id).states.modifiers(tick).velocityPercent);
    return result;
}
DomainResult<> System::skillUnit(const ActorContext &actor,const SkillCastSpec &skill,EntityId id) {
    const auto *caster=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!caster || !caster->entered || caster->actor!=actor.actor || caster->area!=actor.area || caster->persistent.player.hp<=0 ||
       !area || area->generation!=actor.areaGeneration || (skill.effect!=SkillBehavior::Enchant && skill.effect!=SkillBehavior::AreaEnchant && !skill.warcry) || !skill.appliedEffect ||
       caster->persistent.player.mana<skill.manaCost) return {DomainStatus::InvalidActor,{}};
    const auto *m=ports_.monsters.find(id);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const bool allied=m && m->alive() && ports_.areas.at(m->area).definition.act==area->definition.act && participants.auraAligned({caster,nullptr},{nullptr,m});
    if((skill.effect==SkillBehavior::Enchant || skill.effect==SkillBehavior::AreaEnchant) && !allied && m && m->alive() && ports_.areas.at(m->area).definition.act==area->definition.act) return this->skill(actor,skill);
    if(!allied) return {DomainStatus::InvalidRequest,{}};
    // Enchant uses sub_6FD10EC0; BasicShout allocates its own list after
    // its aura filter. Those procedures do not share the NPC posting gate.
    if(!m->rule.states || (!skill.warcry && !m->rule.stateTarget.acceptsCurseList())) return {DomainStatus::Unavailable,{}};
    if(!m || !m->nativeStats || !m->nativeStats->sources || !m->rule.stats)
        return {DomainStatus::Unavailable,{}};
    auto next=units_;auto &target=next[id];target.area=m->area;
    auto spec=*skill.appliedEffect;spec.source={CombatEffectSource::Skill,caster->actor,skill.sourceId,skill.rank};
    const auto previousStats=target.nativeStats[spec.state.id];
    std::optional<MonsterResourceTrace> resources;
    if(m && m->nativeStats && m->nativeStats->sources && m->rule.stats) resources.emplace(*m,actor.tick,&target.states);
    const auto observe=[&] {if(resources) resources->sample(target.states,true);};
    const auto application=applySkillState(target.states,skill,spec,*caster->rules.skills->stats,*m->rule.states,actor.tick,observe);
    if(!application.accepted) return {DomainStatus::Conflict,{}};
    const auto *installed=target.states.find(application.handle);
    if(!installed) throw std::runtime_error("Missing installed native unit skill state");
    target.nativeStats[spec.state.id]=nativeEffectStats(*caster->rules.skills,installed->spec);
    const bool changedStats=previousStats!=target.nativeStats[spec.state.id];
    transactions::CharacterEdit debit{actor,caster->inventoryRevision,caster->characterRevision,caster->persistent.player};
    debit.player.mana-=skill.manaCost;debit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};debit.charge=skill.charge;
    for(const int state:application.clearedStates) if(!target.states.hasState(state,actor.tick))
        debit.publicFacts.emplace_back(StateFact{id,1,m->area,state,false});
    for(const auto &removed:application.removed) if(!target.states.hasState(removed.effect.spec.state.id,actor.tick))
        debit.publicFacts.emplace_back(StateFact{id,1,m->area,removed.effect.spec.state.id,false});
    const auto before=unitStates(id,actor.tick);
    for(const auto &effect:target.states.entries()) if(effect.activeAt(actor.tick) && (!before.contains(effect.spec.state.id) || ((skill.warcry || changedStats) && effect.spec.state.id==skill.appliedEffect->state.id)))
        debit.publicFacts.emplace_back(StateFact{id,1,m->area,effect.spec.state.id,true,target.nativeStats[effect.spec.state.id]});
    if(resources && !ports_.monsters.canCommitLife(resources->read())) return {DomainStatus::Conflict,{}};
    auto units=std::make_shared<AuraUnitPublication>();
    std::optional<monsters::LifeChange> life;
    if(resources) life=resources->take();
    else if(m) life=monsters::LifeChange{id,m->revision,Fixed8::fromRaw(m->life),Fixed8::fromRaw(m->maximumLife),m->nativeStats,false};
    prepareUnitEffects(*units,id,target,std::move(life),actor.tick);
    auto plan=ports_.transactions.prepare(std::move(debit),nullptr,{},std::move(units));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));
    return result;
}
DomainResult<> System::unitSkill(EntityId id,const SkillCastSpec &skill,uint64_t tick,std::optional<std::pair<uint64_t,uint64_t>> random) {
    const auto *m=ports_.monsters.find(id);
    if(!m || !m->alive() || !skill.appliedEffect) return {DomainStatus::InvalidActor,{}};
    if(!m->rule.stats || !m->rule.states || !m->nativeStats || !m->nativeStats->sources ||
       (!skill.warcry && !skill.appliedEffect->nativeDirectAllocation && !m->rule.stateTarget.acceptsCurseList())) return {DomainStatus::Unavailable,{}};
    UnitEffect unit;if(const auto current=units_.find(id);current!=units_.end()) unit=current->second;unit.area=m->area;
    auto spec=*skill.appliedEffect;spec.source={CombatEffectSource::Skill,id,skill.sourceId,skill.rank};
    MonsterResourceTrace resources(*m,tick,&unit.states);
    const auto observe=[&]{resources.sample(unit.states,true);};
    const auto application=applySkillState(unit.states,skill,spec,*m->rule.stats,*m->rule.states,tick,observe);
    if(!application.accepted) return {DomainStatus::Conflict,{}};
    const auto *installed=unit.states.find(application.handle);
    if(!installed) throw std::logic_error("Missing native unit skill state");
    auto stats=nativeEffectStats(*m->rule.stats,installed->spec);
    AuraUnitPublication publication;
    for(const int state:application.clearedStates) if(!unit.states.hasState(state,tick)) publication.facts.emplace_back(StateFact{id,1,m->area,state,false});
    for(const auto &removed:application.removed) if(removed.effect.spec.state.id>=0 && !unit.states.hasState(removed.effect.spec.state.id,tick))
        publication.facts.emplace_back(StateFact{id,1,m->area,removed.effect.spec.state.id,false});
    if(spec.state.id>=0) publication.facts.emplace_back(StateFact{id,1,m->area,spec.state.id,true,stats});
    if(skill.castOverlayId>=0) publication.facts.emplace_back(OverlayFact{id,1,m->area,skill.castOverlayId});
    if(spec.state.id>=0) unit.nativeStats[spec.state.id]=std::move(stats);
    resources.sample(unit.states);
    auto life=resources.take();
    if(random) {life.random=*random;life.changed=true;}
    prepareUnitEffects(publication,id,unit,std::move(life),tick);
    if(!validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},m->area},std::move(publication.facts)});
    if(!sent) return {sent.status,{}};
    publishAuraUnits(publication);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::postModeRate(EntityId id,int skill,int rank,int32_t rate,uint64_t revision,uint64_t tick,std::pair<uint64_t,uint64_t> random) {
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};const auto source=participants.find(id);
    if(!source.id()) return {DomainStatus::InvalidActor,{}};
    const auto &rules=source.player?*source.player->rules.skills->stats:*source.monster->rule.stats;
    CombatEffectSpec spec;spec.source={CombatEffectSource::Skill,id,skill,rank};spec.stacking=EffectStacking::Independent;
    spec.nativeDirectAllocation=true;spec.nativeValues.emplace();spec.modeActionRevision=revision;
    const std::array<StatValue,1> writes{{{rules.names.at("attackrate"),rate}}};
    if(source.player) {
        const auto &player=*source.player;const auto &area=ports_.areas.at(player.area);
        auto next=state_;auto &states=next.players[id].states;
        PlayerResourceTrace resources(player,tick,{},ports_.areas.statTime(player.area));
        const auto observe=[&]{resources.sample(states,true);};
        const auto posted=states.apply(spec,tick,observe);
        if(!states.writeNativeStats(posted.handle,writes,rules,spec,observe)) throw std::logic_error("Missing anonymous mode rate list");
        transactions::CharacterEdit edit{{player.player,id,player.area,area.generation,0,tick},player.inventoryRevision,player.characterRevision,resources.record()};
        edit.resourceBase=resources.maximum();edit.petLimitChanges=resources.petLimits();edit.transient=projectEffectAttributes(states,tick);
        edit.random=transactions::UnitRandomEdit{random.first,random.second};
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto committed=ports_.transactions.commit(std::move(*plan.value));if(committed) publishPlayers(next.players);
        return committed;
    }
    const auto &body=*source.monster;UnitEffect unit;if(const auto current=units_.find(id);current!=units_.end()) unit=current->second;
    unit.area=body.area;MonsterResourceTrace resources(body,tick,&unit.states);const auto observe=[&]{resources.sample(unit.states,true);};
    const auto posted=unit.states.apply(spec,tick,observe);
    if(!unit.states.writeNativeStats(posted.handle,writes,rules,spec,observe)) throw std::logic_error("Missing anonymous monster mode rate list");
    resources.sample(unit.states);auto life=resources.take();life.random=random;life.changed=true;
    AuraUnitPublication publication;prepareUnitEffects(publication,id,unit,std::move(life),tick);
    if(!validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
    publishAuraUnits(publication);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::endMode(EntityId id,uint64_t tick,std::optional<uint64_t> activeRevision) {
    const auto *effects=unitEffects(id);
    if(!effects || !effects->hasEndedModeEffects(activeRevision)) return {DomainStatus::Applied,std::monostate{}};
    if(const auto *player=ports_.players.findActor(id)) {
        auto next=state_;auto &states=next.players[id].states;
        PlayerResourceTrace resources(*player,tick,{},ports_.areas.statTime(player->area));
        const auto removed=states.endModeEffects(activeRevision,[&]{resources.sample(states,true);});
        transactions::CharacterEdit edit{{player->player,id,player->area,ports_.areas.at(player->area).generation,0,tick},
            player->inventoryRevision,player->characterRevision,resources.record()};
        edit.resourceBase=resources.maximum();edit.petLimitChanges=resources.petLimits();edit.transient=projectEffectAttributes(states,tick);
        for(const auto &entry:removed) if(entry.effect.spec.state.id>=0 && !states.hasState(entry.effect.spec.state.id,tick))
            edit.publicFacts.emplace_back(StateFact{id,0,player->area,entry.effect.spec.state.id,false});
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto committed=ports_.transactions.commit(std::move(*plan.value));if(committed) publishPlayers(next.players);
        return committed;
    }
    const auto *body=ports_.monsters.find(id);if(!body) return {DomainStatus::Applied,std::monostate{}};
    auto unit=units_.at(id);MonsterResourceTrace resources(*body,tick,&unit.states);
    const auto removed=unit.states.endModeEffects(activeRevision,[&]{resources.sample(unit.states,true);});
    AuraUnitPublication publication;
    for(const auto &entry:removed) if(entry.effect.spec.state.id>=0 && !unit.states.hasState(entry.effect.spec.state.id,tick))
        publication.facts.emplace_back(StateFact{id,1,body->area,entry.effect.spec.state.id,false});
    resources.sample(unit.states);prepareUnitEffects(publication,id,unit,resources.take(),tick);
    if(!validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
    if(!publication.facts.empty()) {const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},body->area},std::move(publication.facts)});if(!sent) return {sent.status,{}};}
    publishAuraUnits(publication);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::removeUnitState(EntityId id,int state,uint64_t tick,size_t limit) {
    const auto *body=ports_.monsters.find(id);if(!body) return {DomainStatus::InvalidActor,{}};
    const auto found=units_.find(id);if(found==units_.end()) return {DomainStatus::Applied,std::monostate{}};
    auto unit=found->second;MonsterResourceTrace resources(*body,tick,&unit.states);
    std::vector<RemovedCombatEffect> removed;
    const auto observe=[&]{resources.sample(unit.states,true);};
    if(limit==SIZE_MAX) removed=unit.states.removeState(state,observe);
    else for(size_t index=0;index<limit;++index) {
        const auto entries=unit.states.entries();
        const auto found=std::find_if(entries.rbegin(),entries.rend(),[&](const auto &entry){return entry.spec.state.id==state;});
        if(found==entries.rend()) break;
        const auto values=unit.states.remove(found->handle,observe);
        removed.insert(removed.end(),values.begin(),values.end());
    }
    AuraUnitPublication publication;
    for(const auto &entry:removed) if(!unit.states.hasState(entry.effect.spec.state.id,tick))
        publication.facts.emplace_back(StateFact{id,1,body->area,entry.effect.spec.state.id,false});
    resources.sample(unit.states);prepareUnitEffects(publication,id,unit,resources.take(),tick);
    if(!validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
    if(!publication.facts.empty()) {
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},body->area},std::move(publication.facts)});
        if(!sent) return {sent.status,{}};
    }
    publishAuraUnits(publication);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::prepareUnitCurseRemoval(AuraUnitPublication &publication,EntityId id,uint64_t tick) const {
    const auto *body=ports_.monsters.find(id);
    if(!body || !body->rule.states) return {DomainStatus::InvalidActor,{}};
    const auto found=units_.find(id);
    if(found==units_.end()) return {DomainStatus::Applied,std::monostate{}};
    auto unit=found->second;
    MonsterResourceTrace resources(*body,tick,&unit.states);
    bool changed=false;
    for(const auto &[state,definition]:*body->rule.states) {
        if(!definition.curse) continue;
        const auto removed=unit.states.removeState(state,[&]{resources.sample(unit.states,true);});
        changed|=!removed.empty();
        for(const auto &entry:removed) if(!unit.states.hasState(entry.effect.spec.state.id,tick))
            publication.facts.emplace_back(StateFact{id,1,body->area,entry.effect.spec.state.id,false});
    }
    if(changed) {
        resources.sample(unit.states);
        prepareUnitEffects(publication,id,unit,resources.take(),tick);
    }
    return {DomainStatus::Applied,std::monostate{}};
}
void System::collectEntityReferences(std::set<EntityId> &entities) const {
    const auto retain=[&](EntityId id) {if(id) entities.insert(id);};
    const auto timerSources=[&](FrameEventOwner owner) {
        for(const auto &event:ports_.frameEvents.events(owner)) {
            if(event.handler) continue;
            if(event.type==FrameEventType::PeriodicStats ||
                (event.type==FrameEventType::Pd2MissileHitDelay && event.customId!=-1))
                retain(EntityId{uint32_t(event.customId)});
        }
    };
    const auto damage=[&](const auto &unit) {
        if(unit.poison) {retain(unit.poison->source);retain(unit.poison->damage.actualSource);}
        if(unit.wound) {
            retain(unit.wound->source);retain(unit.wound->credit);
            retain(unit.wound->pd2Attacker);retain(unit.wound->pd2Credit);
        }
    };
    for(const auto &[id,recovery]:state_.players) {
        retain(id);recovery.states.collectEntityReferences(entities);
        damage(recovery);retain(recovery.killer);
    }
    for(const auto &[id,unit]:units_) {retain(id);unit.states.collectEntityReferences(entities);}
    for(const auto &[id,player]:ports_.players.all()) {
        (void)id;
        for(const auto &timer:player.totals.itemAuraTimers) retain(timer.source);
        timerSources({FrameEventUnit::Player,player.actor});
    }
    for(const auto &[id,unit]:ports_.monsters.read().actors) {
        (void)id;damage(unit);
        retain(unit.curseControl.caster);retain(unit.curseControl.target);
        for(const auto &timer:unit.itemAuraTimers) retain(timer.source);
        timerSources({FrameEventUnit::Monster,id});
    }
    for(const auto &[key,cycle]:itemAuraCycles_) {
        retain(key.first);retain(key.second);
        if(cycle.itemTimer) retain(cycle.itemTimer->source);
        if(cycle.itemReplacement) retain(cycle.itemReplacement->source);
    }
    collectPd2SacrificeReferences(entities);
}
bool System::retirementPending(EntityId id) const {
    if(std::any_of(nativeStateRemovals_.begin(),nativeStateRemovals_.end(),[&](const auto &entry){return entry.unit==id;})) return true;
    for(const auto &[source,curse]:monsterCurses_) {
        if(source==id) return true;
        if(std::find(curse.targets.begin()+curse.next,curse.targets.end(),id)!=curse.targets.end()) return true;
    }
    for(const auto &[handle,execution]:modifierExecutions_) {(void)handle;if(execution.owner==id) return true;}
    if(const auto found=uniqueCycles_.find(id);found!=uniqueCycles_.end() &&
        (found->second.emissionPending || found->second.deathExecution)) return true;
    const auto includes=[&](const auto &entry) {
        const auto &cycle=entry.second;
        return cycle.pending && (entry.first==id || std::find(cycle.targets.begin(),cycle.targets.end(),id)!=cycle.targets.end());
    };
    if(std::any_of(unitAuraCycles_.begin(),unitAuraCycles_.end(),includes) ||
        std::any_of(paladinCycles_.begin(),paladinCycles_.end(),includes)) return true;
    for(const auto &[key,cycle]:itemAuraCycles_) if(cycle.pending &&
        (key.first==id || std::find(cycle.targets.begin(),cycle.targets.end(),id)!=cycle.targets.end())) return true;
    return false;
}
void System::forgetUnit(EntityId id) noexcept {
    units_.erase(id);unitAuraCycles_.erase(id);uniqueCycles_.erase(id);movementSkillPhases_.erase(id);
    std::erase_if(modifierExecutions_,[&](const auto &entry){return entry.second.owner==id;});
    std::erase_if(itemAuraCycles_,[&](const auto &entry){return entry.first.first==id;});
}
StepStatus System::advanceUnits(uint64_t tick) {
    bool blocked=false;
    for(auto it=units_.begin();it!=units_.end();) {
        const auto *m=ports_.monsters.find(it->first);
        if(!m) {const auto id=it->first;++it;ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,id});forgetUnit(id);continue;}
        if(m && m->area!=it->second.area) it->second.area=m->area;
        // Native state writes and RemoveState callbacks already publish their
        // FullStats/control/velocity changes synchronously. An unchanged live
        // list has no expiry work and must not be reposted on every frame.
        if(m->alive() && m->nativeStats && m->nativeStats->sources && m->nativeStats->sources->fullValues && m->rule.stats &&
           !it->second.states.hasDueExpiration(tick)) {
            if(!m->conversion) it->second.converted=false;
            if(!it->second.states.size() && !it->second.states.hasUnitEvents()) it=units_.erase(it);
            else ++it;
            continue;
        }
        auto next=it->second.states;
        std::optional<MonsterResourceTrace> resources;
        if(m && m->nativeStats && m->nativeStats->sources && m->rule.stats) resources.emplace(*m,tick,&it->second.states);
        const auto observe=[&] {if(resources) resources->sample(next,true);};
        std::vector<RemovedCombatEffect> removed;
        std::vector<int> clearedStates;
        if(m && !m->alive()) {
            const auto kind=m->rule.boss?EffectUnitKind::Boss:EffectUnitKind::Monster;
            removed=next.onDeath(kind,observe);
            if(!m->rule.states) throw std::runtime_error("Missing native unit death masks");
            clearedStates=next.clearNonDeathStates(kind,*m->rule.states,tick,observe);
            auto expired=next.expireOrdinary(tick,observe,kind);removed.insert(removed.end(),expired.begin(),expired.end());
        } else {
            auto expired=next.expireOrdinary(tick,observe);removed.insert(removed.end(),expired.begin(),expired.end());
        }
        if(resources) {
            if(m && !m->alive() && !m->itemAuraTimers.empty()) {
                resources->seedItemAuraTimers({});
            }
            if(m && m->alive() && std::any_of(removed.begin(),removed.end(),[](const auto &effect) {
                return effect.effect.spec.nativeRemoval==NativeStateRemoval::HolyFreeze;
            })) resources->seedShatter(false);
            resources->sample(next);
        }
        std::optional<monsters::LifeChange> life;
        if(resources) life=resources->take();
        else if(m) life=monsters::LifeChange{m->id,m->revision,Fixed8::fromRaw(m->life),Fixed8::fromRaw(m->maximumLife),m->nativeStats,false};
        if(life) {
            prepareUnitControl(*m,it->second.states,next,*life,tick);
            if(life->itemAuraTimers) life->periodicEvents=prepareItemAuraEvents({FrameEventUnit::Monster,m->id},*life->itemAuraTimers);
            if(!ports_.monsters.canCommitLife(*life)) {blocked=true;++it;continue;}
        }
        std::vector<DomainFact> facts;
        for(const int state:clearedStates) if(!m->rule.states->at(state).noSend &&
            std::none_of(removed.begin(),removed.end(),[&](const auto &effect){return effect.effect.spec.state.id==state;}))
            facts.emplace_back(StateFact{it->first,1,it->second.area,state,false});
        for(const auto &e:removed) if(e.effect.spec.state.id>=0 && !next.hasState(e.effect.spec.state.id,tick)) facts.emplace_back(StateFact{it->first,1,it->second.area,e.effect.spec.state.id,false});
        auto timers=ports_.frameEvents.prepareEdit({FrameEventUnit::Monster,it->first},FrameEventType::RemoveState,next.expirationEvents());
        if(!facts.empty()) {
            const auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},it->second.area},std::move(facts)});
            if(!result) {blocked=true;++it;continue;}
        }
        ports_.frameEvents.publish(std::move(timers));std::swap(it->second.states,next);
        if(life && !ports_.monsters.commitLife(std::move(*life))) throw std::logic_error("Native unit expiry/control commit lost its reservation");
        if(m && !m->conversion) it->second.converted=false;
        if(m) ports_.monsters.velocityModifier(it->first,it->second.states.modifiers(tick).velocityPercent);
        if(m) {
            auto &effect=it->second;
            if(resources) {
                effect.baseLife=nativeStatValue(m->nativeStats->values,*m->rule.stats,"maxhp");
                effect.adjustedLife=m->maximumLife;
            } else {
                if(m->hireling) effect.baseLife=int64_t(m->rule.minimumLife)*256;
                else if(!effect.baseLife || m->maximumLife!=effect.adjustedLife) effect.baseLife=m->maximumLife;
                effect.adjustedLife=effect.baseLife*std::max(0,100+effect.states.modifiers(tick).combat.lifePercent)/100;
                ports_.monsters.maximumLife(it->first,effect.adjustedLife);
            }
        }
        if(!it->second.states.size() && !it->second.states.hasUnitEvents()) it=units_.erase(it);else ++it;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
