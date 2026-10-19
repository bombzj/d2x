#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/items/gold_limits.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
namespace d2x::server::inventory {
bool System::stashAccess(PlayerId id, const Access &access) const {
    const auto *p=ports_.players.find(id);
    if (!p || !p->entered || p->persistent.player.hp<=0 || p->area!=access.area) return false;
    const auto *area=ports_.areas.find(p->area); if (!area || area->generation!=access.revision) return false;
    for (const auto &object : area->definition.objects) if (object.id==access.source && object.rule.stash) {
        const auto &r=object.rule; if(access.remoteRange) {const int x=int(object.position.x)-int(p->position.x),y=int(object.position.y)-int(p->position.y);return x*x+y*y<=*access.remoteRange * *access.remoteRange;} return interactionClear(area->definition.collision,p->position,{object.id,object.position,object.position,r.width,r.height,float(r.range),true});
    }
    return false;
}
bool System::storageAccess(PlayerId id) const {
    const auto access=state_.storage.find(id);
    return access!=state_.storage.end() && access->second.kind==ContainerKind::Stash && stashAccess(id,access->second);
}
bool System::cubeAccess(PlayerId id) const {
    const auto access=state_.storage.find(id);const auto *p=ports_.players.find(id);
    if(!ports_.definitions || access==state_.storage.end() || access->second.kind!=ContainerKind::Cube || !p || !p->entered || p->persistent.player.hp<=0 || p->area!=access->second.area) return false;
    const auto *area=ports_.areas.find(p->area);if(!area || area->generation!=access->second.revision) return false;
    const auto item=p->persistent.inventory.items.find(access->second.source);
    if(item==p->persistent.inventory.items.end()) return false;
    const auto *at=std::get_if<ContainerLocation>(&item->second.location);const auto *def=ports_.definitions->find(item->second.definition);
    if (!at || !def || !def->opensCube) return false;
    if (at->container==p->persistent.containers.backpack) return true;
    return at->container==p->persistent.containers.stash && access->second.parentStash &&
        stashAccess(id,{access->second.parentStash,ContainerKind::Stash,access->second.revision,
            access->second.area,access->second.remoteRange});
}
DomainResult<> System::openCube(const ActorContext &actor,ItemHandle handle) {
    const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!ports_.definitions || !p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || !area || area->generation!=actor.areaGeneration || p->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
    const auto item=p->persistent.inventory.items.find(handle.id);
    if(item==p->persistent.inventory.items.end() || item->second.revision!=handle.revision) return {DomainStatus::Stale,{}};
    const auto *at=std::get_if<ContainerLocation>(&item->second.location);const auto *def=ports_.definitions->find(item->second.definition);
    const bool stash=storageAccess(actor.player);
    if(!at || (at->container!=p->persistent.containers.backpack &&
        !(stash && at->container==p->persistent.containers.stash)) || !def || !def->opensCube) return {DomainStatus::InvalidRequest,{}};
    auto next=state_.storage;
    Access access{handle.id,ContainerKind::Cube,area->generation,actor.area,{}};
    if (at->container==p->persistent.containers.stash) {
        access.parentStash=state_.storage.at(actor.player).source;
        access.remoteRange=state_.storage.at(actor.player).remoteRange;
    }
    next[actor.player]=access;
    std::vector<DomainFact> facts;if(stash) facts.emplace_back(UiFact{17});facts.emplace_back(UiFact{21});
    auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},std::move(facts)});
    if(!sent) return {sent.status,{}};
    state_.storage.swap(next);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::openStash(const ActorContext &actor,EntityId source,std::optional<int> remoteRange) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if (!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || !area || area->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    const auto object=std::find_if(area->definition.objects.begin(),area->definition.objects.end(),[&](const auto &value){return value.id==source && value.rule.stash;});
    if(object==area->definition.objects.end()) return {DomainStatus::InvalidRequest,{}};
    const auto &r=object->rule;
    const int x=int(object->position.x)-int(p->position.x),y=int(object->position.y)-int(p->position.y);
    if(remoteRange ? x*x+y*y>*remoteRange * *remoteRange : !interactionClear(area->definition.collision,p->position,{source,object->position,object->position,r.width,r.height,float(r.range),true})) return {DomainStatus::InvalidRequest,{}};
    auto access=state_.storage; access[actor.player]={source,ContainerKind::Stash,area->generation,actor.area,remoteRange};
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
