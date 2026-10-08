#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include <cmath>
namespace d2x::server::transactions {
DomainResult<> System::release(const ActorContext &actor, uint64_t expected, float cost, std::optional<PointTarget> relocation) {
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
