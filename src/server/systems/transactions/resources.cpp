#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include <cmath>
#include <utility>
#include <vector>
namespace d2x::server::transactions {
DomainResult<> System::consumeRoomRandom(const RoomRandomLoan &loan) {
    if(!ports_.areas.validateRoomRandom(loan)) return {DomainStatus::Stale,{}};
    ports_.areas.publishRoomRandom(loan);return {DomainStatus::Applied,std::monostate{}};
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
    std::optional<SkillCharge> charge,std::vector<DomainFact> publicFacts,std::optional<UnitRandomEdit> random) {
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
    if (relocation && (relocation->area != player.area || relocation->generation != area->generation || !area->definition.teleportAllowed ||
        !area->definition.collision.walkable(relocation->position, playerMovement))) return {DomainStatus::Unavailable, {}};
    if(charge) {
        CharacterEdit edit{actor,player.inventoryRevision,expected,player.persistent.player};edit.player.mana-=cost;edit.charge=charge;
        edit.random=random;
        edit.publicFacts=std::move(publicFacts);
        if(relocation) edit.publicFacts.emplace_back(RepositionFact{player.actor,player.area,relocation->position});
        auto plan=prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto result=commit(std::move(*plan.value));
        if(result && relocation) {player.position=relocation->position;player.lastTeleportPosition=player.position+area->definition.origin;ports_.areas.moveRoomUnit(player.actor,player.area,player.position);player.route.clear();player.moving=false;++player.teleportRevision;}
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
    if (relocation) publicFacts.emplace_back(RepositionFact{player.actor, player.area, relocation->position});
    if (!publicFacts.empty()) events.push_back({0, actor.tick, {}, {AudienceKind::Area, {}, player.area},std::move(publicFacts)});
    auto result = ports_.events.publishGroup(std::move(events));
    if (!result) return {result.status, {}};
    player.persistent.player.mana = mana; ++player.characterRevision; player.totals.sourceRevision = player.characterRevision;
    if(random) player.combatRandom=random->next;
    if (relocation) { player.position = relocation->position;player.lastTeleportPosition=player.position+area->definition.origin;ports_.areas.moveRoomUnit(player.actor,player.area,player.position); player.route.clear(); player.moving = false; ++player.teleportRevision; }
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
    if (life <= 0 || life > totals.maxLifeFixed || mana < 0 || mana > totals.maxManaFixed || stamina < 0 || stamina > totals.maxStaminaFixed) return {DomainStatus::InvalidRequest, {}};
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
        ++player.characterRevision; player.totals.sourceRevision = player.characterRevision;
    }
    return {DomainStatus::Applied, std::monostate{}};
}
}
