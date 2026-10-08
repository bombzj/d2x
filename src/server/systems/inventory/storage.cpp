#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/items/gold_limits.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
namespace d2x::server::inventory {
bool System::storageAccess(PlayerId id) const {
    const auto access=state_.storage.find(id); const auto *p=ports_.players.find(id);
    if (access==state_.storage.end() || !p || !p->entered || p->persistent.player.hp<=0 || p->area!=access->second.area) return false;
    const auto *area=ports_.areas.find(p->area); if (!area || area->generation!=access->second.revision) return false;
    for (const auto &object : area->definition.objects) if (object.id==access->second.source && object.rule.stash) {
        const auto &r=object.rule; return interactionClear(area->definition.collision,p->position,{object.id,object.position,object.position,r.width,r.height,float(r.range),true});
    }
    return false;
}
DomainResult<> System::openStash(const ActorContext &actor,EntityId source) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if (!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || !area || area->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    const auto object=std::find_if(area->definition.objects.begin(),area->definition.objects.end(),[&](const auto &value){return value.id==source && value.rule.stash;});
    if(object==area->definition.objects.end()) return {DomainStatus::InvalidRequest,{}};
    const auto &r=object->rule;
    if(!interactionClear(area->definition.collision,p->position,{source,object->position,object->position,r.width,r.height,float(r.range),true})) return {DomainStatus::InvalidRequest,{}};
    auto access=state_.storage; access[actor.player]={source,ContainerKind::Stash,area->generation,actor.area};
    auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{UiFact{16}}}); if(!sent) return {sent.status,{}};
    state_.storage.swap(access); return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::storage(const ActorContext &actor,const Request &request) {
    if(std::holds_alternative<CloseStorage>(request.intent)) {
        auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{UiFact{17}}}); if(!sent) return {sent.status,{}};
        return close(actor.player);
    }
    if(!storageAccess(actor.player)) return {DomainStatus::InvalidRequest,{}};
    const auto &gold=std::get<GoldTransaction>(request.intent); if(!gold.amount || gold.action==GoldAction::Drop) return {DomainStatus::InvalidRequest,{}};
    const auto &p=*ports_.players.find(actor.player); auto record=p.persistent.player;
    if(gold.action==GoldAction::Deposit) {
        const auto limit=stashGoldLimit(unsigned(record.level)); if(record.bankGold>limit || gold.amount>record.gold || gold.amount>limit-record.bankGold) return {DomainStatus::Conflict,{}};
        record.gold-=gold.amount; record.bankGold+=gold.amount;
    } else { const auto limit=unsigned(record.level)*10000; if(record.gold>limit || gold.amount>record.bankGold || gold.amount>limit-record.gold) return {DomainStatus::Conflict,{}}; record.bankGold-=gold.amount; record.gold+=gold.amount; }
    auto plan=ports_.transactions.prepare(transactions::CharacterEdit{actor,p.inventoryRevision,p.characterRevision,std::move(record)}); if(!plan) return {plan.status,{}};
    return ports_.transactions.commit(std::move(*plan.value));
}
}
