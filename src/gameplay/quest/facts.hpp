#pragma once

namespace d2x {
// Authority-prepared ownership facts, never client permission claims.
struct QuestItemFacts {
    bool bark = false, malus = false;
    bool scroll = false, cube = false, shaft = false, head = false, staff = false;
};
} // namespace d2x
