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
#include <algorithm>
#include <cmath>
namespace d2x::server::travel {
namespace {
bool seamClear(const Grid &grid, Vec a, Vec b, int side) {
    // Only the short normal segment across a verified reciprocal seam. Grid
    // collision resolves its footprint into the linked neighbour on each cell.
    const int lateral = int(std::floor(side % 2 ? a.y : a.x));
    const int begin = int(std::floor(side % 2 ? a.x : a.y)), end = int(std::floor(side % 2 ? b.x : b.y));
    for (int normal = std::min(begin, end); normal <= std::max(begin, end); ++normal)
        if (!grid.movementClear(side % 2 ? normal : lateral, side % 2 ? lateral : normal, playerMovement)) return false;
    return true;
}
}

std::optional<CommandStatus> System::walk(const ActorContext &actor, const MovementCommand &command) {
    cancel(actor.player);
    if (command.action != MovementAction::Move) return {};
    const auto *player = ports_.players.find(actor.player);
    if (!player || !player->entered || player->persistent.player.hp <= 0) return CommandStatus::InvalidBinding;
    const auto &source = ports_.areas.at(player->area);
    if (!std::isfinite(command.destination.x) || !std::isfinite(command.destination.y)) return CommandStatus::InvalidDestination;
    for (const auto &edge : source.definition.boundaries) {
        const Vec a = player->position, b = command.destination;
        const float an = edge.side % 2 ? a.x : a.y, bn = edge.side % 2 ? b.x : b.y;
        const bool positive = edge.side == 0 || edge.side == 3;
        if ((positive ? an >= edge.plane || bn < edge.plane : an < edge.plane || bn >= edge.plane) || an == bn) continue;
        const float fraction = (float(edge.plane) - an) / (bn - an);
        const Vec intersection = a + (b - a) * fraction;
        const float lateral = edge.side % 2 ? intersection.y : intersection.x;
        if (lateral < edge.start || lateral >= edge.end) continue;
        const auto *other = ports_.areas.find(edge.destination);
        if (!other) { ports_.world.requestArea(actor, edge.destination); return CommandStatus::Unavailable; }
        const Vec offset = source.definition.origin - other->definition.origin;
        const Vec target = b + offset;
        if (!other->definition.collision.walkable(target, playerMovement)) return CommandStatus::InvalidDestination;
        Vec approach = intersection;
        const float normal = float(edge.plane) + (positive ? -1.5f : 1.5f);
        if (edge.side % 2) approach.x = normal; else approach.y = normal;
        Vec arrival = intersection;
        if (edge.side % 2) arrival.x += positive ? 1.5f : -1.5f; else arrival.y += positive ? 1.5f : -1.5f;
        arrival = arrival + offset;
        if (!source.definition.collision.walkable(approach, playerMovement) || !other->definition.collision.walkable(arrival, playerMovement) ||
            !seamClear(source.definition.collision, approach, arrival - offset, edge.side)) return CommandStatus::NoRoute;
        auto route = source.definition.collision.path(a, approach, false, playerMovement);
        if (route.empty()) return CommandStatus::NoRoute;
        state_.transitions[actor.player] = {actor.area, edge.destination, 0, source.generation, actor.sequence, {}, approach, arrival, target, true, command.forceRun, false, edge.side, edge.plane};
        auto &mutablePlayer = ports_.players.players_.at(actor.player);
        mutablePlayer.route = std::move(route); mutablePlayer.routeRunning = mutablePlayer.running || command.forceRun;
        return CommandStatus::Applied;
    }
    return {};
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
    if ((found->position - player->position).length() > 50) return {DomainStatus::InvalidRequest, {}};
    auto route = area->definition.collision.path(player->position, found->arrival, false, playerMovement);
    if (route.empty()) return {DomainStatus::Unavailable, {}};
    const auto prepared = ports_.world.requestArea(actor, found->destination);
    if (!prepared) return {prepared.status, {}};
    state_.transitions[actor.player] = {actor.area, found->destination, *prepared.value, area->generation, actor.sequence, found->id, found->arrival, {}, {}, false, false, false, 0, 0};
    auto &mutablePlayer = ports_.players.players_.at(actor.player);
    mutablePlayer.route = std::move(route); mutablePlayer.routeRunning = mutablePlayer.running;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::teleport(const ActorContext &actor, PointTarget target, float manaCost,std::optional<SkillCharge> charge) {
    const auto *player = ports_.players.find(actor.player);
    if (!player) return {DomainStatus::InvalidActor, {}};
    const auto result = ports_.transactions.release(actor, player->characterRevision, manaCost, target,charge);
    if (result) cancel(actor.player);
    return result;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
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
        if (!player.route.empty()) { ++pending; continue; }
        if (!transition.crossing && (player.position - transition.approach).length() > .1f) { pending = state_.transitions.erase(pending); continue; }
        Vec arrival = transition.arrival;
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
        if (transition.walking) {
            transition.crossing = true;
            const Vec offset = source->definition.origin - destination->definition.origin;
            const Vec across = transition.arrival - offset;
            const Vec delta = across - player.position;
            const auto speed = player.runningNow ? player.totals.character.runSpeed : player.totals.character.walkSpeed;
            const Vec next = player.position + delta.unit() * std::min(delta.length(), speed * TickContext::seconds);
            if (!seamClear(source->definition.collision, player.position, next, transition.side)) {
                player.moving = false; pending = state_.transitions.erase(pending); continue;
            }
            const float normal = transition.side % 2 ? next.x : next.y;
            const bool crossed = transition.side == 0 || transition.side == 3 ? normal >= transition.plane : normal < transition.plane;
            if (!crossed) { player.position = next; player.look = delta.unit(); player.moving = true; ++pending; continue; }
            arrival = next + offset;
        }
        if(transition.kind==Kind::SpecialPortal && (!ports_.quests.allowsTravel(player.persistent.player,transition.from,transition.to) || std::none_of(state_.specialPortals.begin(),state_.specialPortals.end(),[&](const auto &entry){return entry.second.fieldId==transition.source || entry.second.townId==transition.source;}))) {pending=state_.transitions.erase(pending);continue;}
        if (transition.kind==Kind::Portal) {
            const auto portal=state_.portals.find(player.player);
            if(portal==state_.portals.end() || (portal->second.fieldId!=transition.source && portal->second.townId!=transition.source)) { pending=state_.transitions.erase(pending); continue; }
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
            const auto point=destination->definition.collision.nativeSpawn(*marker,50,playerMovement);
            if(!point) {pending=state_.transitions.erase(pending);continue;}arrival=*point;
        }
        if (!transition.walking && transition.kind==Kind::Exit) {
            const AreaExit *back = nullptr;
            for (const auto &exit : destination->definition.exits) if (exit.destination == transition.from) {
                if (back) { back = nullptr; break; } back = &exit;
            }
            if (!back) { pending = state_.transitions.erase(pending); continue; }
            arrival = back->arrival; exitWalk = back->position + back->exitWalk;
        }
        if (!destination->definition.collision.walkable(arrival, playerMovement)) { pending = state_.transitions.erase(pending); continue; }
        std::deque<Vec> route;
        if (transition.walking || exitWalk) route = destination->definition.collision.path(arrival, exitWalk.value_or(transition.destination), true, playerMovement);
        EventBatch event{0, tick.tick, {}, {AudienceKind::Player, player.player, transition.to},
            {TravelFact{player.player, player.actor, transition.from, transition.to, destination->generation, arrival, transition.walking}}};
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
            edit.facts.emplace_back(QuestFact{player.player,edit.player,player.persistent.difficulty,0});
            edit.facts.emplace_back(TravelFact{player.player,player.actor,transition.from,transition.to,destination->generation,arrival,false});
            auto plan=ports_.transactions.prepare(std::move(edit));
            if(!plan || !ports_.transactions.commit(std::move(*plan.value))) {++pending;continue;}
        } else if (!ports_.events.publish(std::move(event))) { ++pending; continue; }
        ports_.npc.close(player.player); ports_.inventory.close(player.player); state_.waypoints.erase(player.player);
        if(transition.kind==Kind::Portal) { const auto portal=state_.portals.find(player.player); if(portal!=state_.portals.end() && portal->second.town==transition.from) state_.portals.erase(portal); }
        player.area = transition.to; player.position = arrival; player.route = std::move(route);
        player.routeRunning = player.running || transition.run; player.moving = transition.walking;
        if(transition.kind==Kind::Waypoint) ports_.objects.onWaypointArrival(transition.to,tick);
        pending = state_.transitions.erase(pending);
    }
    return state_.transitions.empty() ? StepStatus::Complete : StepStatus::Blocked;
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
    auto &player=ports_.players.players_.at(actor.player);player.route.clear();player.moving=false;player.area=target;player.position=at;
    return {DomainStatus::Applied,std::monostate{}};
}
}
