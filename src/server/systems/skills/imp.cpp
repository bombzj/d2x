#include "system.hpp"
#include "runtime.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/spatial/system.hpp"
#include <cmath>

namespace d2x::server::skills {
DomainResult<> System::ejectImp(EntityId id,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto *carrier=ports_.monsters.find(id);
    if(!carrier || carrier->alive() || carrier->rule.ai.kind!=MonsterAiKind::SiegeBeast) return done();
    auto &execution=runtime_->impEjections[id];if(execution.complete) return done();
    if(!execution.prepared) {
        const auto *rider=ports_.monsters.find(carrier->nativeOwner);
        if(!rider || !rider->alive() || rider->rule.ai.kind!=MonsterAiKind::Imp) {execution.complete=true;return done();}
        execution.rider=rider->id;
        if(carrier->alignment()==UnitAlignment::Evil && rider->rule.skillIds[0]) {
            const auto &area=ports_.areas.at(rider->area).definition;const auto point=rider->position+area.origin;
            const auto room=ports_.areas.roomAtPosition(rider->area,rider->position,int(std::floor(point.x)),int(std::floor(point.y)));
            if(room) if(const auto landing=ports_.areas.nativeFreeCoordinates(*room,int(std::floor(point.x)),int(std::floor(point.y)),
                rider->rule.size,0x3c01,ports_.spatial.nativeCollisionSample(0x3c01)))
                execution.landing=landing->position+ports_.areas.at(landing->room.area).definition.origin-area.origin;
        }
        execution.kill=!execution.landing;execution.prepared=true;
    }
    const auto *rider=ports_.monsters.find(execution.rider);if(!rider) {execution.complete=true;return done();}
    if(!execution.kill) {
        if(!execution.interrupted) {
            cancel({},rider->id);ports_.monsters.finishAction(rider->id,tick);execution.interrupted=true;
        }
        const auto skill=rider->rule.skillIds[0];
        const auto cast=requestCast({rider->id,skill,UnitTarget{},tick,rider->rule.skillActions.at(skill).nativeMode,execution.landing});
        if(isDeferred(cast.status) || cast.status==DomainStatus::Conflict || cast.status==DomainStatus::Stale) return {DomainStatus::Capacity,{}};
        if(cast) {
            ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,rider->id},FrameEventType::AiThink,0);
            execution.complete=true;return done();
        }
        execution.kill=true;
    }
    const auto killed=ports_.monsters.scriptDeath(execution.rider,tick);if(killed) execution.complete=true;
    return killed;
}
}
