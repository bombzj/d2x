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
        route = playerRoute(area->definition.collision,player->position,found->arrival);
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
DomainResult<> System::teleport(const ActorContext &actor, PointTarget target, Fixed8 manaCost,std::optional<SkillCharge> charge) {
    const auto *player = ports_.players.find(actor.player);
    if (!player) return {DomainStatus::InvalidActor, {}};
    if (target.area == actor.area) ports_.world.activate(target.area, target.position + Vec{.5f,.5f});
    const auto result = ports_.transactions.release(actor, player->characterRevision, manaCost, target,charge);
    if (result) cancel(actor.player);
    return result;
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
        if (transition.kind == Kind::Waypoint && targetDefinition.waypointAnchor)
            ports_.world.activate(transition.to, *targetDefinition.waypointAnchor);
        else if (transition.kind == Kind::Npc) ports_.world.activate(transition.to, targetDefinition.spawn);
        else if (transition.kind == Kind::Exit) {
            for (const auto &exit : targetDefinition.exits) if (exit.destination == transition.from)
                ports_.world.activate(transition.to, exit.arrival);
        } else if (transition.kind == Kind::QuestObject) {
            if (targetDefinition.questArrival) ports_.world.activate(transition.to, *targetDefinition.questArrival);
            if (targetDefinition.portalArrival) ports_.world.activate(transition.to, *targetDefinition.portalArrival);
            for (const auto &exit : targetDefinition.exits) if (exit.destination == transition.from)
                ports_.world.activate(transition.to, exit.arrival);
            if (targetDefinition.town) ports_.world.activate(transition.to, targetDefinition.spawn);
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
            arrival=destination->definition.spawn;
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
            if(!definition.waypointAnchor || !nativeWaypointIndex(definition.waypointIndex) ||
                player.rules.character->waypointIndices.at(transition.to)!=definition.waypointIndex) {pending=state_.transitions.erase(pending);continue;}
            const auto spawn=definition.collision.nativeSpawn(*definition.waypointAnchor,50,playerMovement);
            if(!spawn) {pending=state_.transitions.erase(pending);continue;}
            arrival=*spawn;
        }
        if(transition.kind==Kind::QuestObject) {
            const auto object=std::find_if(source->definition.objects.begin(),source->definition.objects.end(),[&](const auto &o){return o.id==transition.source;});
            if(object==source->definition.objects.end() || !ports_.quests.allowsTravel(player.persistent.player,transition.from,transition.to)) {pending=state_.transitions.erase(pending);continue;}
            const auto counterpart=std::find_if(destination->definition.objects.begin(),destination->definition.objects.end(),[&](const auto &o){return o.rule.operation==object->rule.operation;});
            auto marker=destination->definition.questArrival;
            if(!marker && counterpart!=destination->definition.objects.end() && source->definition.act==4) marker=counterpart->position;
            if(!marker && source->definition.act>=2) marker=destination->definition.portalArrival;
            if(!marker && (int(transition.to)==103 || int(transition.to)==109)) marker=destination->definition.spawn;
            if(!marker && int(transition.from)==73 && counterpart!=destination->definition.objects.end()) marker=counterpart->position;
            if(!marker) for(const auto &warp:destination->definition.exits) if(warp.destination==transition.from) {marker=warp.arrival;break;}
            if(!marker) {pending=state_.transitions.erase(pending);continue;}
            ports_.world.activate(transition.to, *marker);
            const auto point=destination->definition.collision.nativeSpawn(*marker,50,playerMovement);
            if(!point) {pending=state_.transitions.erase(pending);continue;}arrival=*point;
        }
        if (transition.kind==Kind::Exit) {
            const AreaExit *back = nullptr;
            for (const auto &exit : destination->definition.exits) if (exit.destination == transition.from) {
                if (back) { back = nullptr; break; } back = &exit;
            }
            if (!back) { pending = state_.transitions.erase(pending); continue; }
            arrival = back->arrival; exitWalk = back->position + back->exitWalk;
        }
        if (!destination->definition.collision.walkable(arrival, playerMovement)) { pending = state_.transitions.erase(pending); continue; }
        std::deque<Vec> route;
        if (exitWalk) route = playerRoute(destination->definition.collision,arrival,*exitWalk);
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
        if(transition.kind==Kind::Waypoint) ports_.objects.onWaypointArrival(transition.to,tick);
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
    const Vec at=area->definition.collision.nearest(desired,playerMovement);
    if(!area->definition.collision.walkable(at,playerMovement) || (at-desired).length()>8) return {DomainStatus::Unavailable,{}};
    const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,target},{TravelFact{actor.player,actor.actor,p->area,target,area->generation,at,false}}});
    if(!sent) return {sent.status,{}};
    ports_.npc.close(actor.player);ports_.inventory.close(actor.player);state_.waypoints.erase(actor.player);cancel(actor.player);
    auto &player=ports_.players.players_.at(actor.player);player.route.clear();player.moving=false;player.area=target;player.position=at;ports_.areas.moveRoomUnit(player.actor,player.area,player.position);
    return {DomainStatus::Applied,std::monostate{}};
}
}
