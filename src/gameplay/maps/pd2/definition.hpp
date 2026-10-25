#pragma once
#include "property_program.hpp"
#include "loot_program.hpp"
#include "gameplay/items/handle.hpp"
#include "gameplay/effects/definition.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace d2x::pd2 {
struct MapItemDefinition {int level{},portal{};uint32_t itemIndex{};};
struct MapCatalog {
    MapPropertyRules properties;
    CombatStateDefinition state;
    MapLootRules loot;
    std::map<std::string,MapItemDefinition,std::less<>> items;
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
};
}
