#pragma once
#include "equipment_rules.hpp"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
enum class ItemFamily { Weapon, Armor, Misc };
// MPQ equipment artwork selectors; indices follow Armor.txt's six body columns.
struct ItemAppearance {
    int component = -1;
    std::string token;
    std::array<std::string, 6> body{};
};
struct ItemBaseStats {
    std::string type, secondaryType, weaponClass;
    std::optional<int> minDamage, maxDamage, twoHandMin, twoHandMax, throwMin, throwMax;
    std::optional<int> minDefense, maxDefense, requiredStrength, requiredDexterity, requiredLevel;
    std::optional<int> level, magicLevel, cost, speed, block, sockets, rarity, spawnable;
    std::optional<int> strengthBonus, dexterityBonus;
    std::string sourceTable;
    size_t sourceRow = 0;
};
struct ItemDefinition {
    std::string code, name;
    ItemFamily family = ItemFamily::Misc;
    int width = 1, height = 1;
    unsigned maxStack = 1, maxDurability = 0;
    bool beltAllowed = false, usable = false;
    std::string icon, groundAnimation;
    bool artAvailable = false;
    bool autoBelt = false;
    int beltRows = 0; // Zero is not an equippable belt; row zero is the ready row.
    ItemBaseStats base;
    EquipmentDefinition equipment;
    ItemAppearance appearance;
};
// Immutable once constructed. Instance IDs and definition codes are different identities.
class ItemCatalog {
    std::map<std::string, ItemDefinition, std::less<>> entries_;

  public:
    explicit ItemCatalog(std::vector<ItemDefinition> definitions);
    const ItemDefinition *find(std::string_view code) const;
    const auto &entries() const { return entries_; }
};
} // namespace d2x
