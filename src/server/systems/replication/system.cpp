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
    std::set<RegionId> areas{recipient->area};
    for (const auto &edge : ports_.areas.at(recipient->area).definition.boundaries) if (ports_.areas.find(edge.destination)) areas.insert(edge.destination);
    std::vector<PlayerId> result;
    for (const auto &[key, player] : ports_.players.all()) if (player.entered && areas.contains(player.area) && key != id) result.push_back(key);
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
    std::vector<RegionId> visible{player->area};
    for (const auto &edge : ports_.areas.at(player->area).definition.boundaries)
        if (ports_.areas.find(edge.destination)) visible.push_back(edge.destination);
    for (const auto &[key, monster] : ports_.monsters.read().actors) {
        if (std::find(visible.begin(), visible.end(), monster.area) == visible.end()) continue;
        const auto &area = ports_.areas.at(monster.area).definition;
        const Vec observer = player->position + ports_.areas.at(player->area).definition.origin - area.origin;
        if (player->area == monster.area) {
            if (!area.activation.nearby(observer, monster.position)) continue;
        } else if ((observer - monster.position).length() > 35) continue;
        const bool dead = monster.life <= 0;
        const uint8_t mode = dead ? (tick >= monster.busyUntil ? 12 : 0) : monster.knockedUntil>tick ? 13 : monster.riseUntil > tick ? monster.riseMode : monster.reactionUntil>tick?monster.reactionMode:1;
        result.push_back({key, monster.area, monster.rule.nativeClass, monster.position,
            monster.route.empty() ? monster.position : monster.route.front(),
            monsterLifeRatio(monster.life,monster.maximumLife),
            mode, monster.revision, monster.moving, !dead && tick < monster.busyUntil, monster.chilledUntil > tick ? std::max(25, monster.velocityPercent + monster.rule.coldEffect) : monster.velocityPercent, monster.running, ports_.effects.unitStates(key,tick),monster.knockbackSource});
        result.back().equipment=monster.equipment;
        if(monster.hireling && monster.owner) if(const auto *owner=ports_.players.find(*monster.owner);owner && owner->player==id) {
            result.back().hireling=owner->persistent.player.hireling;
            result.back().hirelingOwner=owner->actor;
            result.back().hirelingLife=monster.life;
            result.back().hirelingAttributes={{"level",monster.rule.level},{"experience",int64_t(owner->persistent.player.hireling.experience)},{"nextexp",int64_t(monster.hirelingNextExperience)},
                {"hitpoints",monster.life},{"maxhp",monster.maximumLife},{"strength",monster.hirelingStrength},{"dexterity",monster.hirelingDexterity},
                {"armorclass",ports_.effects.unitDefense(monster.id,tick)},{"mindamage",monster.rule.minimumDamage},{"maxdamage",monster.rule.maximumDamage},
                {"fireresist",ports_.effects.unitResistance(monster.id,DamageType::Fire,tick)},{"lightresist",ports_.effects.unitResistance(monster.id,DamageType::Lightning,tick)},
                {"coldresist",ports_.effects.unitResistance(monster.id,DamageType::Cold,tick)},{"poisonresist",ports_.effects.unitResistance(monster.id,DamageType::Poison,tick)}};
        }
        result.back().lightningReady=monster.lightningReady;
        result.back().components=monster.components;result.back().componentCounts=monster.rule.componentCounts;
        if(monster.rule.enchantment) {
            const auto &mods=*monster.rule.enchantment;
            for(int id:mods.ids) result.back().modifiers.push_back(uint8_t(id));
            const auto rank=monster.identity.rank;
            result.back().rankFlags=uint8_t((rank==MonsterRank::Champion?1:0)|((rank==MonsterRank::Unique || rank==MonsterRank::SuperUnique)?2:0)|(rank==MonsterRank::SuperUnique?4:0)|(rank==MonsterRank::Minion?8:0)|(mods.has(36)?16:0));
            result.back().nameSeed=mods.nameSeed;result.back().superUniqueIndex=uint16_t(std::max(0,monster.rule.superUniqueIndex));
        }
        if(monster.amazonPet) {
            result.back().states.insert(monster.amazonPet->state.id);result.back().appearOverlay=monster.amazonPet->appearOverlay;
            if(monster.owner) if(const auto *owner=ports_.players.find(*monster.owner)) result.back().storedOwner=owner->actor;
            if(monster.amazonPet->decoy) result.back().modifiers.push_back(21); // SkillAma::SrvDo015 expiration UMod.
        }
        auto &states = result.back().states;
        for(const auto &effect:monster.potionEffects.entries()) if(effect.activeAt(tick) && effect.spec.state.id>=0) states.insert(effect.spec.state.id);
        result.back().stateStats=ports_.effects.unitStateStats(key,tick);
        if(monster.conversion) {
            states.insert(monster.conversion->state);states.insert(monster.conversion->alignment);
            result.back().stateStats[monster.conversion->alignment]={{monster.conversion->stat,2}};
        }
        if(monster.poison) states.insert(monster.poison->damage.state);
        if(monster.webUntil>tick && monster.rule.web) states.insert(monster.rule.web->aura.id);
        if(monster.slowed && monster.slowed->until>tick) states.insert(monster.slowed->state);
        if (monster.chilledUntil > tick && monster.rule.coldState >= 0) states.insert(monster.rule.coldState);
        if (monster.frozenUntil > tick && monster.rule.frozenState >= 0) states.insert(monster.rule.frozenState);
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
        if((!body.amazonPet && !body.hireling) || !body.owner || body.life<=0) continue;
        const auto *owner=ports_.players.find(*body.owner);
        if(owner && owner->entered)
            result.push_back({key,owner->actor,uint8_t(body.hireling?7:body.amazonPet->petType),uint16_t(body.rule.nativeClass)});
    }
    return result;
}
}
