#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include <cmath>
#include <algorithm>
namespace d2x::server::items {
bool System::reachable(GroundLocation target, Vec from) const {
    const auto *area = ports_.areas.find(target.region);
    return area && area->definition.collision.collisionSegment(from, target.position, 0x0801);
}
std::optional<Vec> System::placement(GroundLocation origin, const InventoryState &world) const {
    const auto *area = ports_.areas.find(origin.region);
    if (!area || !std::isfinite(origin.position.x) || !std::isfinite(origin.position.y)) return {};
    for (int radius = 0; radius <= 4; ++radius)
        for (int y = -radius; y <= radius; ++y) for (int x = -radius; x <= radius; ++x) {
            if (std::max(std::abs(x), std::abs(y)) != radius) continue;
            const Vec point{std::floor(origin.position.x) + float(x), std::floor(origin.position.y) + float(y)};
            if (!area->definition.collision.walkable(point, playerMovement)) continue;
            bool occupied = false;
            for (const auto &[id, item] : world.items) {
                (void)id;
                const auto *at = std::get_if<GroundLocation>(&item.location);
                if (at && at->region == origin.region && std::floor(at->position.x) == point.x && std::floor(at->position.y) == point.y) { occupied = true; break; }
            }
            if (!occupied) return point;
        }
    return {};
}
DomainResult<State> System::prepare(PreparedBatch batch, GroundLocation origin) {
    if (!batch.equipment || batch.items.size() > 64 || state_.world.items.size() + batch.items.size() > 4096 || state_.revision == UINT64_MAX)
        return {DomainStatus::Capacity, {}};
    auto next = state_;
    for (auto &item : batch.items) {
        const auto position = placement(origin, next.world);
        if (!position) return {DomainStatus::Conflict, {}};
        auto properties = batch.equipment->items.at(item.id);
        if (ports_.ids.cursor() > UINT32_MAX) return {DomainStatus::Capacity, {}};
        item.id = ports_.ids.allocate(); item.location = GroundLocation{origin.region, *position};
        next.equipment.items.emplace(item.id, std::move(properties));
        next.world.items.emplace(item.id, std::move(item));
    }
    for (const auto &piece : batch.equipment->sets)
        if (std::none_of(next.equipment.sets.begin(), next.equipment.sets.end(), [&](const auto &v) { return v.row == piece.row; })) next.equipment.sets.push_back(piece);
    next.equipment.setBonuses.insert(batch.equipment->setBonuses.begin(), batch.equipment->setBonuses.end());
    ++next.revision; return {DomainStatus::Applied, std::move(next)};
}
DomainResult<> System::install(PreparedBatch batch, GroundLocation origin) {
    auto next = prepare(std::move(batch), origin); if (!next) return {next.status, {}};
    std::swap(state_, *next.value); return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<ItemInstance> System::resolve(const Address &address) const {
    const InventoryState *inventory = &state_.world;
    if (address.character) {
        const auto *player = ports_.players.find(*address.character);
        if (!player) return {DomainStatus::InvalidActor, {}};
        inventory = &player->persistent.inventory;
    }
    const auto found = inventory->items.find(address.item.id);
    if (found == inventory->items.end() || found->second.revision != address.item.revision) return {DomainStatus::Stale, {}};
    return {DomainStatus::Applied, found->second};
}
}
