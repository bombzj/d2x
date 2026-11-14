#include "system.hpp"
#include "world/native_path_step.hpp"
#include "server/player_store.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/facts/travel.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/world/system.hpp"
#include <algorithm>

namespace d2x::server::travel {
DomainResult<> System::useTeleportPad(const ActorContext &actor,EntityId sourceId) {
    const auto *player=ports_.players.find(actor.player);
    const auto *area=ports_.areas.find(actor.area);
    const auto &objects=ports_.objects.read().objects;
    const auto source=objects.find(sourceId);
    if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area || player->persistent.player.hp<=0)
        return {DomainStatus::InvalidActor,{}};
    if(!area || area->generation!=actor.areaGeneration || source==objects.end() ||
        source->second.area!=actor.area || source->second.rule.operation!=27) return {DomainStatus::Stale,{}};
    auto pending=padTransfers_.find(actor.player);
    if(pending!=padTransfers_.end() && (pending->second.actor.sequence!=actor.sequence || pending->second.source!=sourceId)) {
        padTransfers_.erase(pending);pending=padTransfers_.end();
    }
    const auto reject=[&](DomainStatus status) {padTransfers_.erase(actor.player);return DomainResult<>{status,{}};};
    if(pending==padTransfers_.end()) {
        const auto ownRoom=ports_.areas.roomOfUnit(sourceId);
        if(!ownRoom) return {DomainStatus::Unavailable,{}};
        EntityId targetId;RoomView counterpart=*ownRoom;
        const auto findIn=[&](RoomView room) {
            for(const auto id:ports_.areas.roomUnitOrder(room)) {
                const auto other=objects.find(id);
                if(id!=sourceId && other!=objects.end() && other->second.definition==source->second.definition) {
                    targetId=id;counterpart=room;return true;
                }
            }
            return false;
        };
        // Retail 1.13c Operate27 (RVA 6BE40): own unit chain first,
        // then active adjacent rooms in their original order. No nearest pad.
        if(!findIn(*ownRoom)) {
            const auto &definition=area->definition;
            const auto *room=definition.activation.room(Vec{float(ownRoom->x*5),float(ownRoom->y*5)}-definition.origin);
            if(!room) return {DomainStatus::Unavailable,{}};
            const size_t index=size_t(room-definition.activation.rooms().data());
            for(const auto near:definition.roomNear.at(index)) if(near!=*ownRoom && findIn(near)) break;
        }
        if(!targetId) return {DomainStatus::Unavailable,{}};
        const auto &target=objects.at(targetId);
        const auto &targetArea=ports_.areas.at(counterpart.area);
        const Vec global=target.position+targetArea.definition.origin;
        const auto sample=ports_.spatial.nativeCollisionSample(0x1c09);
        const auto pad=ports_.areas.nativeFreeCoordinates(counterpart,int(global.x),int(global.y),3,0x1c09,sample);
        if(!pad) return {DomainStatus::Unavailable,{}};
        // Operate27's helper passes the original counterpart room into SUNIT;
        // SUNIT repeats free-coordinate search with the player's native size 2.
        const Vec padGlobal=pad->position+ports_.areas.at(pad->room.area).definition.origin;
        const auto landing=ports_.areas.nativeFreeCoordinates(counterpart,int(padGlobal.x),int(padGlobal.y),2,0x1c09,sample);
        if(!landing) return {DomainStatus::Unavailable,{}};
        const auto &destination=ports_.areas.at(landing->room.area);
        if(destination.definition.act!=area->definition.act) return {DomainStatus::Unavailable,{}};
        pending=padTransfers_.emplace(actor.player,PadTransfer{actor,sourceId,targetId,source->second.revision,
            target.revision,player->locomotionSequence,counterpart,{landing->room.area,destination.generation,landing->position}}).first;
    }
    const auto plan=pending->second;
    const auto target=objects.find(plan.target);
    const auto *destination=ports_.areas.find(plan.landing.area);
    if(plan.actor.actor!=actor.actor || plan.actor.areaGeneration!=actor.areaGeneration ||
        plan.locomotion!=player->locomotionSequence || source->second.revision!=plan.sourceRevision ||
        target==objects.end() || target->second.revision!=plan.targetRevision ||
        ports_.areas.roomOfUnit(plan.target)!=plan.counterpart || !destination || destination->generation!=plan.landing.generation)
        return reject(DomainStatus::Stale);
    // Content/output backpressure retains the selected pair and landing. It
    // must not repeat a committed relocation or select a different counterpart.
    if(!ports_.world.prepareClientRoom(plan.landing.area,plan.landing.position+Vec{.5f,.5f}))
        return {DomainStatus::PendingContent,{}};
    if(player->teleportRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto landingRoom=ports_.areas.roomAtPosition(plan.counterpart.area,
        Vec{float(plan.counterpart.x*5),float(plan.counterpart.y*5)}-ports_.areas.at(plan.counterpart.area).definition.origin,
        int(plan.landing.position.x+destination->definition.origin.x),int(plan.landing.position.y+destination->definition.origin.y));
    if(!landingRoom || ports_.areas.nativeCollisionMask(*landingRoom,
        int(plan.landing.position.x+destination->definition.origin.x),int(plan.landing.position.y+destination->definition.origin.y),
        2,0x1c09,ports_.spatial.nativeCollisionSample(0x1c09))) return reject(DomainStatus::Unavailable);
    std::vector<EventBatch> facts;
    if(plan.landing.area!=actor.area) facts.push_back({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},
        {TravelFact{actor.player,actor.actor,actor.area,plan.landing.area,destination->generation,plan.landing.position,false}}});
    facts.push_back({0,actor.tick,{}, {AudienceKind::Area,{},plan.landing.area},
        {RepositionFact{actor.actor,plan.landing.area,plan.landing.position,true,1,true,
            RepositionFact::Room{plan.counterpart.area,uint16_t(plan.counterpart.x),uint16_t(plan.counterpart.y)}}}});
    const auto published=ports_.events.publishGroup(std::move(facts));
    if(!published) return {published.status,{}};
    auto &moving=ports_.players.players_.at(actor.player);
    moving.area=plan.landing.area;moving.position=plan.landing.position;
    moving.route.clear();NativePathStep::reset(moving.pathStep);moving.knockback.reset();moving.moving=false;moving.runningNow=false;
    moving.movementTarget.reset();moving.movementArrival.reset();moving.routeDestination=moving.position;
    moving.routeObstacles=destination->definition.collision.obstacleRevision;
    moving.lastTeleportPosition=moving.position+destination->definition.origin;++moving.teleportRevision;
    moving.movementHistory.push_back(moving.lastTeleportPosition);
    if(moving.movementHistory.size()>20) moving.movementHistory.pop_front();
    ports_.areas.moveRoomUnit(moving.actor,moving.area,moving.position);
    ports_.world.changeClientRoom(actor.player,actor.tick);
    if(moving.area!=actor.area) {ports_.npc.close(actor.player);ports_.inventory.close(actor.player);state_.waypoints.erase(actor.player);}
    cancel(actor.player);padTransfers_.erase(actor.player);
    return {DomainStatus::Applied,std::monostate{}};
}
} // namespace d2x::server::travel
