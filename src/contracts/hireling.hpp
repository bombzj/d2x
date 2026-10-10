#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
struct PetPortraitView {
    EntityId single;
    bool hireling = false, showCount = false;
    int count = 0;
    std::string art, name;
    std::optional<uint16_t> lifePercentage;
};
struct HirelingView {
    uint64_t revision = 0;
    EntityId actor, id;
    Vec position;
    bool known = false, active = false;
    std::string name;
    int level = 0;
    int classId = -1;
    float life = 0;
    int maximumLife = 1, strength = 0, dexterity = 0, defense = 0;
    bool defenseImproved = false;
    int damageMinimum = 0, damageMaximum = 0;
    uint64_t experience = 0, nextExperience = 0;
    std::array<int, 4> resistances{};
    std::vector<PetPortraitView> portraits;
};
struct HirelingOfferView {
    uint32_t slot = 0;
    std::string name, description;
    int level = 0, life = 0, defense = 0, price = 0;
};
// Bound player's offered candidates; no seeds, native hireling records or other players.
struct HirelingListView {
    uint64_t revision = 0;
    EntityId actor, npc;
    unsigned gold = 0;
    std::string cancelLabel;
    std::vector<HirelingOfferView> offers;
};
} // namespace d2x
