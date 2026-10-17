#include "system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/skills/behavior.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::effects {
StepStatus System::finishContacts(uint64_t tick) {
    if(advanceMonsterCurses(tick)==StepStatus::Blocked) return StepStatus::Blocked;
    return StepStatus::Complete;
}
DomainResult<> System::movementSkills(EntityId id,uint64_t tick,uint8_t direction,Vec nativePosition) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,id);
    if(!caster || !caster->programs()) return done();
    const auto &programs=*caster->programs();const auto &catalog=*programs.formulas;
    auto *states=unitEffects(id);if(!states) return done();
    const auto &area=ports_.areas.at(caster->area());
    auto &phase=movementSkillPhases_[id];
    if(phase==0) {
        const auto blaze=catalog.skillIds.find("blaze");
        if(blaze!=catalog.skillIds.end() && !area.definition.town) {
            const auto *list=states->findState(catalog.skills.at(blaze->second).auraState);
            if(list && states->hasState(list->spec.state.id,tick)) {
                const auto native=combatEffectNativeValues(list->spec);
                if(native) {
                    const StatSnapshot values{*native};
                    const auto skill=values.get({programs.stats->names.at("modifierlist_skill"),0});
                    auto rank=values.get({programs.stats->names.at("modifierlist_level"),0});if(rank<0) rank=1;
                    const auto row=catalog.skills.find(skill);
                    if(row!=catalog.skills.end()) {
                        const auto definition=ports_.missiles.nativeDefinition(row->second.reference("srvmissilea"));
                        if(definition) {
                            const auto result=ports_.missiles.spawnNative({id,caster->area(),area.generation,tick,nativePosition,{},*definition,skill,rank});
                            if(!result) return {result.status,{}};
                        }
                    }
                }
            }
        }
        phase=1;
    }
    if(phase==1) {
        states=unitEffects(id);if(!states) {movementSkillPhases_.erase(id);return done();}
        const auto spider=catalog.skillIds.find("spiderlay");
        if(spider!=catalog.skillIds.end() && states->hasState(catalog.skills.at(spider->second).auraState,tick)) {
            auto input=caster->formulaInput(*this,tick);
            const auto rank=input.referenceRanks->effective.find(spider->second);
            const auto definition=ports_.missiles.nativeDefinition("spidergoolay");
            if(definition) {
                const auto points=monsterWebTrailPoints(nativePosition,direction);
                missiles::NativeSpawn request{id,caster->area(),area.generation,tick,points.first,points.second,*definition,spider->second,
                    rank==input.referenceRanks->effective.end()?0:rank->second};request.nativeVelocity=3;
                const auto result=ports_.missiles.spawnNative(request);if(!result) return {result.status,{}};
            }
        }
        phase=2;
    }
    movementSkillPhases_.erase(id);return done();
}
}
