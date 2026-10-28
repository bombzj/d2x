#include "system.hpp"
#include <set>
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/actions/state.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/effects/state.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include <stdexcept>
namespace d2x::server::transactions {
void System::storeLastAttacker(EntityId target,EntityId source) {
    for(auto &[id,player]:ports_.players.players_) {
        (void)id;
        if(player.actor!=target || !player.entered || player.persistent.player.hp<=0 || player.lastAttacker==source) continue;
        if(player.characterRevision==UINT64_MAX) throw std::overflow_error("Player last-attacker revision exhausted");
        player.lastAttacker=source;++player.characterRevision;player.totals.sourceRevision=player.characterRevision;
        return;
    }
}
DomainResult<> System::incrementPd2Counter(const ActorContext &actor,size_t index,std::optional<size_t> second,uint32_t bossFlag) {
    const auto found=ports_.players.players_.find(actor.player);
    if(found==ports_.players.players_.end()) return {DomainStatus::InvalidActor,{}};
    auto &player=found->second;const auto *area=ports_.areas.find(actor.area);
    if(!player.entered || player.actor!=actor.actor || player.area!=actor.area || !area || area->generation!=actor.areaGeneration ||
       !player.rules.character || !player.rules.character->maps) return {DomainStatus::InvalidActor,{}};
    if(index>=player.pd2SessionCounters.size() || (second && *second>=player.pd2SessionCounters.size())) return {DomainStatus::InvalidRequest,{}};
    if(player.characterRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    if(!player.persistent.player.pd2Counters) player.persistent.player.pd2Counters.emplace();
    auto &total=player.persistent.player.pd2Counters->at(index);
    total=uint16_t(total+1);player.pd2SessionCounters[index]=uint16_t(player.pd2SessionCounters[index]+1);
    if(second) {
        auto &additional=player.persistent.player.pd2Counters->at(*second);additional=uint16_t(additional+1);
        player.pd2SessionCounters[*second]=uint16_t(player.pd2SessionCounters[*second]+1);
    }
    if(bossFlag) {
        player.persistent.player.pd2BossKills=player.persistent.player.pd2BossKills.value_or(0)|bossFlag;
        player.pd2AchievementSelection|=bossFlag==1?8u:bossFlag==2?16u:bossFlag==8?256u:0u;
    }
    ++player.characterRevision;player.totals.sourceRevision=player.characterRevision;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::consumeRoomRandom(const RoomRandomLoan &loan) {
    if(!ports_.areas.validateRoomRandom(loan)) return {DomainStatus::Stale,{}};
    ports_.areas.publishRoomRandom(loan);return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::consumeRoomRandom(std::span<const std::shared_ptr<const RoomRandomLoan>> loans,std::optional<EventBatch> facts) {
    std::set<RoomView> rooms;
    for(const auto &loan:loans) {
        if(!loan || !ports_.areas.validateRoomRandom(*loan)) return {DomainStatus::Stale,{}};
        if(!rooms.insert(loan->room).second) return {DomainStatus::InvalidRequest,{}};
    }
    if(facts) {
        const auto sent=ports_.events.publish(std::move(*facts));if(!sent) return {sent.status,{}};
    }
    for(const auto &loan:loans) ports_.areas.publishRoomRandom(*loan);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::consumeRandom(const ActorContext &actor,uint64_t expected,uint64_t next) {
    const auto found=ports_.players.players_.find(actor.player);
    if(found==ports_.players.players_.end()) return {DomainStatus::InvalidActor,{}};
    auto &player=found->second;const auto *area=ports_.areas.find(actor.area);
    if(player.actor!=actor.actor || player.area!=actor.area || !area || area->generation!=actor.areaGeneration || player.combatRandom!=expected)
        return {DomainStatus::Stale,{}};
    if(next!=expected) {
        if(player.characterRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        player.combatRandom=next;++player.characterRevision;player.totals.sourceRevision=player.characterRevision;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::release(const ActorContext &actor, uint64_t expected, Fixed8 cost, std::optional<PointTarget> relocation,
    std::optional<SkillCharge> charge,std::vector<DomainFact> publicFacts,std::optional<UnitRandomEdit> random,int skill) {
    const auto found = ports_.players.players_.find(actor.player);
    if (found == ports_.players.players_.end()) return {DomainStatus::InvalidActor, {}};
    auto &player = found->second;
    const auto *area = ports_.areas.find(actor.area);
    if (!player.entered || player.actor != actor.actor || player.area != actor.area || player.persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    if (!area || area->generation != actor.areaGeneration || expected != player.characterRevision) return {DomainStatus::Stale, {}};
    if(random && player.combatRandom!=random->expected) return {DomainStatus::Stale,{}};
    if (expected == UINT64_MAX) return {DomainStatus::Capacity, {}};
    if (cost < 0 || player.persistent.player.mana < cost) return {DomainStatus::Unavailable, {}};
    const auto *destination=relocation?ports_.areas.find(relocation->area):nullptr;
    if(relocation) {
        if(!destination || relocation->generation!=destination->generation || !area->definition.teleportAllowed ||
            destination->definition.act!=area->definition.act ||
            !ports_.areas.loaded(relocation->area,relocation->position+Vec{.5f,.5f}) ||
            !ports_.spatial.navigationGrid(relocation->area,player.actor).walkable(relocation->position+Vec{.5f,.5f},playerMovement)) return {DomainStatus::Unavailable,{}};
        if(relocation->area!=actor.area) {
            Vec origin;const auto navigation=area->definition.collision.linkedNavigation(origin);
            if(std::none_of(navigation.navigationSources.begin(),navigation.navigationSources.end(),
                [&](const auto &entry){return entry.grid==&destination->definition.collision;})) return {DomainStatus::Unavailable,{}};
        }
        if(player.teleportRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    }
    const auto relocated=[&] {
        if(!relocation) return;
        player.area=relocation->area;player.position=relocation->position;
        player.lastTeleportPosition=player.position+destination->definition.origin;
        ports_.areas.moveRoomUnit(player.actor,player.area,player.position);
        player.route.clear();player.moving=false;player.runningNow=false;player.pathStep.reset();
        player.movementTarget.reset();player.movementArrival.reset();
        player.routeDestination=player.position;player.routeObstacles=destination->definition.collision.obstacleRevision;
        ++player.teleportRevision;
    };
    const auto travelFact=[&] {
        return TravelFact{player.player,player.actor,actor.area,relocation->area,destination->generation,relocation->position,false};
    };
    const bool bloodMana=(skill>=0 || cost>0) && player.rules.character && player.transient.effects &&
        player.transient.effects->hasState(player.rules.character->bloodManaState,actor.tick);
    const bool mapTravel=relocation && relocation->area!=actor.area && player.rules.character && player.rules.character->maps;
    if(charge || bloodMana || mapTravel) {
        CharacterEdit edit{actor,player.inventoryRevision,expected,player.persistent.player};edit.player.mana-=cost;edit.charge=charge;
        if(skill<0) if(const auto used=ports_.actions.usedSkill(actor.actor)) skill=used->skill;
        if(bloodMana && !charge && skill<0) return {DomainStatus::Unavailable,{}};
        if(charge || bloodMana) edit.skillPayment=SkillManaPayment{cost,skill};
        if(mapTravel) edit.environmentRegion=relocation->area;
        edit.random=random;
        edit.publicFacts=std::move(publicFacts);
        if(relocation) {
            if(relocation->area!=actor.area) edit.facts.emplace_back(travelFact());
            edit.publicFacts.emplace_back(RepositionFact{player.actor,relocation->area,relocation->position,relocation->area==actor.area});
        }
        auto plan=prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto result=commit(std::move(*plan.value));
        if(result) relocated();
        return result;
    }
    const auto mana = player.persistent.player.mana - cost;
    DomainFact resourceFact=ManaFact{player.actor,mana};
    if(random) {
        // This narrow release replaces a CharacterEdit carrying the unit's
        // random candidate. Retain its attribute-delta encoding, including
        // suppression when a free release leaves mana unchanged.
        const auto &before=player.persistent.player;
        resourceFact=ResourcesFact{player.actor,{before.hp,before.mana,before.stamina},{before.hp,mana,before.stamina}};
    }
    std::vector<EventBatch> events{{0, actor.tick, {}, {AudienceKind::Player, player.player, player.area}, {std::move(resourceFact)}}};
    if(relocation) {
        if(relocation->area!=actor.area) events.front().facts.emplace_back(travelFact());
        publicFacts.emplace_back(RepositionFact{player.actor,relocation->area,relocation->position,relocation->area==actor.area});
    }
    if (!publicFacts.empty()) events.push_back({0, actor.tick, {}, {AudienceKind::Area, {}, player.area},std::move(publicFacts)});
    auto result = ports_.events.publishGroup(std::move(events));
    if (!result) return {result.status, {}};
    player.persistent.player.mana = mana; ++player.characterRevision; player.totals.sourceRevision = player.characterRevision;
    if(random) player.combatRandom=random->next;
    relocated();
    return {DomainStatus::Applied, std::monostate{}};
}
}

namespace d2x::server::transactions {
DomainResult<> System::resources(const ActorContext &actor, uint64_t expected, Fixed8 life, Fixed8 mana, Fixed8 stamina,
                                std::vector<DomainFact> publicFacts) {
    const auto found = ports_.players.players_.find(actor.player);
    if (found == ports_.players.players_.end()) return {DomainStatus::InvalidActor, {}};
    auto &player = found->second; const auto &totals = player.totals.character;
    const auto *area = ports_.areas.find(actor.area);
    if (!player.entered || player.actor != actor.actor || player.area != actor.area || player.persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    if (!area || area->generation != actor.areaGeneration || player.characterRevision != expected) return {DomainStatus::Stale, {}};
    const auto &current=player.persistent.player;
    // A narrow resource write may preserve or reduce an admitted surplus,
    // but cannot create a new value above both the old value and the maximum.
    if (life <= 0 || life > std::max(totals.maxLifeFixed,current.hp) ||
        mana < 0 || mana > std::max(totals.maxManaFixed,current.mana) ||
        stamina < 0 || stamina > std::max(totals.maxStaminaFixed,current.stamina)) return {DomainStatus::InvalidRequest, {}};
    const bool changed = life != player.persistent.player.hp || mana != player.persistent.player.mana || stamina != player.persistent.player.stamina;
    if (!changed && publicFacts.empty()) return {DomainStatus::Applied, std::monostate{}};
    if (expected == UINT64_MAX) return {DomainStatus::Capacity, {}};
    std::vector<EventBatch> events;
    if (changed) {
        const auto &before=player.persistent.player;
        events.push_back({0, actor.tick, {}, {AudienceKind::Player, player.player, player.area},
            {ResourcesFact{player.actor,{before.hp,before.mana,before.stamina},{life,mana,stamina}}}});
    }
    if (!publicFacts.empty()) events.push_back({0, actor.tick, {}, {AudienceKind::Area, {}, player.area}, std::move(publicFacts)});
    const auto result = ports_.events.publishGroup(std::move(events));
    if (!result) return {result.status, {}};
    if (changed) {
        player.persistent.player.hp = life; player.persistent.player.mana = mana; player.persistent.player.stamina = stamina;
        ports_.players.refreshCollision(player.player,ports_.areas);
        ++player.characterRevision; player.totals.sourceRevision = player.characterRevision;
    }
    return {DomainStatus::Applied, std::monostate{}};
}
}
