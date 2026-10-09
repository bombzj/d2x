#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include <cmath>
namespace d2x::server::transactions {
DomainResult<> System::release(const ActorContext &actor, uint64_t expected, float cost, std::optional<PointTarget> relocation, std::optional<SkillCharge> charge) {
    const auto found = ports_.players.players_.find(actor.player);
    if (found == ports_.players.players_.end()) return {DomainStatus::InvalidActor, {}};
    auto &player = found->second;
    const auto *area = ports_.areas.find(actor.area);
    if (!player.entered || player.actor != actor.actor || player.area != actor.area || player.persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    if (!area || area->generation != actor.areaGeneration || expected != player.characterRevision) return {DomainStatus::Stale, {}};
    if (expected == UINT64_MAX) return {DomainStatus::Capacity, {}};
    if (!std::isfinite(cost) || cost < 0 || player.persistent.player.mana < cost) return {DomainStatus::Unavailable, {}};
    if (relocation && (relocation->area != player.area || relocation->generation != area->generation || !area->definition.teleportAllowed ||
        !area->definition.collision.walkable(relocation->position, playerMovement))) return {DomainStatus::Unavailable, {}};
    if(charge) {
        CharacterEdit edit{actor,player.inventoryRevision,expected,player.persistent.player};edit.player.mana-=cost;edit.charge=charge;
        if(relocation) edit.publicFacts.emplace_back(RepositionFact{player.actor,player.area,relocation->position});
        auto plan=prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto result=commit(std::move(*plan.value));
        if(result && relocation) {player.position=relocation->position;player.route.clear();player.moving=false;}
        return result;
    }
    const float mana = player.persistent.player.mana - cost;
    std::vector<EventBatch> events{{0, actor.tick, {}, {AudienceKind::Player, player.player, player.area}, {ManaFact{player.actor, mana}}}};
    if (relocation) events.push_back({0, actor.tick, {}, {AudienceKind::Area, {}, player.area},
        {RepositionFact{player.actor, player.area, relocation->position}}});
    auto result = ports_.events.publishGroup(std::move(events));
    if (!result) return {result.status, {}};
    player.persistent.player.mana = mana; ++player.characterRevision; player.totals.sourceRevision = player.characterRevision;
    if (relocation) { player.position = relocation->position; player.route.clear(); player.moving = false; }
    return {DomainStatus::Applied, std::monostate{}};
}
}

namespace d2x::server::transactions {
DomainResult<> System::resources(const ActorContext &actor, uint64_t expected, float life, float mana, float stamina) {
    const auto found = ports_.players.players_.find(actor.player);
    if (found == ports_.players.players_.end()) return {DomainStatus::InvalidActor, {}};
    auto &player = found->second; const auto &totals = player.totals.character;
    const auto *area = ports_.areas.find(actor.area);
    if (!player.entered || player.actor != actor.actor || player.area != actor.area || player.persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    if (!area || area->generation != actor.areaGeneration || player.characterRevision != expected) return {DomainStatus::Stale, {}};
    if (expected == UINT64_MAX) return {DomainStatus::Capacity, {}};
    if (!std::isfinite(life) || !std::isfinite(mana) || !std::isfinite(stamina) || life <= 0 || life > totals.maxLife || mana < 0 || mana > totals.maxMana || stamina < 0 || stamina > totals.maxStamina) return {DomainStatus::InvalidRequest, {}};
    auto record = player.persistent.player; record.hp = life; record.mana = mana; record.stamina = stamina;
    if (record.hp == player.persistent.player.hp && record.mana == player.persistent.player.mana && record.stamina == player.persistent.player.stamina) return {DomainStatus::Applied, std::monostate{}};
    const auto result = ports_.events.publish({0, actor.tick, {}, {AudienceKind::Player, player.player, player.area},
        {CharacterFact{player.persistent.player, record, player.totals, player.totals}}});
    if (!result) return {result.status, {}};
    player.persistent.player.hp = life; player.persistent.player.mana = mana; player.persistent.player.stamina = stamina;
    ++player.characterRevision; player.totals.sourceRevision = player.characterRevision;
    return {DomainStatus::Applied, std::monostate{}};
}
}
