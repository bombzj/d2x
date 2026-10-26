#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/maps/pd2/region_program.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <array>
#include <string_view>

namespace d2x::server::maps {
DomainResult<> System::advanceOuterVoidDeath(const monsters::Actor &unit,Death &death,TickContext tick) {
    using Phase=Death::Phase;
    if(death.phase==Phase::Counter) {
        // 41d49 3792C0 consumes the three death-context membership flags.
        // Advance the stage once, before asynchronous reward preparation.
        const int stage=pd2::outerVoidDeathStage(unit.identity.nativeClass);
        death.remaining=stage && state_.phase==stage-1?stage:0;
        if(death.remaining) {state_.phase=uint8_t(stage);death.phase=Phase::Reward;}
        else death.phase=Phase::Cleanup;
    }
    if(death.phase==Phase::Reward) {
        // Ordered DWORD item-code set, 41d49 constructor 19A060. MPQ still
        // owns every item definition, property and quality qualification.
        static constexpr std::array<std::string_view,9> codes{
            "lucb","lucc","lucd","cm2f","dcbl","dcho","rtmo","dcso","rtmv"};
        if(!death.reward) death.reward=pd2CrtRandom(ports_.random)%codes.size();
        if(!death.rewardPositionPrepared) prepareDropPosition(unit,death);
        LootRequest source;source.source=unit.id;source.identity=unit.identity;
        source.region=unit.area;source.difficulty=unit.rule.difficulty;
        loot::Request request{unit.deathOccurrence,source,{},unit.position};
        request.environment=true;request.reportCompletion=true;
        loot::NativeDrop drop;drop.code=codes.at(*death.reward);drop.quality=ItemQuality::Normal;drop.level=130;
        drop.exactPosition=true;drop.placementValid=death.rewardPosition.has_value();
        if(death.rewardPosition) {request.source.region=death.rewardPosition->area;request.position=death.rewardPosition->position;}
        request.nativeDrops.push_back(std::move(drop));
        const auto queued=ports_.loot.queue(request);if(!queued) return queued;
        death.phase=Phase::Wait;
    }
    if(death.phase==Phase::Wait) {
        if(!ports_.loot.takeCompletion(unit.id)) return {DomainStatus::PendingContent,{}};
        death.phase=Phase::Portal;
    }
    if(death.phase==Phase::Portal && death.remaining==3) {
        const auto ready=ports_.world.requestMapArea(RegionId{109},state_.instance);
        if(!ready) return {ready.status,{}};
        if(!ports_.areas.find(RegionId{109})) return {DomainStatus::PendingContent,{}};
        if(!death.portalPosition) {
            const auto point=ports_.world.nativeFreeSpace(unit.area,unit.position,unit.position,1);
            if(point && point->area==unit.area) death.portalPosition=point->position;
        }
        if(death.portalPosition) {
            auto portal=ports_.travel.prepareNativePermanentPortal(unit.area,*death.portalPosition,109,tick.tick,586);
            if(isDeferred(portal.status)) return {portal.status,{}};
            if(portal) {
                const auto &link=portal.value->next.rbegin()->second;
                ports_.world.retainControllerRoom(link.field,link.fieldPosition,true);
                ports_.world.retainControllerRoom(link.town,link.townPosition,true);
                ports_.travel.commitSpecialPortal(std::move(*portal.value));
            }
        }
        death.phase=Phase::Cleanup;
    }
    if(death.phase==Phase::Portal) {
        const Vec offset=death.remaining==1?Vec{485,930}:Vec{845,590};
        const pd2::MapPortalProgram program{180,0,587,2,1,{},false,offset};
        const auto portal=advanceDeathPortal(unit.area,death,program,585,unit.position-Vec{1,1},2,tick);
        if(!portal) return portal;
        death.phase=Phase::Cleanup;
    }

    if(death.phase==Phase::Cleanup) {
        // 379550 follows this callback for all three sets, independently of
        // the current scene stage. Its count/materials have a separate cursor.
        death.phase=Phase::Complete;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
