#include "system.hpp"
#include "timed_treasure.hpp"
#include "server/area_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/monsters/native_birth.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/rules/pd2_random.hpp"

namespace d2x::server::maps {
DomainResult<> System::wandererEvent(const FrameEvent &event,TickContext tick) {
    const auto found=state_.wandererBirths.find({event.owner.unit,event.payload});
    if(found==state_.wandererBirths.end()) return {DomainStatus::Applied,std::monostate{}};
    auto &job=found->second;
    const auto finish=[&] {
        ports_.world.retainControllerUnitRoom(event.owner.unit,job.area,job.source,false,pd2WandererTimerHandler);
        job.complete=true;return DomainResult<>{DomainStatus::Applied,std::monostate{}};
    };
    const auto *source=ports_.monsters.find(event.owner.unit);
    if(job.complete || !source) return finish();
    if(!job.positionPrepared) {
        const auto free=ports_.world.nativeFreeSpace(source->area,source->position,source->position,1);
        if(free) job.position=PointTarget{free->area,ports_.areas.at(free->area).generation,free->position};
        job.positionPrepared=true;
    }
    if(!job.position) return finish();
    const auto &point=*job.position;
    ports_.world.activate(point.area,point.position);
    if(!ports_.areas.loaded(point.area,point.position)) return {DomainStatus::PendingContent,{}};
    const auto &area=ports_.areas.at(point.area).definition;
    while(job.attempt<50 && !job.child) {
        if(!job.selected) {
            job.actBoss=pd2CrtRandom(ports_.random)%3==0;
            const auto &pool=area.pd2WandererPools[job.actBoss?1:0];
            if(pool.empty()) return {DomainStatus::NotImplemented,{}};
            job.definition=pool[pd2CrtRandom(ports_.random)%pool.size()];job.selected=true;
        }
        if(const auto preset=area.pd2WandererPresets.find(job.definition);preset!=area.pd2WandererPresets.end()) {
            const auto born=ports_.population.advancePd2PresetBirth(job.preset,point.area,preset->second,point.position,tick);
            if(!born) return {born.status,{}};
            job.child=*born.value;
        } else {
            const auto born=ports_.monsters.preparePd2WandererSpawn(point.area,point.position,point.position,
                job.definition,job.actBoss,tick.tick,ports_.spatial.nativeCollisionSample());
            if(!born) return {born.status,{}};
            if(!(*born.value)->validate()) return {DomainStatus::Stale,{}};
            job.child=(*born.value)->actor();(*born.value)->publish();
        }
        if(!job.child) {++job.attempt;job.selected=false;job.preset={};}
    }
    if(job.child && area.pd2WandererPresets.contains(job.definition)) {
        const auto buffed=ports_.monsters.applyPd2WandererState(job.child,false,tick.tick);if(!buffed) return buffed;
    }
    return finish();
}
}
