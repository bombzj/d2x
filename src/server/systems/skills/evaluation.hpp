#pragma once
#include <map>
#include <optional>
#include <utility>
namespace d2x {
struct SkillCastSpec;
struct ChargedSkill;
namespace server { struct PlayerState; }
}
namespace d2x::server::skills {
int mastery(const PlayerState &, const std::map<int, std::pair<int, int>> &);
SkillCastSpec evaluate(const PlayerState &, int skill, int rank);
std::optional<ChargedSkill> chargedSource(const PlayerState &, int skill, bool right);
int coldPierce(const PlayerState &);
}
