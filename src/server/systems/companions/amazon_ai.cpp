#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <cmath>
namespace d2x::server::companions {
StepStatus System::amazonStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *owner=ports_.players.find(pet.owner);
    if(!body || !owner || !owner->entered || owner->persistent.player.hp<=0 || body->amazonPet->decoy || tick.tick<body->busyUntil || ports_.monsters.frozen(body->id,tick.tick)) return StepStatus::Complete;
    const auto &area=ports_.areas.at(owner->area);const ActorContext context{owner->player,owner->actor,owner->area,area.generation,0,tick.tick};
    if(body->area!=owner->area || missileDistance(body->position,owner->position)>50) {
        for(int radius=1;radius<=4;++radius) for(int x=-radius;x<=radius;++x) for(int y=-radius;y<=radius;++y) {
            if(std::max(std::abs(x),std::abs(y))!=radius) continue;
            const Vec at{std::floor(owner->position.x)+x+.5f,std::floor(owner->position.y)+y+.5f};
            if(ports_.monsters.warpPet(pet.actor,context,at)) {pet.nextDecision=tick.tick+1;return StepStatus::Complete;}
        }
        return StepStatus::Complete;
    }
    if(tick.tick<pet.nextDecision) return StepStatus::Complete;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    EntityId target;int nearest=36;
    const auto cast=ports_.skills.read().casts.find(owner->actor);
    if(cast!=ports_.skills.read().casts.end()) {
        const auto candidate=participants.find(cast->second.target);
        if(candidate.aliveIn(body->area) && participants.canHarm(body->id,candidate.id()) && missileDistance(body->position,candidate.position())<nearest) target=candidate.id();
    }
    if(!target) for(const auto id:participants.enemies(body->id,body->area)) {
        const auto candidate=participants.find(id);
        const int distance=missileDistance(body->position,candidate.position());
        if(distance<nearest && area.definition.collision.segment(body->position,candidate.position())) {nearest=distance;target=id;}
    }
    auto random=body->combatRandom;
    if(target) {
        const auto enemy=participants.find(target);
        if(meleeDistance(body->position,body->rule.size,enemy.position(),enemy.size())<=body->rule.meleeRange && area.definition.collision.segment(body->position,enemy.position())) {
            ports_.monsters.stop(pet.actor);
            const bool attack=int(limitedRandom(random,100))<body->amazonPet->attackChance;
            ports_.monsters.commitCombatRandom(body->id,random);
            if(attack) {
                const auto result=requestAction(pet,{pet.actor,0,UnitTarget{target,0,uint8_t(enemy.player?0:1)},tick.tick});
                if(isDeferred(result.status)) return StepStatus::Blocked;
            }
        } else ports_.monsters.requestMove({pet.actor,{body->area,area.generation,enemy.position()},target,body->rule.meleeRange,75,false});
    } else if(missileDistance(body->position,owner->position)>4) ports_.monsters.requestMove({pet.actor,{body->area,area.generation,owner->position},owner->actor,4,75,false});
    pet.nextDecision=tick.tick+uint64_t(std::max(1,body->amazonPet->thinkFrames));return StepStatus::Complete;
}
}
