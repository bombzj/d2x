#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/items/equipment_rules.hpp"
#include <array>
#include <string>
#include <string_view>
#include <optional>

namespace d2x {
// AnimData uses 8-bit fractional frames and a 25 Hz simulation clock.
struct WeaponAttackTiming {
    std::string mode;
    int frames = 0, speed = 0, actionFrame = 0, startFrame = 0;
    int durationTicks() const;
    int actionTick() const;
};
struct WeaponAttackState {
    EntityId weapon, target;
    Vec aim;
    WeaponAttackTiming timing;
    bool thrown = false, released = false;
    int ticks = 0;
    std::string weaponClass;
    std::array<std::string, size_t(EquipmentSlot::Count)> appearanceDefinitions{};
    int animationFrame() const;
};
int effectiveAttackSpeed(int animationSpeed, int itemIAS, int baseWeaponSpeed, int skillRate);
int attackStartingFrame(std::string_view character, std::string_view weapon, std::string_view mode);
int meleeDistance(Vec from, int fromSize, Vec to, int toSize);
int missileDistance(Vec from, Vec to);
std::optional<float> missileUnitIntersection(Vec from, Vec to, int missileSize, Vec unit, int unitSize);
} // namespace d2x
