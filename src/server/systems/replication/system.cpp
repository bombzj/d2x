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
        if (!ports_.areas.nearby(player->area, player->position + Vec{.5f,.5f}, monster.area, monster.position)) continue;
        if(monster.life<=0 && monster.corpseUnavailable && tick>=monster.busyUntil) continue;
        const bool dead = monster.life <= 0;
        const uint8_t mode = dead ? (tick >= monster.busyUntil ? 12 : 0) : monster.knockedUntil>tick ? 13 : monster.riseUntil > tick ? monster.riseMode : monster.reactionUntil>tick?monster.reactionMode:monster.burrowed?8:(monster.holdPose || monster.poseUntil>tick)?monster.aiPose:1;
        result.push_back({key, monster.area, monster.rule.nativeClass, monster.position,
            monster.skillMotion?monster.skillMotion->destination:monster.route.empty()?monster.position:monster.route.back(),
            monsterLifeRatio(monster.life,monster.maximumLife),
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
        if(monster.amazonPet) {
            result.back().states.insert(monster.amazonPet->state.id);result.back().appearOverlay=monster.amazonPet->appearOverlay;
            if(monster.owner) if(const auto *owner=ports_.players.find(*monster.owner)) result.back().storedOwner=owner->actor;
            if(monster.amazonPet->decoy) result.back().modifiers.push_back(21); // SkillAma::SrvDo015 expiration UMod.
        }
        auto &states = result.back().states;
        states.insert(monster.aiStates.begin(),monster.aiStates.end());
        states.insert(monster.deathStates.begin(),monster.deathStates.end());
        if(!dead && monster.shatterActive && monster.rule.shatterState.id>=0) states.insert(monster.rule.shatterState.id);
        if(monster.skillMotion && monster.skillMotion->moveState && monster.rule.skillMoveState>=0) states.insert(monster.rule.skillMoveState);
        if(monster.revived()) {states.insert(monster.necroPet->reviveState.id);if(monster.owner) if(const auto *owner=ports_.players.find(*monster.owner)) result.back().storedOwner=owner->actor;}
        for(const auto &effect:monster.potionEffects.entries()) if(effect.activeAt(tick) && effect.spec.state.id>=0) states.insert(effect.spec.state.id);
        result.back().stateStats=ports_.effects.unitStateStats(key,tick);
        // SUNIT_SetAlign always owns an alignment state, including evil's
        // zero value (an empty state stat list on the original wire).
        if(monster.rule.alignmentState>=0 && monster.rule.alignmentStat>=0) {
            states.insert(monster.rule.alignmentState);
            const int alignment=int(monster.alignment());
            if(alignment) result.back().stateStats[monster.rule.alignmentState]={{monster.rule.alignmentStat,alignment}};
            else result.back().stateStats.erase(monster.rule.alignmentState);
        }
        if(monster.conversion) {
            states.insert(monster.conversion->state);states.insert(monster.conversion->alignment);
            result.back().stateStats[monster.conversion->alignment]={{monster.conversion->stat,2}};
        }
        if(monster.poison) states.insert(monster.poison->damage.state);
        if(monster.webUntil>tick && monster.rule.web) states.insert(monster.rule.web->aura.id);
        if(monster.slowed && monster.slowed->until>tick) states.insert(monster.slowed->state);
        // Keep the native state until its owning damage/expiry transaction
        // clears it, including an overdue wound waiting for output capacity.
        if (!dead && monster.wound && monster.rule.openWoundsState >= 0) states.insert(monster.rule.openWoundsState);
        if (monster.chilledUntil > tick && monster.rule.coldState >= 0) states.insert(monster.rule.coldState);
        if (monster.frozenUntil > tick && monster.rule.frozenState.id >= 0) states.insert(monster.rule.frozenState.id);
        if(monster.stunnedUntil>tick && monster.rule.stunState>=0) states.insert(monster.rule.stunState);
    }
    return result;
}

std::vector<PetOwnershipSnapshot> System::pets(PlayerId id) const {
    std::vector<PetOwnershipSnapshot> result;
    const auto *recipient=ports_.players.find(id);
    if(!recipient || !recipient->entered) return result;
    // PlayerPets broadcasts ownership to the game independently of room interest.
    for(const auto &[key,body]:ports_.monsters.read().actors) {
        if((!body.amazonPet && !(body.necroSummon && body.rule.petType>0) && !body.hireling) || !body.owner || body.life<=0) continue;
        const auto *owner=ports_.players.find(*body.owner);
        if(owner && owner->entered)
            result.push_back({key,owner->actor,uint8_t(body.hireling?7:body.amazonPet?body.amazonPet->petType:body.rule.petType),uint16_t(body.rule.nativeClass),
                playerLifePercentage(body.life,body.maximumLife),body.area});
    }
    return result;
}
}
