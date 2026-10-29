#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/objects/system.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace d2x::server::monsters {
void System::setStoredOwner(Actor &unit,EntityId owner) const {
    unit.nativeOwner=owner;
    const int state=unit.rule.storedOwnerState;
    if(state<0 || !unit.nativeStats || !unit.nativeStats->sources || !unit.rule.stats) return;
    auto root=std::make_shared<StatList>(*unit.nativeStats->sources);root->fullValues.reset();
    std::erase_if(root->contributions,[&](const auto &list){return list && list->source.kind==StatSourceKind::State && list->source.definition==state;});
    if(owner) {
        std::vector<ModifierStatSource> sources;attributes::appendStoredOwnerStats(unit,sources);
        root->contributions.push_back(prepareModifierStatSource(*unit.rule.stats,sources.front(),StatOwnerKind::Monster));unit.aiStates.insert(state);
    } else unit.aiStates.erase(state);
    unit.nativeStats->values=evaluateStatList(*unit.rule.stats,*root,{root.get(),{}, {}});unit.nativeStats->sources=std::move(root);
}
void System::resetImpAi(EntityId id) {
    if(auto found=state_.actors.find(id);found!=state_.actors.end()) {
        auto &unit=found->second;unit.impMountedAi=false;unit.impMountedSkill=unit.impMountedParameter=0;
        ++unit.aiFunctionRevision;++unit.revision;
    }
}
void System::initializeImpMount(Actor &unit) const {
    unit.impMountedAi=true;unit.impMountedParameter=unit.impMountedSkill=0;
    int index=int(rollRandom(unit.combatRandom)&1);
    constexpr std::array indices{2,4};
    for(int attempt=0;attempt<2;++attempt) {
        index=(index+1)%2;const auto slot=indices[size_t(index)];
        if(unit.rule.baseSkillRanks.contains(unit.rule.skillIds[size_t(slot)])) {unit.impMountedSkill=uint8_t(slot);break;}
    }
    ++unit.aiFunctionRevision;++unit.revision;
}
DomainResult<> System::impRiderEvent(const FrameEvent &event,uint64_t tick) {
    const auto *source=find(event.owner.unit);
    const auto *mount=source?find(source->nativeOwner):nullptr;
    if(!source || !source->aiStates.contains(source->rule.attachedState) || !mount) {
        ports_.frameEvents.cancel(event.handle);return {DomainStatus::Applied,std::monostate{}};
    }
    auto &unit=state_.actors.at(source->id);
    if(unit.area!=mount->area || unit.position.x!=mount->position.x || unit.position.y!=mount->position.y) {
        if(unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        unit.area=mount->area;unit.position=mount->position;
        ports_.areas.moveRoomUnit(unit.id,unit.area,unit.position);refreshCollision(unit);++unit.revision;
    }
    (void)tick;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::impTeleport(EntityId id,EntityId target,Vec point,int skill,int rank,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto *source=find(id);if(!source) return {DomainStatus::InvalidActor,{}};
    const bool retail=!source->rule.stats || !source->rule.stats->pd2;
    if(retail) {
        if(!source->rule.formulas || !source->rule.formulas->skills.contains(skill)) return {DomainStatus::InvalidRequest,{}};
        const auto &program=source->rule.formulas->skills.at(skill);
        if(program.number("srvdofunc")!=129) return {DomainStatus::InvalidRequest,{}};
        if(!source->rule.states || !source->rule.states->contains(program.suppressionState)) return {DomainStatus::Unavailable,{}};
    } else if(source->rule.ai.kind!=MonsterAiKind::Imp) {
        if(!source->alive()) return {DomainStatus::InvalidActor,{}};
        if(!source->rule.formulas || !source->rule.formulas->skills.contains(skill) || source->rule.formulas->skills.at(skill).number("srvdofunc")!=129)
            return {DomainStatus::InvalidRequest,{}};
        if(source->nativeOwner || target) return {DomainStatus::NotImplemented,{}};
        return nativeTeleport(id,point,tick,collision);
    }
    // Do129 is generic; only its two attachment helpers test BaseId=imp1.
    const bool imp=retail?source->rule.impBaseClass>=0 && source->rule.baseClass==source->rule.impBaseClass:
        source->rule.ai.kind==MonsterAiKind::Imp;
    if(source->impDetachDeathPending) {
        const auto killed=scriptDeath(id,tick);
        if(killed) state_.actors.at(id).impDetachDeathPending=false;
        return killed;
    }
    if(!source->alive()) return {DomainStatus::InvalidActor,{}};
    if(imp && source->rule.attachedState<0) return {DomainStatus::Unavailable,{}};
    const auto *owner=find(source->nativeOwner);
    const auto ownerExists=[&](EntityId unit) {
        return unit && (find(unit) || ports_.players.findActor(unit) || ports_.objects.read().objects.contains(unit));
    };
    const auto carrier=[&](const Actor &unit){return unit.rule.baseClass==source->rule.siegeBeastBaseClass || unit.rule.baseClass==source->rule.barricadeTowerBaseClass;};
    if(retail?ownerExists(source->nativeOwner):owner!=nullptr) {
        if(!imp || !owner || !carrier(*owner)) {
            if(!retail) return {DomainStatus::Applied,std::monostate{}};
            const auto moved=nativeTeleport(id,point,tick,collision,false);
            if(isDeferred(moved.status)) return moved;
            // The owned branch returns success even when free-coordinate
            // relocation failed. Non-Imp helpers leave both owners untouched.
            return {DomainStatus::Applied,std::monostate{}};
        }
        if(source->revision>UINT64_MAX-4 || owner->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        // GeneralTeleport's free-coordinate failure does not prevent detaching.
        // Reserve the state publication first so output backpressure cannot
        // repeat a successful relocation or expose half of the owner pair.
        if(!ports_.events.hasCapacity(3)) return {DomainStatus::Capacity,{}};
        const auto moved=nativeTeleport(id,point,tick,collision,!retail);if(isDeferred(moved.status)) return moved;
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},find(id)->area},
            {StateFact{id,1,find(id)->area,source->rule.attachedState,false},StateFact{id,1,find(id)->area,source->rule.storedOwnerState,false},
             StateFact{owner->id,1,owner->area,owner->rule.storedOwnerState,false}}});
        if(!sent) return {sent.status,{}};
        auto &unit=state_.actors.at(id);auto &mount=state_.actors.at(owner->id);
        unit.aiStates.erase(unit.rule.attachedState);setStoredOwner(unit,{});setStoredOwner(mount,{});++mount.revision;
        const int64_t maximum=unit.maximumLife>>8,life=unit.life>>8;
        const bool survive=maximum && 100*life/maximum>=10;
        if(survive) {
            unit.targetFlagsDisabled=false;resetImpAi(id);
            ports_.frameEvents.cancel({FrameEventUnit::Monster,id},FrameEventType::AiReset,skill);
        } else {unit.impDetachDeathPending=true;++unit.revision;}
        // Do147 cancels its continuous event on the next callback after the
        // attached state is cleared; no new timer is created when detaching.
        if(!survive) return impTeleport(id,{},point,skill,rank,tick,collision);
        return {DomainStatus::Applied,std::monostate{}};
    }
    // Retail DBD0 clears a dynamic-path target which points to the caster.
    if(!target || (retail && target==id)) return nativeTeleport(id,point,tick,collision);
    const auto *mount=find(target);
    const auto *player=retail?ports_.players.findActor(target):nullptr;
    // SUNIT_IsDead rejects other unit types, including objects.
    if(mount?(!mount->alive() || (retail && mount->roomless) || (retail?ownerExists(mount->nativeOwner):find(mount->nativeOwner)!=nullptr)):
        (!player || !player->entered || player->persistent.player.hp<=0)) return {DomainStatus::Unavailable,{}};
    const auto destination=mount?mount->area:player->area;
    const Vec position=mount?mount->position:player->position;
    if(retail?!ports_.areas.loaded(destination,position):!ports_.areas.at(destination).definition.activation.room(position)) return {DomainStatus::Unavailable,{}};
    const bool attach=imp && mount && carrier(*mount);
    if(source->revision>UINT64_MAX-4 || (attach && mount->revision==UINT64_MAX)) return {DomainStatus::Capacity,{}};
    std::optional<FrameEventQueue::PreparedInsertion> timer;
    if(attach) {
        timer.emplace(ports_.frameEvents.prepareInsertion({{}, {FrameEventUnit::Monster,id},FrameEventType::ActiveState,tick+1,true,0,skill,rank}));
        const auto ownerFact=[](const Actor &unit,EntityId owner) {return StateFact{unit.id,1,unit.area,unit.rule.storedOwnerState,true,
            {{unit.rule.stats->names.at("source_unit_type"),1},{unit.rule.stats->names.at("source_unit_id"),int64_t(owner.value)}}};};
        const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},source->area},
            {StateFact{id,1,source->area,source->rule.attachedState,true},ownerFact(*source,target),ownerFact(*mount,id)}});
        if(!sent) return {sent.status,{}};
    }
    // a6=1 uses the target room's exact integer coordinates, with no free
    // position search or fallback around the target's collision footprint.
    auto &unit=state_.actors.at(id);stop(id);unit.area=destination;
    unit.position=retail?Vec{std::floor(position.x)+.5f,std::floor(position.y)+.5f}:position;
    ports_.areas.moveRoomUnit(id,unit.area,unit.position);refreshCollision(unit);++unit.revision;
    if(attach) {
        auto &horse=state_.actors.at(target);setStoredOwner(horse,id);++horse.revision;
        setStoredOwner(unit,target);unit.aiStates.insert(unit.rule.attachedState);unit.targetFlagsDisabled=true;
        initializeImpMount(unit);
        ports_.frameEvents.cancel({FrameEventUnit::Monster,id},FrameEventType::ActiveState,skill);
        ports_.frameEvents.publish(std::move(*timer));
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
