#pragma once
#include <optional>
#include <string>

namespace d2x {
class GameSession;
struct CharacterActionStats {
    std::string damage;
    std::string attackRating;
};
CharacterActionStats characterActionStats(const GameSession &session, std::optional<int> skill);
} // namespace d2x
