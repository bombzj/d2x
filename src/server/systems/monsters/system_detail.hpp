#pragma once
// Private shared helpers for the monsters System translation units.
#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/actions/timing.hpp"
#include "gameplay/combat/stat_modifiers.hpp"
#include "gameplay/rewards/experience.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace d2x::server::monsters {
namespace monsters_system_detail {
inline std::optional<std::pair<Vec,int>> nativePathTarget(const PlayerStore &players,const System &monsters,EntityId id,RegionId area) {
    for(const auto &[key,player]:players.all()) {
        (void)key;
        if(player.actor==id && player.entered && player.area==area) return std::pair{player.position,2};
    }
    const auto *unit=monsters.find(id);
    if(unit && unit->area==area) return std::pair{unit->position,unit->rule.size};
    // Object targets (Fn131's Cain portal) have a static path and retain
    // their type-2 identity independently of combat/life eligibility.
    return monsters.nativeTargetPosition(id,area);
}
inline int recoveryTicks(const Actor &actor,const effects::System &effects,uint64_t tick,bool block) {
    const auto &clock=block?actor.rule.blockAnimation:actor.rule.hitRecoveryAnimation;
    if(!clock) return block?actor.rule.blockTicks:actor.rule.hitRecoveryTicks;
    auto modifiers=effects.unitModifiers(actor.id,tick).combat;
    if(actor.combatCompanion()) mergeCombatModifiers(modifiers,actor.petStats.attributes.combat);
    const int speed=effectiveRecoverySpeed(clock->speed,block?modifiers.fasterBlock:modifiers.fasterHitRecovery);
    return monsterAnimationTicks({clock->frames,speed});
}
inline void playerCountBonus(MonsterRule &rule,unsigned count) {
    const unsigned scale=100+50*(count-1);
    rule.experienceBase=rule.experienceBase*scale/100;
    for(size_t level=1;level<rule.experience.size() && level<rule.experienceRatios.size();++level)
        rule.experience[level]=monsterExperienceGain(rule.experienceBase,1,rule.experienceLevel,int(level),rule.experienceRatios[level],rule.experienceShift);
    if(rule.difficulty==0 || count==1) return;
    // D2MOO MonsterMode::sub_6FC627B0 uses 8*(count-1)/128 for
    // expansion Nightmare/Hell attack damage, to-hit and native element duration.
    const auto scaled=[&](int value){return int(std::min(int64_t(INT32_MAX),int64_t(value)+int64_t(value)*8*(count-1)/128));};
    rule.minimumDamage=scaled(rule.minimumDamage);rule.maximumDamage=scaled(rule.maximumDamage);rule.attackRating=scaled(rule.attackRating);
    const auto attackBonus=[&](MonsterAttackRule &attack) {
        attack.minimum=scaled(attack.minimum);attack.maximum=scaled(attack.maximum);attack.rating=scaled(attack.rating);
        for(auto &element:attack.elements) {
            element.minimum=scaled(element.minimum);element.maximum=scaled(element.maximum);element.durationFrames=scaled(element.durationFrames);
        }
    };
    for(auto &[mode,attack]:rule.attacks) {(void)mode;attackBonus(attack);}
    for(auto &[skill,attack]:rule.skillActions) {(void)skill;attackBonus(attack);}
}
} // namespace monsters_system_detail
using namespace monsters_system_detail;
} // namespace d2x::server::monsters
