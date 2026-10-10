#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <cmath>
namespace d2x::server::companions {
StepStatus System::amazonStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *owner=ports_.players.find(pet.owner);
    if(!body || !owner || !owner->entered || owner->persistent.player.hp<=0 || body->amazonPet->decoy || tick.tick<body->busyUntil || body->frozenUntil>tick.tick) return StepStatus::Complete;
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
    EntityId target;int nearest=36;
    const auto cast=ports_.skills.read().casts.find(owner->actor);
    if(cast!=ports_.skills.read().casts.end()) {
        const auto *candidate=ports_.monsters.find(cast->second.target);
        if(candidate && candidate->enemyTarget() && candidate->life>0 && candidate->area==body->area && missileDistance(body->position,candidate->position)<nearest) target=candidate->id;
    }
    if(!target) for(const auto &[id,candidate]:ports_.monsters.read().actors) if(candidate.enemyTarget() && candidate.life>0 && candidate.area==body->area) {
        const int distance=missileDistance(body->position,candidate.position);
        if(distance<nearest && area.definition.collision.segment(body->position,candidate.position)) {nearest=distance;target=id;}
    }
    auto random=pet.random;
    if(target) {
        const auto *enemy=ports_.monsters.find(target);
        if(meleeDistance(body->position,body->rule.size,enemy->position,enemy->rule.size)<=body->rule.meleeRange && area.definition.collision.segment(body->position,enemy->position)) {
            ports_.monsters.stop(pet.actor);
            if(int(limitedRandom(random,100))<body->amazonPet->attackChance) {
                const auto result=ports_.skills.requestCast({pet.actor,0,UnitTarget{target,0,1},tick.tick});
                if(result.status==DomainStatus::Capacity) return StepStatus::Blocked;
            }
        } else ports_.monsters.requestMove({pet.actor,{body->area,area.generation,enemy->position},target,body->rule.meleeRange,75,false});
    } else if(missileDistance(body->position,owner->position)>4) ports_.monsters.requestMove({pet.actor,{body->area,area.generation,owner->position},owner->actor,4,75,false});
    pet.random=random;pet.nextDecision=tick.tick+uint64_t(std::max(1,body->amazonPet->thinkFrames));return StepStatus::Complete;
}
}
