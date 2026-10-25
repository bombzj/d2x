#include "../system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/social/system.hpp"
#include "gameplay/units/distance.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::companions {
DomainResult<bool> System::maintainPd2Hireling(Companion &pet,const PreparedHireling &prepared,TickContext tick) {
    const auto *owner=ports_.players.find(pet.owner);const auto *body=ports_.monsters.find(pet.actor);
    if(!owner || !body || !owner->rules.skills->pd2 || prepared.actions.empty()) return {DomainStatus::Applied,false};
    const auto &action=prepared.actions.front();
    // Core 0x35A030 precedes Fn061's weighted selection. Chance0 does not
    // disable a first-slot aura, nor require an enemy to be present.
    if(action.mode==1 && action.aura && !ports_.effects.hirelingAuraActive(pet.actor,action.skill,action.rank)) {
        const auto applied=ports_.effects.hirelingAura(pet.actor,*action.aura,tick.tick);
        if(!applied) return {applied.status,{}};
        pet.nextDecision=tick.tick+10;return {DomainStatus::Applied,true};
    }
    if(!prepared.maintainPartyBuff || action.state<0 || body->area!=owner->area) return {DomainStatus::Applied,false};
    if(ports_.areas.at(body->area).definition.town && !owner->rules.skills->definitions.at(action.skill).allowedInTown)
        return {DomainStatus::Applied,false};
    const auto needsBuff=[&](EntityId id,RegionId area,Vec position) {
        return area==body->area && unitCoordinateDistance(int(std::floor(body->position.x)),int(std::floor(body->position.y)),
            int(std::floor(position.x)),int(std::floor(position.y)))<30 &&
            !ports_.effects.unitStates(id,tick.tick).contains(action.state);
    };
    bool cast=needsBuff(owner->actor,owner->area,owner->position) ||
        (needsBuff(body->id,body->area,body->position) &&
            unitCoordinateDistance(int(body->position.x),int(body->position.y),int(owner->position.x),int(owner->position.y))<30);
    // Core 0x356A00 checks other party members/pets for five frames every
    // hundred frames, without consuming the mercenary's random stream.
    if(!cast && tick.tick%100<5) {
        for(const auto &[id,player]:ports_.players.all()) if(player.entered && player.persistent.player.hp>0 &&
            ports_.social.sameParty(owner->player,id) && needsBuff(player.actor,player.area,player.position)) {cast=true;break;}
        if(!cast) for(const auto &[id,ally]:state_.companions) {
            const auto *target=ports_.monsters.find(id);
            if(id!=body->id && ally.owner==owner->player && target && target->alive() && !target->inactivePet &&
                needsBuff(id,target->area,target->position)) {cast=true;break;}
        }
    }
    if(!cast) return {DomainStatus::Applied,false};
    const auto requested=requestAction(pet,{pet.actor,uint16_t(action.skill),UnitTarget{{},0,1},tick.tick,uint8_t(action.mode)});
    if(!requested) return {requested.status,{}};
    pet.nextDecision=tick.tick+10;return {DomainStatus::Applied,true};
}
}
