#include "system.hpp"
#include "timed_treasure.hpp"
#include "server/area_store.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/loot/system.hpp"
#include <algorithm>

namespace d2x::server::maps {
DomainResult<> System::objectTreasureEvent(const FrameEvent &event,TickContext tick) {
    const auto id=event.owner.unit;
    const auto found=state_.objectTreasures.find({id,event.payload});
    if(found==state_.objectTreasures.end()) return {DomainStatus::Applied,std::monostate{}};
    auto &job=found->second;
    const auto finish=[&] {
        if(job.retained) ports_.world.retainControllerUnitRoom(id,job.area,job.position,false,pd2ObjectTreasureTimerHandler);
        job.retained=false;return DomainResult<>{DomainStatus::Applied,std::monostate{}};
    };
    const auto source=ports_.objects.read().objects.find(id);
    if(source==ports_.objects.read().objects.end() || job.remaining<1 || job.remaining>=2000) return finish();
    if(!job.waiting) {
        // 3729C0 indexes both authored arrays by remaining & 15. The
        // callback restores the object's original room/coordinates afterwards.
        static constexpr std::array<int,16> x{10,9,7,4,0,-4,-7,-9,-10,-9,-7,-4,0,4,7,9};
        static constexpr std::array<int,16> y{0,4,7,9,10,9,7,4,0,-4,-7,-9,-10,-9,-7,-4};
        const auto index=size_t(job.remaining&15);
        const auto point=job.position+Vec{float(x[index]),float(y[index])};
        ports_.world.activate(job.area,point);
        if(!ports_.areas.loaded(job.area,point)) return {DomainStatus::PendingContent,{}};
        LootRequest actor;actor.source=id;actor.region=job.area;
        loot::Request request{event.payload,actor,{},point};request.environment=true;request.reportCompletion=true;
        request.nativeTreasure=loot::NativeTreasure{job.treasure,1};
        const auto queued=ports_.loot.queue(request);if(!queued) return queued;
        job.waiting=true;
    }
    const auto completed=ports_.loot.takeCompletionDetail(id);
    if(!completed) return {DomainStatus::PendingContent,{}};
    job.remaining-=int(std::min(completed->created,size_t(INT32_MAX)));job.waiting=false;
    if(job.remaining<=0) return finish();
    auto timer=ports_.frameEvents.prepareInsertion({{},{FrameEventUnit::Object,id},FrameEventType::MonsterModifier,
        tick.tick+5,false,pd2ObjectTreasureTimerHandler,job.remaining,0,event.payload});
    ports_.frameEvents.publish(std::move(timer));return {DomainStatus::Applied,std::monostate{}};
}
}
