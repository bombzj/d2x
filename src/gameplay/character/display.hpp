#pragma once
#include <cstdint>
#include <string>

namespace d2x {
struct CharacterActionDisplay {
    std::string damage, attackRating;
    uint8_t damageColor{}, burstColor{}, attackRatingColor{};
};
} // namespace d2x
