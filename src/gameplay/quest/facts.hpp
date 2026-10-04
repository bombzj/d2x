#pragma once
#include <array>

namespace d2x {
// Authority-prepared ownership facts, never client permission claims.
struct QuestItemFacts {
    bool bark = false, malus = false;
    bool scroll = false, cube = false, shaft = false, head = false, staff = false;
    bool jadeFigurine = false, goldenBird = false;
    bool gidbinn = false;
    bool lamTome = false;
    bool soulstone = false, forgeHammer = false;
    bool defrostPotion = false, resistanceScroll = false;
    std::array<bool, 5> khalim{}; // Eye, brain, heart, flail, assembled will.
};
} // namespace d2x
