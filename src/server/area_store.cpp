#include "area_store.hpp"
#include <algorithm>

namespace d2x::server {
bool AreaStore::loaded(RegionId id, Vec point) const {
    const auto *area = find(id);
    if (!area) return false;
    const auto &layout = area->definition.activation;
    const auto *room = layout.room(point);
    if (!room) return false;
    return !area->rooms.empty() && area->rooms.at(size_t(room - layout.rooms().data())).loaded;
}
bool AreaStore::active(RegionId id, Vec point) const {
    const auto *area = find(id);
    if (!area) return false;
    const auto &layout = area->definition.activation;
    const auto *room = layout.room(point);
    if (!room) return false;
    if (area->rooms.empty()) return false;
    const auto &state = area->rooms.at(size_t(room - layout.rooms().data()));
    return state.loaded && state.status <= 1;
}
bool AreaStore::nearby(RegionId from, Vec observer, RegionId to, Vec target) const {
    const auto *source = find(from), *destination = find(to);
    if (!source || !destination || source->definition.act != destination->definition.act) return false;
    const auto *first = source->definition.activation.room(observer);
    const auto *second = destination->definition.activation.room(target);
    if (!first || !second || !loaded(to, target)) return false;
    const auto index = size_t(first - source->definition.activation.rooms().data());
    const RoomView desired{to, (int(destination->definition.origin.x) + second->x) / 5,
        (int(destination->definition.origin.y) + second->y) / 5};
    const auto &near = source->definition.roomNear.at(index);
    return std::find(near.begin(), near.end(), desired) != near.end();
}
}
