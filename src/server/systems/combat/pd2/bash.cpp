#include "../system.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/life.hpp"
#include <cstdlib>

namespace d2x::server::combat {
void System::preparePd2LifeNotification(ParticipantView target,TickContext tick) {
    // UNITFLAG_UPGRLIFENHITCLASS survives contacts until the frame's unit
    // publication. Track that latch independently of our reliable HitFacts.
    auto [found,created]=state_.lifeNotifications.try_emplace(target.id());auto &entry=found->second;
    if(!created && entry.tick==tick.tick) return;
    entry={tick.tick,target.player?playerLifePercentage(target.player->persistent.player.hp.raw(),target.player->totals.character.maxLifeFixed.raw()):
        monsterLifeRatio(target.monster->life,target.monster->maximumLife),false};
}
void System::finishPd2LifeNotification(SpellImpact &impact,ParticipantView target,bool getHit,bool softHit,int64_t damage,bool recovery,TickContext tick) {
    auto &entry=state_.lifeNotifications[target.id()];
    if(entry.tick!=tick.tick) preparePd2LifeNotification(target,tick);
    const auto &execution=*impact.nativeSkill;
    const bool uninterruptible=target.player?false:
        ports_.effects.unitStates(target.id(),tick.tick).contains(target.monster->rule.uninterruptableState) ||
        target.monster->aiStates.contains(target.monster->rule.uninterruptableState);
    if(target.aliveIn(target.player?target.player->area:target.monster->area) && !uninterruptible && !recovery &&
        !impact.knockback && !execution.defense.block && execution.defense.avoidance==WeaponAvoidance::None) {
        if(getHit || softHit) entry.dirty=true;
        else if(target.monster && damage>0 && std::abs(int(entry.sentLife)-int(monsterLifeRatio(target.monster->life,target.monster->maximumLife)))>4)
            entry.dirty=true;
    }
    impact.nativeSkill->lifeHitClassUpdated=entry.dirty;
}
}
