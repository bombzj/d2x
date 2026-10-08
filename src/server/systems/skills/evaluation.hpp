#pragma once
#include "server/player_store.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/rank_bonus.hpp"
namespace d2x::server::skills {
inline int mastery(const PlayerState &player, const std::map<int, std::pair<int, int>> &curves) {
    int value = 0;
    for (const auto &[id, curve] : curves)
        if (const auto rank = player.totals.skillRanks.find(id); rank != player.totals.skillRanks.end())
            value += skillRankBonus(curve, rank->second);
    return value;
}
// A child keeps its originating cast rank, but reads the owner's current
// synergies and masteries, as the master authority's SkillSource adapter did.
inline SkillCastSpec evaluate(const PlayerState &player, int skill, int rank) {
    const auto &rules = *player.rules.skills;
    return resolveSkill(rules.definitions.at(skill).spec,
        {rank, player.persistent.player.skillRanks, mastery(player, rules.fireMasteries),
         mastery(player, rules.lightningMasteries), player.totals.character.combat.coldSkillDamagePercent});
}
inline int coldPierce(const PlayerState &player) {
    return player.totals.character.combat.coldPierce + mastery(player, player.rules.skills->coldMasteries);
}
}
