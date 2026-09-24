#include "gameplay/session/session.hpp"
#include <algorithm>
#include <stdexcept>
#include <variant>

namespace d2x {
namespace {
void requireSave(bool condition, const char *reason) {
    if (!condition)
        throw std::runtime_error(std::string("Invalid save: ") + reason);
}
} // namespace

SessionSnapshot GameSession::prepareCharacterRestore(SessionSnapshot character) const {
    requireSave(character.contentFingerprint == contentFingerprint_,
                "MPQ content or gameplay rules differ");
    requireSave(character.maps.size() == regions_.size(), "region count");
    for (size_t i = 0; i < regions_.size(); ++i)
        requireSave(character.maps[i] == regions_[i].definition.mapPath,
                    "map configuration differs; check --level/--variant/--preset");
    requireSave(std::all_of(character.inventory.items.begin(), character.inventory.items.end(),
                            [](const auto &entry) {
                                return std::holds_alternative<ContainerLocation>(entry.second.location);
                            }), "ground item in character save");

    auto lastRegion = std::find_if(regions_.begin(), regions_.end(),
        [&](const Region &region) { return region.definition.id == character.world.area.region; });
    requireSave(lastRegion != regions_.end(), "last visited region");
    auto level = worldContent_.levels().find(int(lastRegion->definition.id));
    int act = level == worldContent_.levels().end() ? 0 : level->second.act;
    auto town = std::find_if(regions_.begin(), regions_.end(), [&](const Region &region) {
        auto record = worldContent_.levels().find(int(region.definition.id));
        return region.definition.safe && record != worldContent_.levels().end() &&
               record->second.act == act;
    });
    requireSave(town != regions_.end(), "act town is unavailable");

    // This snapshot is never written to disk. It supplies the complete value
    // model expected by the existing semantic validator and atomic restore.
    SessionSnapshot fresh = snapshot();
    fresh.contentFingerprint = character.contentFingerprint;
    fresh.nextEntityId = std::max(fresh.nextEntityId, character.nextEntityId);
    fresh.maps = std::move(character.maps);
    fresh.world.mapSeed = character.world.mapSeed;
    fresh.world.population.difficulty = character.world.population.difficulty;
    fresh.world.time = character.world.time;
    fresh.world.waypoints = std::move(character.world.waypoints);
    fresh.world.message.clear();
    fresh.world.portal = {};
    fresh.world.player = std::move(character.world.player);
    fresh.world.player.pos = town->map.spawn;
    fresh.world.player.previous = town->map.spawn;
    fresh.world.player.dead = false;
    fresh.world.player.hp = std::max(1.f, fresh.world.player.hp);
    fresh.world.area = {};
    fresh.world.area.region = town->definition.id;
    fresh.world.area.initialized = true;
    for (size_t i = 0; i < regions_.size(); ++i) {
        fresh.inactiveAreas[i] = {};
        fresh.inactiveAreas[i].region = regions_[i].definition.id;
    }
    fresh.inventory = std::move(character.inventory);
    fresh.containers = character.containers;
    fresh.loot = {loot_.randomState(), {}, {}};
    fresh.npcMotions = initialNpcMotions_;
    fresh.soldVendorOffers.clear();
    return fresh;
}
} // namespace d2x
