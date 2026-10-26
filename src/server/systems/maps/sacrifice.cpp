#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/objects/system.hpp"
#include "world/interaction_geometry.hpp"
#include <stdexcept>

namespace d2x::server::maps {
DomainResult<> System::operateSacrifice(const ActorContext &actor,EntityId id) {
    synchronize();const auto object=ports_.objects.read().objects.find(id);
    if(state_.instance && state_.instance->controller==45 && object!=ports_.objects.read().objects.end() &&
       object->second.definition==621 && object->second.area==actor.area && object->second.mode==1) {
        const auto changed=ports_.objects.setNativeMode(id,2,actor.tick);if(!changed) return changed;
        state_.sacrificeUsers.erase(actor.player);return {DomainStatus::Applied,std::monostate{}};
    }
    if(!state_.instance || state_.instance->controller!=45 || state_.phase!=8 || object==ports_.objects.read().objects.end() ||
       object->second.definition!=621 || object->second.area!=actor.area || object->second.mode!=0) return {DomainStatus::InvalidRequest,{}};
    const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{NpcServiceFact{id,0}}});
    if(!sent) return {sent.status,{}};
    state_.sacrificeUsers.insert_or_assign(actor.player,id);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::closeSacrifice(const ActorContext &actor,EntityId id) {
    const auto user=state_.sacrificeUsers.find(actor.player);
    if(user==state_.sacrificeUsers.end() || user->second!=id) return {DomainStatus::Stale,{}};
    const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{NpcServiceFact{id,1}}});
    if(!sent) return {sent.status,{}};
    state_.sacrificeUsers.erase(user);return {DomainStatus::Applied,std::monostate{}};
}
bool System::sacrificeAccess(const ActorContext &actor,EntityId altar,ItemHandle item) const {
    const auto user=state_.sacrificeUsers.find(actor.player);const auto *player=ports_.players.find(actor.player);
    const auto *area=ports_.areas.find(actor.area);const auto object=ports_.objects.read().objects.find(altar);
    if(!state_.instance || state_.instance->controller!=45 || state_.phase!=8 || user==state_.sacrificeUsers.end() || user->second!=altar ||
       !player || !player->entered || player->actor!=actor.actor || player->area!=actor.area || player->persistent.player.hp<=0 ||
       !area || area->generation!=actor.areaGeneration || object==ports_.objects.read().objects.end() || object->second.area!=actor.area ||
       object->second.definition!=621 || object->second.mode!=0 || object->second.revision==UINT64_MAX) return false;
    const auto found=player->persistent.inventory.items.find(item.id);
    if(found==player->persistent.inventory.items.end() || found->second.revision!=item.revision ||
       found->second.location!=ItemLocation{ContainerLocation{player->persistent.containers.cursor,{}}}) return false;
    const auto &value=object->second;
    return interactionClear(area->definition.collision,player->position,
        {value.id,value.position,value.position,value.rule.width,value.rule.height,float(value.rule.range),true});
}
void System::completeSacrifice(const ActorContext &actor,EntityId altar) {
    if(state_.phase!=8) throw std::logic_error("Stale native sacrifice publication");
    state_.phase=9;state_.sacrificeUsers.erase(actor.player);
    if(!ports_.objects.setNativeMode(altar,1,actor.tick,true)) std::terminate();
}
}
