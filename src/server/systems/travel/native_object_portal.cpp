#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/world/system.hpp"
#include "world/interaction_geometry.hpp"

namespace d2x::server::travel {
DomainResult<> System::useNativeObjectPortal(const ActorContext &actor,EntityId id,std::optional<int> remoteRange) {
    const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!player || !area || !player->entered || player->actor!=actor.actor || player->area!=actor.area ||
        player->persistent.player.hp<=0 || area->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    const auto source=ports_.objects.read().objects.find(id);
    if(source==ports_.objects.read().objects.end() || source->second.area!=actor.area || source->second.mode!=2) return {DomainStatus::Stale,{}};
    const auto &object=source->second;
    const auto linked=ports_.objects.read().objects.find(object.nativePortalOwner);
    if(linked==ports_.objects.read().objects.end() || int(linked->second.area)!=object.portalDestination) return {DomainStatus::Unavailable,{}};
    const auto &target=linked->second;const auto *destination=ports_.areas.find(target.area);
    if(!destination || destination->definition.act!=area->definition.act ||
        !ports_.quests.allowsTravel(player->persistent.player,actor.area,target.area)) return {DomainStatus::InvalidRequest,{}};
    const InteractionTarget access{id,object.position,object.position,object.rule.width,object.rule.height,float(object.rule.range),true};
    if(!remoteRange && !interactionClear(area->definition.collision,player->position,access)) return {DomainStatus::InvalidRequest,{}};
    if(remoteRange && (object.position-player->position).length()>float(*remoteRange)) return {DomainStatus::InvalidRequest,{}};
    Transition next{actor.area,target.area,0,actor.areaGeneration,actor.sequence,id,player->position,target.position};
    next.kind=Kind::NativeObjectPortal;next.arrivalGeneration=destination->generation;
    state_.transitions.insert_or_assign(actor.player,next);
    auto &moving=ports_.players.players_.at(actor.player);moving.locomotionSequence=actor.sequence;moving.route.clear();moving.moving=false;
    ports_.world.activate(target.area,target.position);
    return {DomainStatus::Applied,std::monostate{}};
}
}
