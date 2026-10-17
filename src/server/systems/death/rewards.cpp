#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/progression/system.hpp"
#include "server/systems/companions/system.hpp"
#include "server/systems/social/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/loot/system.hpp"
#include "gameplay/rewards/experience.hpp"
#include <algorithm>

namespace d2x::server::death {
StepStatus System::advanceRewards(TickContext tick) {
    bool blocked = false;
    for (const auto &[id, monster] : ports_.monsters.read().actors) {
        if (monster.alive() || monster.rewardComplete) continue;
        if(!monster.killer) {
            const PlayerState *beneficiary=nullptr;
            for(const auto &[key,player]:ports_.players.all()) {(void)key;if(player.entered && player.area==monster.area && player.persistent.player.hp>0) {beneficiary=&player;break;}}
            if(!monster.rule.noTreasure && monster.enemyTarget() && beneficiary) {
                LootRequest source;source.source=id;source.identity=monster.identity;source.region=monster.area;source.difficulty=monster.rule.difficulty;source.sourceSeed=true;source.rewardModifiers=monsterRewardModifiers(monster.rule.enchantment);source.monsterPlayerCount=monster.admittedPlayerCount;
                loot::Request request{monster.deathOccurrence,source,beneficiary->player,monster.position};request.environment=true;
                const auto queued=ports_.loot.queue(request);if(!queued && isDeferred(queued.status)) {blocked=true;continue;}
            }
            ports_.monsters.rewardComplete(id);continue;
        }
        auto reward = state_.rewards.find(id);
        if (reward == state_.rewards.end()) {
            const PlayerState *killer = nullptr;
            for (const auto &[playerId, player] : ports_.players.all()) {
                (void)playerId; if (player.actor == monster.killer) { killer = &player; break; }
            }
            if (!killer || !killer->entered || killer->area != monster.area || killer->persistent.player.hp <= 0 ||
                killer->persistent.player.level < 1 || size_t(killer->persistent.player.level) >= monster.rule.experience.size()) {
                ports_.monsters.rewardComplete(id); continue;
            }
            const auto amount = monster.rule.noExperience?0:playerExperienceGain(monster.rule.experience[size_t(killer->persistent.player.level)], killer->totals.character.combat.experiencePercent);
            reward = state_.rewards.emplace(id, Reward{killer->player, killer->actor, amount,killer->totals.character.combat.lifeOnKill,killer->totals.character.combat.manaOnKill,killer->persistent.player}).first;
            if(!monster.rule.noExperience) reward->second.hireling=ports_.companions.experience(killer->player,monster);
            std::vector<const PlayerState*> members;
            unsigned levelSum=0;
            if(ports_.social.partyId(killer->player)!=UINT16_MAX)
                for(const auto &[peerId,peer]:ports_.players.all())
                    if(peer.entered && peer.area==monster.area && peer.persistent.player.hp>0 &&
                       ports_.social.sameParty(killer->player,peerId) && (peer.position-monster.position).length()<=80 &&
                       peer.persistent.player.level>0 && size_t(peer.persistent.player.level)<monster.rule.experienceRatios.size()) {
                        members.push_back(&peer);levelSum+=unsigned(peer.persistent.player.level);
                    }
            if(!monster.rule.noExperience && members.size()>1 && levelSum && monster.rule.experienceBase) {
                const auto base=monster.rule.experienceBase;
                const auto pooled=base+89*base*(members.size()-1)/256;
                const float multiplier=float(pooled)/float(levelSum);
                for(const auto *member:members) {
                    const auto level=member->persistent.player.level;
                    const auto raw=uint64_t(double(level)*multiplier);
                    const auto experience=monsterExperienceGain(raw,1,monster.rule.experienceLevel,level,monster.rule.experienceRatios[size_t(level)],monster.rule.experienceShift);
                    reward->second.shares.push_back({member->player,member->actor,playerExperienceGain(experience,member->totals.character.combat.experiencePercent)});
                }
            } else if(ports_.social.partyId(killer->player)==UINT16_MAX || !members.empty())
                reward->second.shares.push_back({killer->player,killer->actor,amount});
        }
        const auto *killer = ports_.players.find(reward->second.player);
        if (!killer || !killer->entered || killer->actor != reward->second.actor || killer->persistent.player.hp <= 0 || killer->lastExperienceAward == UINT64_MAX) {
            ports_.monsters.rewardComplete(id); state_.rewards.erase(reward); continue;
        }
        if(reward->second.phase==RewardPhase::Restore) {
            const ActorContext actor{killer->player,killer->actor,killer->area,ports_.areas.at(killer->area).generation,0,tick.tick};
            const auto result=ports_.transactions.resources(actor,killer->characterRevision,
                std::clamp(killer->persistent.player.hp+Fixed8(reward->second.life),Fixed8{},killer->totals.character.maxLifeFixed),
                std::clamp(killer->persistent.player.mana+Fixed8(reward->second.mana),Fixed8{},killer->totals.character.maxManaFixed),killer->persistent.player.stamina);
            if(isDeferred(result.status)) {blocked=true;continue;}
            if(!result) {ports_.monsters.rewardComplete(id);state_.rewards.erase(reward);continue;}
            reward->second.phase=RewardPhase::HirelingExperience;
        }
        if(reward->second.phase==RewardPhase::HirelingExperience) {
            if(reward->second.hireling) {
                const ActorContext actor{killer->player,killer->actor,killer->area,ports_.areas.at(killer->area).generation,0,tick.tick};
                const auto result=ports_.companions.awardExperience(actor,*reward->second.hireling);
                if(isDeferred(result.status)) {blocked=true;continue;}
            }
            reward->second.phase=RewardPhase::Loot;
        }
        if(reward->second.phase==RewardPhase::Loot && monster.rule.noTreasure) reward->second.phase=RewardPhase::PlayerExperience;
        if (reward->second.phase==RewardPhase::Loot) {
            LootRequest source; source.source = id; source.identity = monster.identity; source.region = monster.area; source.difficulty = monster.rule.difficulty; source.sourceSeed = true; source.rewardModifiers=monsterRewardModifiers(monster.rule.enchantment);
            source.monsterPlayerCount=monster.admittedPlayerCount;
            const auto queued = ports_.loot.queue({monster.deathOccurrence, source, killer->player, monster.position,{},reward->second.lootClaimant,false,false,monster.hirelingLootMagicFind,monster.hirelingLootGoldFind});
            if (!queued) { blocked = true; continue; }
            reward->second.phase = RewardPhase::PlayerExperience;
        }
        auto &value=reward->second;
        while(value.awarded<value.shares.size()) {
            const auto &share=value.shares[value.awarded];const auto *recipient=ports_.players.find(share.player);
            if(!share.amount || !recipient || !recipient->entered || recipient->actor!=share.actor || recipient->lastExperienceAward==UINT64_MAX) {++value.awarded;continue;}
            const auto result=ports_.progression.award({share.player,recipient->lastExperienceAward+1,share.amount,tick.tick});
            if(result || result.status==DomainStatus::Conflict) ++value.awarded;
            else {blocked=true;break;}
        }
        if(value.awarded==value.shares.size()) {ports_.monsters.rewardComplete(id);state_.rewards.erase(reward);}
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
