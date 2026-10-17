#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include <utility>
namespace d2x::server::missiles {
DomainResult<DirectContact> System::directContact(const Spawn &request,std::vector<EntityId> targets) {
    if(!request.weapon || !request.skill.weapon) return {DomainStatus::InvalidRequest,{}};
    const auto *area=ports_.areas.find(request.actor.area);
    if(!area || area->generation!=request.actor.areaGeneration || area->definition.town) return {DomainStatus::InvalidActor,{}};
    if(const auto *source=ports_.monsters.find(request.actor.actor)) {
        if(!request.free || source->area!=request.actor.area || (!source->alive() && !request.deathTrigger)) return {DomainStatus::InvalidActor,{}};
    } else {
        const auto *player=ports_.players.find(request.actor.player);
        if(!player || !player->entered || player->actor!=request.actor.actor || player->area!=request.actor.area ||
           (player->persistent.player.hp<=0 && !request.deathTrigger)) return {DomainStatus::InvalidActor,{}};
    }
    return nativeWeaponContact(request,std::move(targets));
}
}
