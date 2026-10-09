#pragma once
#include "server/player_store.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include <algorithm>
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
    const auto &definition=rules.definitions.at(skill).spec;
    auto result=resolveSkill(definition,
        {rank, player.persistent.player.skillRanks, mastery(player, rules.fireMasteries),
         mastery(player, rules.lightningMasteries), player.totals.character.combat.coldSkillDamagePercent});
    if(definition.summon && definition.summon->amazon) result.summon=resolveAmazonSummon(*definition.summon,rank,player.persistent.player.level,player.persistent.difficulty,player.totals.character,player.persistent.player.skillRanks);
    return result;
}
inline std::optional<ChargedSkill> chargedSource(const PlayerState &player,int skill,bool right) {
    const auto owner=player.persistent.player.selectedSkillOwners.at(player.persistent.player.weaponSet*2+(right?1:0));
    const auto found=std::find_if(player.totals.chargedSkills.begin(),player.totals.chargedSkills.end(),[&](const auto &c){return c.item.id.value==owner && c.skill==skill && c.charges>0;});
    return found==player.totals.chargedSkills.end()?std::nullopt:std::optional{*found};
}
inline int coldPierce(const PlayerState &player) {
    return player.totals.character.combat.coldPierce + mastery(player, player.rules.skills->coldMasteries);
}
}
