#pragma once
#include "property_program.hpp"
#include "loot_program.hpp"
#include "portal_program.hpp"
#include "gameplay/items/handle.hpp"
#include "gameplay/effects/definition.hpp"
#include "core/math.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace d2x::pd2 {
// Current core 37A740: unsigned (level - 137) < 74. This is a
// map-state boundary, not admission to every special scene in that range.
constexpr bool isMapRegion(int level) noexcept { return uint32_t(level)-137u<74u; }
struct MapItemDefinition {int level{},portal{};uint32_t itemIndex{};};
struct MapCatalog {
    MapPropertyRules properties;
    CombatStateDefinition state;
    MapLootRules loot;
    std::map<std::string,MapItemDefinition,std::less<>> items;
    struct SceneUnique {bool eligible{},socketable{};};
    std::vector<SceneUnique> sceneUniques;
    struct TimedTreasure {std::string treasure;int count{60};};
    std::map<int,TimedTreasure> timedTreasures;
    std::vector<std::string> relativeTreasures;
    std::map<std::string,std::string,std::less<>> notices;
};
// Runtime map identity is the consumed item revision. This is deliberately
// absent from character saves: a map belongs to its game, never to a D2S.
struct MapInstance {
    EntityId owner;
    ItemHandle item;
    MapItemDefinition destination;
    std::shared_ptr<const MapCatalog> catalog;
    MapProperties properties;
    uint8_t quality{};
    int controller{};
    uint32_t initSeed{};
    uint8_t participants{},bossDifficulty{};
};
}
