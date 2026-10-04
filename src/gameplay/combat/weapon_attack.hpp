#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/items/equipment_rules.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include <array>
#include <string>
#include <string_view>
#include <optional>

namespace d2x {
struct SpearSequence;
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
    int remainingAttacks = 1;
    bool chargeSequence = false;
    std::string weaponClass = {};
    std::array<std::string, size_t(EquipmentSlot::Count)> appearanceDefinitions{};
    std::optional<SkillCastSpec> skill = {};
    std::shared_ptr<const SpearSequence> sequence;
    int animationFrame() const;
    std::string_view animationMode() const;
};
int effectiveAttackSpeed(int animationSpeed, int itemIAS, int baseWeaponSpeed, int skillRate);
int attackStartingFrame(std::string_view character, std::string_view weapon, std::string_view mode);
} // namespace d2x
