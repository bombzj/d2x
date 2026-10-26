#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/player_store.hpp"

namespace d2x::server::maps {
void System::objectRestored(EntityId previous,EntityId current) {
    const auto found=ports_.objects.read().objects.find(current);
    if(found==ports_.objects.read().objects.end()) return;
    const auto &object=found->second;bool retained=object.nativePortalOwner || bool(object.pd2Portal);
    auto portal=state_.portals.extract(previous);
    if(!portal.empty()) {portal.key()=current;state_.portals.insert(std::move(portal));}
    for(auto &[id,birth]:state_.portals) {
        (void)id;
        if(birth.destination==previous) birth.destination=current;
        retained|=birth.destination==current;
    }
    for(auto &[id,death]:state_.deaths) {
        (void)id;
        if(death.portalObject==previous) death.portalObject=current;
        if(death.portalDestination==previous) death.portalDestination=current;
        retained|=death.portalObject==current || death.portalDestination==current;
    }
    auto operation=state_.eventOperations.extract(previous);
    if(!operation.empty()) {operation.key()=current;state_.eventOperations.insert(std::move(operation));retained=true;}
    for(auto &[key,event]:state_.roomEvents) {
        (void)key;
        if(event.source==previous) event.source=current;
        if(event.portal.portalObject==previous) event.portal.portalObject=current;
        if(event.portal.portalDestination==previous) event.portal.portalDestination=current;
    }
    for(size_t slot=0;slot<state_.bindings.size();++slot)
        if(state_.bindings[slot].object && state_.bindings[slot].id==previous)
            bind(int(slot),current,object.area,object.position,true);
    for(const auto &[id,other]:ports_.objects.read().objects) {(void)id;retained|=other.nativePortalOwner==current;}
    if(retained) {
        ports_.world.retainControllerUnitRoom(previous,object.area,object.position,false);
        ports_.world.retainControllerUnitRoom(current,object.area,object.position,true);
    }
}
DomainResult<> System::operateTome(const ActorContext &actor,EntityId id) {
    synchronize();
    const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    const auto &objects=ports_.objects.read().objects;
    const auto tome=objects.find(id);
    if(!state_.instance || state_.instance->controller!=30 || !area || !player || !player->entered ||
        player->actor!=actor.actor || player->area!=actor.area || area->generation!=actor.areaGeneration ||
        tome==objects.end() || tome->second.area!=actor.area || tome->second.definition!=600 || tome->second.mode==2)
        return {DomainStatus::InvalidRequest,{}};
    EntityId portal;
    const auto *room=area->definition.activation.room(player->position);
    if(room) for(const int type:{598,599}) {
        for(const auto near:area->definition.roomNear.at(size_t(room-area->definition.activation.rooms().data()))) {
            for(const auto candidate:ports_.areas.roomUnitOrder(near))
                if(const auto object=objects.find(candidate);object!=objects.end() && object->second.definition==type) {
                    portal=candidate;break;
                }
            if(portal) break;
        }
        if(portal) break;
    }
    if(!portal) {
        // 17d5 3A8270 / 41d49 3AA650: the missing nearby gate calls
        // PlaySound(player, 19, 0). The decompiler omits EDX=0x13.
        const auto refused=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},
            {SoundFact{actor.actor,0,actor.area,19}}});
        return {refused?DomainStatus::Unavailable:refused.status,{}};
    }
    const auto &gate=objects.at(portal);
    if(gate.pd2Portal) {
        const auto birth=state_.portals.find(portal);
        if(birth==state_.portals.end() || birth->second.phase!=5) return {DomainStatus::PendingContent,{}};
    }
    if(tome->second.revision==UINT64_MAX || gate.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    uint8_t next=state_.phase;
    std::vector<DomainFact> facts;
    if(gate.definition==598 && state_.phase<2) {
        facts.emplace_back(SoundFact{id,2,actor.area,uint16_t(0x1260+state_.phase),true});++next;
    } else if(gate.definition==599 && state_.phase<3) {
        facts.emplace_back(SoundFact{id,2,actor.area,0x1262,true});next=3;
    }
    facts.emplace_back(SoundFact{id,2,actor.area,0x1263,true});
    const auto published=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},std::move(facts)});
    if(!published) return {published.status,{}};
    // These two mode writes cannot fail after the revision preflight above;
    // the event publication and both object states form one accepted action.
    state_.phase=next;
    ports_.objects.setNativeMode(id,2,actor.tick);
    ports_.objects.setNativeMode(portal,1,actor.tick,true);
    return {DomainStatus::Applied,std::monostate{}};
}
bool System::advancePortals(TickContext tick) {
    for(const auto &[id,object]:ports_.objects.read().objects)
        if(object.pd2Portal && !state_.portals.contains(id)) {
            state_.portals.emplace(id,State::PortalBirth{*object.pd2Portal});
            ports_.world.retainControllerUnitRoom(id,object.area,object.position,true);
        }
    bool pending{};
    for(auto &[id,birth]:state_.portals) {
        if(birth.phase==5) continue;
        const auto &objects=ports_.objects.read().objects;
        const auto source=objects.find(id);
        if(source==objects.end()) {birth.phase=5;continue;}
        const auto &program=birth.program;
        if(birth.phase==0) {
            const auto point=preparePortalLanding(program,birth.anchor,birth.landing);
            if(isDeferred(point.status)) {pending=true;continue;}
            if(!point) {birth.phase=5;continue;}
            birth.phase=1;
        }
        if(birth.phase==1) {
            const auto &point=*birth.landing;
            ports_.world.activate(point.area,point.position);
            if(!ports_.areas.loaded(point.area,point.position)) {pending=true;continue;}
            const auto created=ports_.objects.admit({program.definition,point.area,point.position,{},{},0,tick.tick});
            if(isDeferred(created.status)) {pending=true;continue;}
            if(!created) {birth.phase=5;continue;}
            birth.destination=*created.value;birth.phase=2;
        }
        if(birth.phase==2) {
            const auto linked=ports_.objects.linkNativePortals(id,birth.destination,program.mutual,tick.tick);
            if(isDeferred(linked.status)) {pending=true;continue;}
            if(!linked) {birth.phase=5;continue;}
            birth.phase=3;
        }
        if(birth.phase==3) {
            const auto from=ports_.objects.setNativeMode(id,program.sourceMode,tick.tick,program.sourceMode==1);
            if(isDeferred(from.status)) {pending=true;continue;}
            birth.phase=4;
        }
        if(birth.phase==4) {
            const auto to=ports_.objects.setNativeMode(birth.destination,program.destinationMode,tick.tick,program.destinationMode==1);
            if(isDeferred(to.status)) {pending=true;continue;}
            birth.phase=5;
        }
    }
    return pending;
}
}
