#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/population/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/world/system.hpp"
#include <algorithm>

namespace d2x::server::objects {
StepStatus System::publishChanges(TickContext tick) {
    std::erase_if(publishedModes_,[&](const auto &entry){return !state_.objects.contains(entry.first);});
    for(const auto &[id,object]:state_.objects) {
        if(!object.rule.chest && !object.rule.shrine && object.rule.operation!=18) continue;
        auto [previous,inserted]=publishedModes_.try_emplace(id,object.revision,object.mode);
        // Initialization seeds the baseline before any operation, including
        // operations in the birth frame. Unknown initial state is a snapshot.
        if(inserted) continue;
        if(previous->second.first==object.revision) continue;
        const int before=previous->second.second;
        // Native ENDANIM writes ON silently. The preceding OP fact is already
        // retained even when hosting advances several frames before delivery.
        if(before!=object.mode && !(before==1 && object.mode==2)) {
            const auto shrine=object.rule.shrine && (object.rule.subClass&1) && !(object.rule.subClass&4)
                ?std::optional<int>{object.rule.shrine->code}:std::nullopt;
            const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},object.area},
                {ObjectFact{id,object.operatorActor,object.area,object.revision,object.position,object.mode,
                    object.rule.operation!=18 && object.rule.selectable[size_t(object.mode)],shrine}}});
            if(!sent) return StepStatus::Blocked;
        }
        // Advance only after acceptance. A full outbox retries this publication
        // phase without replaying the operation, inventory or effect transaction.
        previous->second={object.revision,object.mode};
    }
    return StepStatus::Complete;
}
namespace {
FrameEventType hazardEvent(const Object &object) {
    switch(object.rule.initialization) {
    case 24:return FrameEventType::StatRegeneration;
    case 51:return FrameEventType::PeriodicStats;
    case 58:return FrameEventType::PeriodicSkills;
    case 59:return FrameEventType::MonsterModifier;
    default:return FrameEventType::ModeChange;
    }
}
}
void System::activateAncientStatues(const std::array<EntityId,3> &statues,uint64_t tick) {
    RegionId region;
    for(const auto id:statues) {
        auto &object=state_.objects.at(id);region=object.area;
        object.mode=3;object.until=0;++object.revision;
        const FrameEventOwner owner{FrameEventUnit::Object,id};
        ports_.frameEvents.cancelHandler(owner,FrameEventType::MonsterModifier,0);
        ports_.frameEvents.insertPrepared({{},owner,FrameEventType::MonsterModifier,tick+20,false,0,0,0,tick+20});
    }
    collision(region);
}
void System::schedule(Object &object) {
    const FrameEventOwner owner{FrameEventUnit::Object,object.id};
    const auto synchronize=[&](FrameEventType type,uint64_t due) {
        const auto events=ports_.frameEvents.events(owner);
        if(std::any_of(events.begin(),events.end(),[&](const auto &event){return event.type==type && event.payload==due && due;})) return;
        std::optional<FrameEventQueue::PreparedInsertion> timer;
        if(due) timer=ports_.frameEvents.prepareInsertion({{},owner,type,
            std::max(due,ports_.frameEvents.currentFrame()+1),false,0,0,0,due});
        ports_.frameEvents.cancelHandler(owner,type,0);
        if(timer) ports_.frameEvents.publish(std::move(*timer));
    };
    synchronize(FrameEventType::EndAnimation,!ports_.quests.prisonerPortalDue(object.id) && !ports_.quests.npcPortalDue(object.id) && !object.rule.door && object.rule.initialization!=22 &&
        object.rule.initialization!=24 && object.mode==1?object.until:0);
    synchronize(FrameEventType::ActiveState,object.reset);
    synchronize(hazardEvent(object),object.hazardAt);
    if(object.rule.initialization>=63 && object.rule.initialization<=65)
        synchronize(FrameEventType::MonsterModifier,ports_.quests.ancientStatueDue(object.id)); // Object QUESTFN is event7.
    if(object.rule.operation==10 && int(object.area)==38)
        synchronize(FrameEventType::MonsterModifier,ports_.quests.cainBirthDue(object.id));
    if(const auto due=ports_.quests.prisonerPortalDue(object.id))
        synchronize(FrameEventType::MonsterModifier,due);
    if(const auto due=ports_.quests.forgeRewardDue(object.id))
        synchronize(FrameEventType::MonsterModifier,due);
    if(const auto due=ports_.quests.npcPortalDue(object.id))
        synchronize(FrameEventType::MonsterModifier,due);
    synchronize(FrameEventType::Trap,object.trapAt);
    synchronize(FrameEventType::AiReset,object.lootAt);
    synchronize(FrameEventType::DelayedPortal,object.portalAt);
}
DomainResult<> System::frameEvent(const FrameEvent &event,uint64_t tick) {
    const auto found=state_.objects.find(event.owner.unit);
    if(found==state_.objects.end()) return {DomainStatus::Applied,std::monostate{}};
    auto &object=found->second;
    if(!ports_.areas.loaded(object.area,object.position)) {
        auto next=event;next.handle={};next.due=tick+1;
        ports_.frameEvents.insertPrepared(next);return {DomainStatus::Applied,std::monostate{}};
    }
    bool changed=false;
    if(event.type==FrameEventType::EndAnimation && object.until==event.payload) {
        ports_.quests.finishAncientObjectAnimation(object.id);
        if(object.mode==1) {object.mode=object.rule.operation==10 && int(object.area)==38?5:2;changed=true;}
        object.until=0;
    } else if(event.type==FrameEventType::ActiveState && object.reset==event.payload) {
        object.mode=0;object.reset=0;object.operatorActor={};changed=true;
    } else if(event.type==FrameEventType::AiThink && object.rule.operation==22) {
        // ObjMode::74B40 restores one use per event installed by OperateWell.
        // Multiple uses retain distinct callbacks; there is no refill loop.
        object.uses=std::min(2*object.rule.parameters[2],object.uses+1);
        if(object.rule.parameters[2]>0 && object.uses%object.rule.parameters[2]==0)
            object.mode=2-object.uses/object.rule.parameters[2];
        changed=true;
    } else if(event.type==FrameEventType::DelayedPortal && object.portalAt==event.payload) {
        const auto result=ports_.travel.linkNativePortal(object.id,tick);
        if(result.status==DomainStatus::Unavailable) {
            const auto region=object.area;
            ports_.world.retainControllerRoom(region,object.position,false);
            ports_.frameEvents.cancelOwner({FrameEventUnit::Object,object.id});
            ports_.events.discardUnitSound(object.id);ports_.areas.removeRoomUnit(object.id);
            state_.objects.erase(found);collision(region);
            return {DomainStatus::Applied,std::monostate{}};
        }
        if(!result) return result;
        object.portalAt=0;
    } else if(event.type==FrameEventType::Trap && object.trapAt==event.payload) {
        const auto result=objectTrap({tick},object.id);
        if(result==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        if(result!=StepStatus::Complete) return {DomainStatus::NotImplemented,{}};
    } else if(event.type==FrameEventType::AiReset && object.lootAt==event.payload && !object.pending) {
        if(const auto *player=ports_.players.find(object.operatorId)) {
            LootRequest source;source.source=object.id;source.region=object.area;source.difficulty=ports_.settings.difficulty;
            const auto queued=ports_.loot.queue({object.revision,source,player->player,object.position,
                loot::ObjectSource{object.definition,object.rule.operation,{},{}}});
            if(!queued) return {queued.status,{}};
            object.pending=true;object.lootAt=0;
        }
    } else if(event.type==FrameEventType::MonsterModifier && ports_.quests.npcPortalDue(object.id)) {
        const auto result=ports_.quests.npcPortalEvent(object.id,tick);if(!result) return result;
    } else if(event.type==FrameEventType::MonsterModifier && ports_.quests.forgeRewardDue(object.id)) {
        const auto result=ports_.quests.forgeRewardEvent(object.id,tick);if(!result) return result;
    } else if(event.type==FrameEventType::MonsterModifier && ports_.quests.prisonerPortalDue(object.id)) {
        const auto result=ports_.quests.prisonerPortalEvent(object.id,tick);if(!result) return result;
    } else if(event.type==FrameEventType::MonsterModifier && object.rule.operation==10 && int(object.area)==38) {
        const auto result=ports_.quests.cainBirthEvent(object.id,tick);if(!result) return result;
        object.mode=5;object.until=0;changed=true;
    } else if(event.type==FrameEventType::MonsterModifier && object.rule.initialization>=63 && object.rule.initialization<=65) {
        const auto result=ports_.quests.ancientStatueEvent(object.id,tick);if(!result) return result;
        if(const auto mode=ports_.quests.objectMode(object.area,object.id,object.definition,object.rule.operation,tick);mode && *mode!=object.mode) {
            object.mode=*mode;changed=true;
        }
    } else if(event.type==FrameEventType::MonsterModifier && object.rule.initialization==59 && object.hazardAt==event.payload) {
        if(object.mode==1) {object.mode=2;++object.revision;collision(object.area);}
        DomainResult<> result{DomainStatus::Applied,std::monostate{}};
        if(int(object.area)==76) {
            const int definition=ports_.areas.at(object.area).definition.populationRules.afterglowClass;
            if(definition<0) return {DomainStatus::NotImplemented,{}};
            result=ports_.population.requestObjectBirth(object.area,object.id,object.position,{tick},definition);
        } else if(int(object.area)==108) result=ports_.quests.sealAfterglow(object.area,object.id,object.position,tick);
        if(isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale)
            return {DomainStatus::Capacity,{}};
        if(result.status==DomainStatus::NotImplemented) return result;
        object.hazardAt=!result && int(object.area)==108?tick+10:0;
    } else if(event.type==hazardEvent(object) && object.hazardAt==event.payload) {
        if(hazardStep({tick},object.id)==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
    }
    if(changed) {++object.revision;collision(object.area);}
    // The executing node is still linked until dispatch accepts this callback.
    ports_.frameEvents.cancel(event.handle);schedule(object);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::trapEvent(EntityId id,uint64_t tick) {
    if(monsterTrap({tick},id)==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
    const auto *unit=ports_.monsters.find(id);
    if(unit && unit->alive()) {
        const auto found=traps_.find(id);
        auto due=tick+uint64_t(std::max(1,unit->rule.decisionTicks));
        if(found!=traps_.end()) {
            const auto &clock=found->second;
            if(clock.release) due=clock.release;
            else if(clock.next>tick) due=clock.next;
        }
        ports_.monsters.scheduleAi(id,std::max({due,unit->frozenUntil,unit->stunnedUntil,unit->knockedUntil}));
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
