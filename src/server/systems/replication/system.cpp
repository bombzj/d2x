#include "system.hpp"
#include "gameplay/combat/life.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include <algorithm>
namespace d2x::server::replication {
std::vector<PlayerId> System::visible(PlayerId id) const {
    const auto *recipient = ports_.players.find(id); if (!recipient || !recipient->entered) return {};
    std::vector<PlayerId> result;
    for (const auto &[key, player] : ports_.players.all())
        if (player.entered && key != id && ports_.areas.nearby(recipient->area, recipient->position + Vec{.5f,.5f}, player.area, player.position + Vec{.5f,.5f})) result.push_back(key);
    return result;
}
StepStatus System::step(TickContext, FrameFacts &) {
    State next;
    for (const auto &[id, player] : ports_.players.all()) if (player.entered) {
        Interest interest{player.area, ports_.areas.at(player.area).generation, {}};
        for (const auto other : visible(id)) interest.visible.insert(ports_.players.find(other)->actor);
        next.recipients.emplace(id, std::move(interest));
    }
    state_ = std::move(next); return StepStatus::Complete;
}
DomainResult<> System::admit(PlayerId id) {
    if (!ports_.players.find(id)) return {DomainStatus::InvalidActor, {}};
    return {DomainStatus::Applied, std::monostate{}};
}
std::vector<MonsterSnapshot> System::visibleMonsters(PlayerId id, uint64_t tick) const {
    std::vector<MonsterSnapshot> result;
    const auto *player = ports_.players.find(id);
    if (!player || !player->entered) return result;
    for (const auto &[key, monster] : ports_.monsters.read().actors) {
        if(monster.restorationPending) continue;
        if (!ports_.areas.nearby(player->area, player->position + Vec{.5f,.5f}, monster.area, monster.position)) continue;
        if(!monster.alive() && monster.corpse.unavailable && tick>=monster.busyUntil) continue;
        const bool dead = !monster.alive();
        const uint8_t mode = dead ? (tick >= monster.busyUntil ? 12 : 0) : monster.knockedUntil>tick ? 13 : monster.riseUntil > tick ? monster.riseMode : monster.reactionUntil>tick?monster.reactionMode:monster.burrowed?8:(monster.holdPose || monster.poseUntil>tick)?monster.aiPose:1;
        result.push_back({key, monster.area, monster.rule.nativeClass, monster.position,
            monster.questNpc?monster.movementGoal:monster.skillMotion?monster.skillMotion->destination:monster.route.empty()?monster.position:monster.route.back(),
            monster.modeAlive && !monster.maximumLife?uint8_t{128}:monsterLifeRatio(monster.life,monster.maximumLife),
            mode, monster.revision, monster.moving, !dead && tick < monster.busyUntil, monsters::movementPercent(monster,tick), monster.running, ports_.effects.unitStates(key,tick),monster.knockbackSource});
        result.back().equipment=monster.equipment;
        result.back().pathSteps=std::max(uint8_t{1},monster.pathSteps);
        result.back().pathType=monster.pathType;result.back().pathDistance=monster.pathDistance;
        result.back().hostile=monster.enemyTarget();
        if(monster.movementTarget) {
            if(const auto *target=ports_.players.findActor(monster.movementTarget);target && target->entered && target->area==monster.area)
                result.back().movementTarget=MotionTarget{target->actor,0};
            else if(const auto *target=ports_.monsters.find(monster.movementTarget);target && target->area==monster.area && target->life>0)
                result.back().movementTarget=MotionTarget{target->id,1};
        }
        // AC's hireling owner is public; only the detailed identity/stat
        // messages below are restricted to that owner.
        if(monster.hireling && monster.owner) if(const auto *owner=ports_.players.find(*monster.owner))
            result.back().hirelingOwner=owner->actor;
        if(monster.hireling && monster.owner) if(const auto *owner=ports_.players.find(*monster.owner);owner && owner->player==id) {
            result.back().hireling=owner->persistent.player.hireling;
            result.back().hirelingLife=monster.life;
            result.back().hirelingAttributes=monster.hirelingBaseAttributes;
            result.back().hirelingAttributes.insert({{"level",monster.rule.level},{"experience",int64_t(owner->persistent.player.hireling.experience)},
                {"nextexp",int64_t(monster.hirelingNextExperience)},{"hitpoints",monster.life}});
        }
        result.back().lightningReady=monster.lightningReady;
        result.back().components=monster.components;result.back().componentCounts=monster.rule.componentCounts;
        if(monster.rule.enchantment) {
            const auto &mods=*monster.rule.enchantment;
            for(int id:mods.ids) result.back().modifiers.push_back(uint8_t(id));
            const auto rank=monster.identity.rank;
            result.back().rankFlags=uint8_t((rank==MonsterRank::Champion?1:0)|((rank==MonsterRank::Champion || rank==MonsterRank::Unique || rank==MonsterRank::SuperUnique)?2:0)|(rank==MonsterRank::SuperUnique?4:0)|(rank==MonsterRank::Minion?8:0)|(mods.has(36)?16:0));
            result.back().nameSeed=mods.nameSeed;result.back().superUniqueIndex=uint16_t(std::max(0,monster.rule.superUniqueIndex));
        }
        if(monster.whippedAi && std::ranges::find(result.back().modifiers,33)==result.back().modifiers.end()) result.back().modifiers.push_back(33);
        if(monster.amazonPet) {
            result.back().appearOverlay=monster.amazonPet->appearOverlay;
            if(monster.owner) if(const auto *owner=ports_.players.find(*monster.owner)) result.back().storedOwner=owner->actor;
            if(monster.amazonPet->decoy) result.back().modifiers.push_back(21); // SkillAma::SrvDo015 expiration UMod.
        }
        if(monster.revived() && monster.owner) if(const auto *owner=ports_.players.find(*monster.owner)) result.back().storedOwner=owner->actor;
        result.back().stateStats=ports_.effects.unitStateStats(key,tick);
        if(monster.changedClass && monster.rule.changeClassState>=0 && result.back().states.contains(monster.rule.changeClassState))
            result.back().stateStats[monster.rule.changeClassState]={{monster.rule.shortParameterStat,*monster.changedClass}};
        if(monster.nativeOwner && monster.rule.storedOwnerState>=0 && result.back().states.contains(monster.rule.storedOwnerState))
            result.back().stateStats[monster.rule.storedOwnerState]={{monster.rule.stats->names.at("source_unit_type"),1},
                {monster.rule.stats->names.at("source_unit_id"),int64_t(monster.nativeOwner.value)}};
        // SUNIT_SetAlign always owns an alignment state, including evil's
        // zero value (an empty state stat list on the original wire).
        if(monster.rule.alignmentState>=0 && monster.rule.alignmentStat>=0) {
            const int alignment=int(monster.alignment());
            if(alignment) result.back().stateStats[monster.rule.alignmentState]={{monster.rule.alignmentStat,alignment}};
            else result.back().stateStats.erase(monster.rule.alignmentState);
        }
        if(monster.conversion) {
            result.back().stateStats[monster.conversion->alignment]={{monster.conversion->stat,2}};
        }
    }
    return result;
}

std::vector<PetOwnershipSnapshot> System::pets(PlayerId id) const {
    std::vector<PetOwnershipSnapshot> result;
    const auto *recipient=ports_.players.find(id);
    if(!recipient || !recipient->entered) return result;
    // PlayerPets broadcasts ownership to the game independently of room interest.
    for(const auto &[key,body]:ports_.monsters.read().actors) {
        if((!body.amazonPet && !body.assassinPet && !body.druidPet && !((body.necroSummon || body.hydra()) && body.rule.petType>0) && !body.hireling) || !body.owner || !body.alive()) continue;
        const auto *owner=ports_.players.find(*body.owner);
        if(owner && owner->entered)
            result.push_back({key,owner->actor,uint8_t(body.hireling?7:body.amazonPet?body.amazonPet->petType:body.rule.petType),uint16_t(body.rule.nativeClass),
                playerLifePercentage(body.life,body.maximumLife),body.area});
    }
    return result;
}
}
