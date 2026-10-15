#include "system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/behavior.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::effects {
StepStatus System::finishContacts(uint64_t tick) {
    if(advanceMonsterCurses(tick)==StepStatus::Blocked) return StepStatus::Blocked;
    return StepStatus::Complete;
}
StepStatus System::advanceSkills(const ActorContext &actor, Recovery &recovery) {
    const auto &p=*ports_.players.find(actor.player);const auto &area=ports_.areas.at(actor.area);
    bool blocked=false;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    for(const auto &effect:recovery.states.entries()) {
        if(!effect.activeAt(actor.tick) || effect.spec.source.kind!=CombatEffectSource::Skill || !p.rules.skills ||
            !p.rules.skills->definitions.contains(effect.spec.source.definition)) continue;
        if(p.rules.skills->definitions.at(effect.spec.source.definition).spec.effect==SkillBehavior::Blaze) {
            if(!area.definition.town && p.moving && recovery.previous &&
                (int(recovery.previous->x)!=int(p.position.x) || int(recovery.previous->y)!=int(p.position.y))) {
                auto skill=skills::evaluate(p,effect.spec.source.definition,effect.spec.source.level);
                const auto result=ports_.missiles.spawn({actor,skill,{},p.position,true});
                if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
            }
        }
    }
    if(!blocked) recovery.previous=p.position;
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
