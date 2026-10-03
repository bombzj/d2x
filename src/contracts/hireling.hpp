#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace d2x {
struct HirelingView {
    uint64_t revision = 0;
    EntityId actor, id;
    Vec position;
    bool known = false, active = false;
    std::string name;
    int level = 0;
    float life = 0;
    int maximumLife = 1, strength = 0, dexterity = 0, defense = 0;
    int damageMinimum = 0, damageMaximum = 0;
    uint64_t experience = 0, nextExperience = 0;
    std::array<int, 4> resistances{};
    std::map<int, int> summonCounts;
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
