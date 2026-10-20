#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/movement.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/objects/system.hpp"
#include "gameplay/areas/waypoint.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/social/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::travel {
namespace {
std::deque<Vec> playerRoute(const Grid &grid,Vec from,Vec to) {
    constexpr Vec center{.5f,.5f};
    auto route=grid.nativePlayerPath(from+center,to+center,playerMovement);
    for(auto &point:route) point=point-center;
    return route;
}
}

DomainResult<> System::walked(const ActorContext &actor) {
    auto *player = ports_.players.find(actor.player);
    const auto *source = ports_.areas.find(actor.area);
    if (!player || !source || !player->entered || player->actor != actor.actor || player->area != actor.area ||
        source->generation != actor.areaGeneration) return {DomainStatus::InvalidActor, {}};
    constexpr Vec center{.5f, .5f};
    if (source->definition.activation.room(player->position + center)) return {DomainStatus::Applied, std::monostate{}};
    Vec navigationOrigin;
    const auto navigation = source->definition.collision.linkedNavigation(navigationOrigin);
    const AreaState *destination = nullptr;
    Vec position;
    for (const auto &[id, candidate] : ports_.areas.all()) {
        (void)id;
        if (candidate.definition.act != source->definition.act ||
            std::none_of(navigation.navigationSources.begin(), navigation.navigationSources.end(),
                [&](const auto &entry) { return entry.grid == &candidate.definition.collision; })) continue;
        const Vec point = player->position + source->definition.origin - candidate.definition.origin;
        if (!candidate.definition.activation.room(point + center)) continue;
        if (destination) return {DomainStatus::Conflict, {}};
        destination = &candidate; position = point;
    }
    if (!destination) return {DomainStatus::Unavailable, {}};
    // D2MOO Step::PATH_RecacheRoomAtCoordIfNeeded: preserve the continuous
    // position and remaining path; room ownership follows the occupied cell.
    // Publish before transferring authority, so backpressure can retry safely.
    const auto output = ports_.events.publish({0, actor.tick, {}, {AudienceKind::Player, actor.player, destination->definition.id},
        {TravelFact{actor.player, actor.actor, actor.area, destination->definition.id, destination->generation, position, true}}});
    if (!output) return {output.status, {}};
    ports_.npc.close(actor.player); ports_.inventory.close(actor.player); state_.waypoints.erase(actor.player); cancel(actor.player);
    const Vec offset = source->definition.origin - destination->definition.origin;
    auto &moving = ports_.players.players_.at(actor.player);
    moving.area = destination->definition.id;
    moving.position = moving.pathStep.translate(moving.position + center, offset) - center;
    ports_.areas.moveRoomUnit(moving.actor,moving.area,moving.position);
    for (auto &point : moving.route) point = point + offset;
    moving.routeDestination = moving.routeDestination + offset;
    if (moving.movementArrival) {
        moving.movementArrival->pos = moving.movementArrival->pos + offset;
        moving.movementArrival->accessPoint = moving.movementArrival->accessPoint + offset;
    }
    moving.routeObstacles = destination->definition.collision.navigationRevision();
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    if (request.kind != Kind::Exit) return useSpecial(actor,request);
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0 ||
        !area || actor.areaGeneration != area->generation) return {DomainStatus::InvalidActor, {}};
    const auto found = std::find_if(area->definition.exits.begin(), area->definition.exits.end(), [&](const auto &e) { return e.id == request.source.id; });
    if (found == area->definition.exits.end() || (request.destination && *request.destination != found->destination)) return {DomainStatus::InvalidRequest, {}};
    if (!ports_.quests.allowsTravel(player->persistent.player,actor.area,found->destination)) return {DomainStatus::Conflict, {}};
    const int distance = nativeUnitDistance(player->position, 2, found->position, 1);
    if (distance > 50) return {DomainStatus::InvalidRequest, {}};
    std::deque<Vec> route;
    if (distance > 4) {
        route = playerRoute(ports_.spatial.navigationGrid(actor.area,actor.actor),player->position,found->arrival);
        if (route.empty()) return {DomainStatus::Unavailable, {}};
    }
    const auto prepared = ports_.world.requestArea(actor, found->destination);
    if (!prepared) return {prepared.status, {}};
    state_.transitions[actor.player] = {actor.area, found->destination, *prepared.value, area->generation, actor.sequence, found->id, found->position, {}};
    auto &mutablePlayer = ports_.players.players_.at(actor.player);
    mutablePlayer.route = std::move(route); mutablePlayer.routeRunning = mutablePlayer.running;
    mutablePlayer.pathStep.reset();
    mutablePlayer.movementTarget.reset();mutablePlayer.movementArrival.reset();
    mutablePlayer.routeDestination=found->arrival;mutablePlayer.routeObstacles=area->definition.collision.obstacleRevision;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::teleport(const ActorContext &actor, PointTarget target, Fixed8 manaCost,std::optional<SkillCharge> charge,int skill) {
    const auto *player = ports_.players.find(actor.player);
    if (!player) return {DomainStatus::InvalidActor, {}};
    ports_.world.activate(target.area, target.position + Vec{.5f,.5f});
    const auto result = ports_.transactions.release(actor, player->characterRevision, manaCost, target,charge,{},{},skill);
    if (result) {
        cancel(actor.player);
        if(target.area!=actor.area) {
            ports_.npc.close(actor.player);ports_.inventory.close(actor.player);state_.waypoints.erase(actor.player);
        }
    }
    return result;
}
DomainResult<> System::castTeleport(const ActorContext &actor,PointTarget target,Fixed8 manaCost,std::optional<SkillCharge> charge,int skill) {
    const auto *player=ports_.players.find(actor.player);
    const auto *source=ports_.areas.find(actor.area);
    if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area || player->persistent.player.hp<=0)
        return {DomainStatus::InvalidActor,{}};
    if(!source || source->generation!=actor.areaGeneration || target.area!=actor.area || target.generation!=actor.areaGeneration)
        return {DomainStatus::Stale,{}};
    if(!std::isfinite(target.position.x) || !std::isfinite(target.position.y)) return {DomainStatus::InvalidRequest,{}};
    const auto &definition=source->definition;
    if(!definition.teleportMode) return {DomainStatus::Unavailable,{}};
    const Vec global=target.position+definition.origin;
    if(global.x<0 || global.y<0 || global.x>UINT16_MAX || global.y>UINT16_MAX) return {DomainStatus::InvalidRequest,{}};
    const int column=int(std::floor(global.x+.5f)),row=int(std::floor(global.y+.5f));
    const auto *bounds=definition.activation.room(player->position+Vec{.5f,.5f});
    if(!bounds) return {DomainStatus::Unavailable,{}};
    const RoomView current{actor.area,(int(definition.origin.x)+bounds->x)/5,(int(definition.origin.y)+bounds->y)/5};
    // SrvDo27 -> SUNIT relocation: resolve the original room neighbourhood,
    // acquire its terrain, then search size 2 / 0x1c09 at the release frame.
    const auto acquire=[&](RoomView view) {
        const auto *area=ports_.areas.find(view.area);
        if(!area || area->definition.act!=definition.act) return false;
        const Vec point=Vec{float(column),float(row)}-area->definition.origin;
        const auto *room=area->definition.activation.room(point+Vec{.5f,.5f});
        if(!room || (int(area->definition.origin.x)+room->x)/5!=view.x ||
            (int(area->definition.origin.y)+room->y)/5!=view.y) return false;
        ports_.world.activate(view.area,point+Vec{.5f,.5f});return true;
    };
    bool acquired=acquire(current);
    if(!acquired) for(const auto room:definition.roomNear.at(size_t(bounds-definition.activation.rooms().data())))
        if(acquire(room)) {acquired=true;break;}
    if(!acquired) return {DomainStatus::Unavailable,{}};
    // UNITS_TestCollisionByCoordinates shortens both ends by unit size 2.
    if(definition.teleportMode==2 && !definition.collision.interactionSegment(player->position,target.position,2,{}))
        return {DomainStatus::Unavailable,{}};
    const auto room=ports_.areas.roomAtPosition(actor.area,player->position+Vec{.5f,.5f},column,row);
    if(!room) return {DomainStatus::Unavailable,{}};
    const auto sample=ports_.spatial.nativeCollisionSample(0x1c09);
    const auto free=ports_.areas.nativeFreeCoordinates(*room,column,row,2,0x1c09,sample);
    if(!free) return {DomainStatus::Unavailable,{}};
    const auto &destination=ports_.areas.at(free->room.area);
    return teleport(actor,{free->room.area,destination.generation,free->position},manaCost,charge,skill);
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    const auto synchronizationStatus=expireSynchronizations(tick);
    std::erase_if(state_.portals,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered;});
    std::erase_if(state_.waypoints,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered || p->area!=entry.second.area || p->persistent.player.hp<=0;});
    for(auto &[owner,portal]:state_.portals) { (void)owner; if(!portal.opened && tick.tick>=portal.ready) {portal.opened=true;++portal.revision;} }
    for (auto pending = state_.transitions.begin(); pending != state_.transitions.end();) {
        const auto found = ports_.players.players_.find(pending->first);
        auto &transition = pending->second;
        const auto *source = ports_.areas.find(transition.from), *destination = ports_.areas.find(transition.to);
        if (found == ports_.players.players_.end() || !source || source->generation != transition.sourceGeneration ||
            !found->second.entered || found->second.area != transition.from || found->second.locomotionSequence != transition.sequence || found->second.persistent.player.hp <= 0) {
            pending = state_.transitions.erase(pending); continue;
        }
        auto &player = found->second;
        if(transition.kind==Kind::Waypoint) {
            const ActorContext actor{player.player,player.actor,player.area,source->generation,transition.sequence,tick.tick};
            if(!waypointAccess(actor,transition.source,transition.waypointRevision) || !player.rules.character ||
                !player.rules.character->waypointIndices.contains(transition.to) || !player.persistent.waypoints.contains(transition.to)) {
                pending=state_.transitions.erase(pending);continue;
            }
        }
        if (!destination) {
            const ActorContext actor{player.player, player.actor, player.area, source->generation, transition.sequence, tick.tick};
            const auto prepared = transition.kind == Kind::Waypoint ? ports_.world.requestWaypoint(actor, transition.to) :
                (transition.kind == Kind::Npc || transition.kind==Kind::QuestObject) ? ports_.world.request(transition.to) : ports_.world.requestArea(actor, transition.to);
            if (!prepared) { pending = state_.transitions.erase(pending); continue; }
            ++pending; continue;
        }
        if (transition.kind == Kind::Exit) {
            if (nativeUnitDistance(player.position, 2, transition.approach, 1) > 4) {
                if (!player.route.empty()) { ++pending; continue; }
                pending = state_.transitions.erase(pending); continue;
            }
        } else {
            if (!player.route.empty()) { ++pending; continue; }
            if ((player.position - transition.approach).length() > .1f) { pending = state_.transitions.erase(pending); continue; }
        }
        Vec arrival = transition.arrival;
        // Warp/waypoint arrival must acquire the destination's real room before
        // free-coordinate and collision queries, including a previously retired room.
        const auto &targetDefinition = destination->definition;
        if(transition.arrivalGeneration && *transition.arrivalGeneration!=destination->generation) {pending=state_.transitions.erase(pending);continue;}
        if (transition.kind == Kind::Waypoint) {
            if(transition.arrivalGeneration) ports_.world.activate(transition.to,transition.arrival);
        }
        else if (transition.kind == Kind::Npc) ports_.world.activate(transition.to, targetDefinition.spawn);
        else if (transition.kind == Kind::Exit) {
            for (const auto &exit : targetDefinition.exits) if (exit.destination == transition.from)
                ports_.world.activate(transition.to, exit.arrival);
        } else if (transition.kind == Kind::QuestObject) {
            if(transition.arrivalGeneration) ports_.world.activate(transition.to,transition.arrival);
        } else ports_.world.activate(transition.to, arrival);
        if(transition.kind==Kind::Npc) {
            const ActorContext actor{player.player,player.actor,player.area,source->generation,transition.sequence,tick.tick};
            // The original client follows 0x38 travel with 0x30 close. The
            // accepted travel retains its lease identity while content loads;
            // a different conversation, movement, death or area invalidates it.
            const auto *conversation=ports_.npc.conversation(player.player);
            const auto *npc=ports_.npc.find(actor,transition.source);
            const auto &book=player.persistent.player.quests.at(size_t(player.persistent.difficulty));
            if(!npc || (conversation && conversation->revision!=transition.npcConversation) ||
                ((npc->rule.code=="warriv1" || npc->rule.code=="warriv2") && book.at(questIndex(QuestId::SistersToTheSlaughter)).stage<uint32_t(SlaughterStage::AndarielSlain)) ||
                (npc->rule.code=="meshif1" && book.at(questIndex(QuestId::SevenTombs)).stage<uint32_t(TombsStage::PassageGranted)) ||
                (npc->rule.code=="meshif2" && !player.persistent.player.completedActs.at(size_t(player.persistent.difficulty)).at(1))) {pending=state_.transitions.erase(pending);continue;}
            if(!transition.arrivalGeneration) {
                const auto spawn=ports_.world.nativeActSpawnLocationEx(transition.to,0,2);
                if(!spawn || spawn->area!=transition.to) {pending=state_.transitions.erase(pending);continue;}
                transition.arrival=spawn->position;transition.arrivalGeneration=destination->generation;
            }
            arrival=transition.arrival;
        }
        std::optional<Vec> exitWalk;
        if(transition.kind==Kind::SpecialPortal && (!ports_.quests.allowsTravel(player.persistent.player,transition.from,transition.to) || std::none_of(state_.specialPortals.begin(),state_.specialPortals.end(),[&](const auto &entry){return entry.second.fieldId==transition.source || entry.second.townId==transition.source;}))) {pending=state_.transitions.erase(pending);continue;}
        if (transition.kind==Kind::Portal) {
            const auto portal=std::find_if(state_.portals.begin(),state_.portals.end(),[&](const auto &entry){
                return (entry.second.fieldId==transition.source || entry.second.townId==transition.source) &&
                    (entry.first==player.player || ports_.social.sameParty(entry.first,player.player));
            });
            if(portal==state_.portals.end()) { pending=state_.transitions.erase(pending); continue; }
        }
        if(transition.kind==Kind::Waypoint) {
            const auto &definition=destination->definition;
            if(!nativeWaypointIndex(definition.waypointIndex) ||
                player.rules.character->waypointIndices.at(transition.to)!=definition.waypointIndex) {pending=state_.transitions.erase(pending);continue;}
            if(!transition.arrivalGeneration) {
                const auto spawn=ports_.world.nativeWaypointPosition(transition.to,source->definition.act!=definition.act);
                if(!spawn || spawn->area!=transition.to) {pending=state_.transitions.erase(pending);continue;}
                transition.arrival=spawn->position;transition.arrivalGeneration=destination->generation;
            }
            arrival=transition.arrival;
        }
        if(transition.kind==Kind::QuestObject) {
            const auto object=std::find_if(source->definition.objects.begin(),source->definition.objects.end(),[&](const auto &o){return o.id==transition.source;});
            if(object==source->definition.objects.end() || !ports_.quests.allowsTravel(player.persistent.player,transition.from,transition.to)) {pending=state_.transitions.erase(pending);continue;}
            if(object->rule.operation==34 || object->rule.operation==43) {
                if(!transition.arrivalGeneration) {
                    const auto point=object->rule.operation==43?ports_.world.nativeDurielPortalPosition(transition.to):
                        ports_.world.nativeActSpawnLocationEx(transition.to,int(transition.to)==74?11:0,2);
                    if(!point || point->area!=transition.to) {pending=state_.transitions.erase(pending);continue;}
                    transition.arrival=point->position;transition.arrivalGeneration=destination->generation;
                }
                arrival=transition.arrival;
            } else {
                if(targetDefinition.questArrival) ports_.world.activate(transition.to,*targetDefinition.questArrival);
                if(targetDefinition.portalArrival) ports_.world.activate(transition.to,*targetDefinition.portalArrival);
                for(const auto &exit:targetDefinition.exits) if(exit.destination==transition.from)
                    ports_.world.activate(transition.to,exit.arrival);
                if(targetDefinition.town) ports_.world.activate(transition.to,targetDefinition.spawn);
                const auto counterpart=std::find_if(destination->definition.objects.begin(),destination->definition.objects.end(),[&](const auto &o){return o.rule.operation==object->rule.operation;});
                auto marker=destination->definition.questArrival;
                if(!marker && counterpart!=destination->definition.objects.end() && source->definition.act==4) marker=counterpart->position;
                if(!marker && source->definition.act>=2) marker=destination->definition.portalArrival;
                if(!marker && (int(transition.to)==103 || int(transition.to)==109)) marker=destination->definition.spawn;
                if(!marker && int(transition.from)==73 && counterpart!=destination->definition.objects.end()) marker=counterpart->position;
                if(!marker) for(const auto &warp:destination->definition.exits) if(warp.destination==transition.from) {marker=warp.arrival;break;}
                if(!marker) {pending=state_.transitions.erase(pending);continue;}
                ports_.world.activate(transition.to,*marker);
                arrival=*marker;
            }
        }
        if (transition.kind==Kind::Exit) {
            const AreaExit *back = nullptr;
            for (const auto &exit : destination->definition.exits) if (exit.destination == transition.from) {
                if (back) { back = nullptr; break; } back = &exit;
            }
            if (!back) { pending = state_.transitions.erase(pending); continue; }
            arrival = back->arrival; exitWalk = back->exitWalk;
        }
        const auto global=arrival+targetDefinition.origin;
        const auto room=ports_.areas.roomAtPosition(transition.to,arrival+Vec{.5f,.5f},
            int(std::floor(global.x+.5f)),int(std::floor(global.y+.5f)));
        if(!room) {pending=state_.transitions.erase(pending);continue;}
        const bool changeAct=source->definition.act!=targetDefinition.act;
        const uint16_t mask=changeAct?0x1c89:playerMovement.mask;
        const auto free=ports_.areas.nativeFreeCoordinates(*room,int(std::floor(global.x+.5f)),int(std::floor(global.y+.5f)),
            2,mask,ports_.spatial.nativeCollisionSample(mask,{},player.actor),{},0,false,50,changeAct?5:1);
        if(!free || free->room.area!=transition.to) {pending=state_.transitions.erase(pending);continue;}
        arrival=free->position;
        if(exitWalk) *exitWalk=*exitWalk+arrival;
        std::deque<Vec> route;
        if (exitWalk) route = playerRoute(ports_.spatial.navigationGrid(transition.to,player.actor),arrival,*exitWalk);
        EventBatch event{0, tick.tick, {}, {AudienceKind::Player, player.player, transition.to},
            {TravelFact{player.player, player.actor, transition.from, transition.to, destination->generation, arrival, false}}};
        if(transition.kind==Kind::Npc || (int(transition.from)==102 && int(transition.to)==103) || (int(transition.from)==103 && int(transition.to)==109) || (int(transition.from)==132 && int(transition.to)==109)) {
            transactions::CharacterEdit edit{{player.player,player.actor,player.area,source->generation,transition.sequence,tick.tick},player.inventoryRevision,player.characterRevision,player.persistent.player};
            if(int(transition.to)==40 && int(transition.from)==1) {
                edit.player.quests.at(size_t(player.persistent.difficulty)).at(questIndex(QuestId::SistersToTheSlaughter)).stage=uint32_t(SlaughterStage::Completed);
                edit.player.completedActs.at(size_t(player.persistent.difficulty)).at(0)=true;
            }
            if(int(transition.to)==75 && int(transition.from)==40) edit.player.completedActs.at(size_t(player.persistent.difficulty)).at(1)=true;
            if(int(transition.to)==103 && int(transition.from)==102) edit.player.completedActs.at(size_t(player.persistent.difficulty)).at(2)=true;
            if(int(transition.to)==109 && int(transition.from)==103) edit.player.completedActs.at(size_t(player.persistent.difficulty)).at(3)=true;
            if(int(transition.to)==109 && int(transition.from)==132) {
                // Expansion has four inter-act completion words. Final Act V
                // completion is Baal's original bit/progression, not a fifth word.
                edit.player.quests.at(size_t(player.persistent.difficulty))[questIndex(QuestId::EveOfDestruction)].flags|=64;
            }
            // SUnitNpc::WARRIV1 activates Lut Gholein after travelling east.
            // Keep the waypoint, Act completion and travel in one transaction.
            if((int(transition.to)==40 || int(transition.to)==75 || int(transition.to)==103 || int(transition.to)==109) && nativeWaypointIndex(destination->definition.waypointIndex)) {
                edit.waypoints=player.persistent.waypoints;
                edit.waypoints->try_emplace(transition.to,float(tick.tick)*TickContext::seconds);
            }
            edit.facts.emplace_back(ports_.quests.projection(player.player,edit.player,transition.to,false));
            edit.facts.emplace_back(TravelFact{player.player,player.actor,transition.from,transition.to,destination->generation,arrival,false});
            auto plan=ports_.transactions.prepare(std::move(edit));
            if(!plan || !ports_.transactions.commit(std::move(*plan.value))) {++pending;continue;}
        } else if (!ports_.events.publish(std::move(event))) { ++pending; continue; }
        ports_.npc.close(player.player); ports_.inventory.close(player.player); state_.waypoints.erase(player.player);
        if(transition.kind==Kind::Portal) { const auto portal=state_.portals.find(player.player); if(portal!=state_.portals.end() && portal->second.town==transition.from) state_.portals.erase(portal); }
        player.area = transition.to; player.position = arrival;ports_.areas.moveRoomUnit(player.actor,player.area,player.position); player.route = std::move(route);
        player.pathStep.reset();
        player.routeRunning = player.running; player.moving = !player.route.empty();
        player.movementTarget.reset();player.movementArrival.reset();
        player.routeDestination=exitWalk.value_or(arrival);player.routeObstacles=destination->definition.collision.obstacleRevision;
        if(transition.kind==Kind::Waypoint) ports_.objects.onWaypointArrival(transition.to,arrival);
        pending = state_.transitions.erase(pending);
    }
    return state_.transitions.empty() && synchronizationStatus==StepStatus::Complete ? StepStatus::Complete : StepStatus::Blocked;
}
DomainResult<> System::relocate(const ActorContext &actor,RegionId target,std::optional<Vec> requested) {
    const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(target);
    if(!p || !p->entered || p->actor!=actor.actor || p->persistent.player.hp<=0 || int(target)<1 || int(target)>136) return {DomainStatus::InvalidActor,{}};
    if(!area) {const auto result=ports_.world.request(target);return {result?DomainStatus::Unavailable:result.status,{}};}
    const Vec desired=requested.value_or(area->definition.town?area->definition.spawn:
        area->definition.portalArrival.value_or(area->definition.exits.empty()?area->definition.spawn:area->definition.exits.front().arrival));
    if(!std::isfinite(desired.x) || !std::isfinite(desired.y) || desired.x<0 || desired.y<0 || desired.x>=area->definition.collision.width || desired.y>=area->definition.collision.height) return {DomainStatus::InvalidRequest,{}};
    // Administrative relocation needs the same room collision lease as the
    // normal warp path before searching for an arrival cell.
    ports_.world.activate(target,desired);
    const auto arrival=ports_.world.nativePlayerPosition(target,desired,actor.actor);
    if(!arrival || arrival->area!=target || (arrival->position-desired).length()>8) return {DomainStatus::Unavailable,{}};
    const Vec at=arrival->position;
    const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,target},{TravelFact{actor.player,actor.actor,p->area,target,area->generation,at,false}}});
    if(!sent) return {sent.status,{}};
    ports_.npc.close(actor.player);ports_.inventory.close(actor.player);state_.waypoints.erase(actor.player);cancel(actor.player);
    auto &player=ports_.players.players_.at(actor.player);player.route.clear();player.moving=false;player.area=target;player.position=at;ports_.areas.moveRoomUnit(player.actor,player.area,player.position);
    return {DomainStatus::Applied,std::monostate{}};
}
}
