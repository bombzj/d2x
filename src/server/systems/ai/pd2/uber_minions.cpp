#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/monsters/pd2/boss_program.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <algorithm>

namespace d2x::server::ai {
std::optional<StepStatus> System::pd2UberMinions(EntityId id,std::optional<NativeAiTarget> target,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);
    const auto pool=pd2::uberMinionPool(unit.identity.nativeClass);
    if(pool.empty() || (!target && unit.identity.nativeClass!=709)) return {};
    const auto &area=ports_.areas.at(unit.area).definition;
    const auto chance=pd2UnitEventRandom(controller.random)%100;
    const int type=unit.identity.nativeClass;
    if((type==705 && chance>=30) || (type==709 && chance>=20)) return {};
    std::array<int,3> counts{};std::vector<EntityId> cleanup;
    if(const auto *room=area.activation.room(unit.position))
        for(const auto near:area.roomNear.at(size_t(room-area.activation.rooms().data())))
            for(const auto otherId:ports_.areas.roomUnitOrder(near)) {
                const auto *other=ports_.monsters.find(otherId);if(!other || otherId==id) continue;
                const int definition=other->identity.nativeClass;
                if(other->nativeMode==12) {
                    if(definition!=704 && definition!=705 && definition!=709) cleanup.push_back(otherId);
                    continue;
                }
                if(definition>=725 && definition<=730) ++counts[0];
                else if(definition==712) ++counts[1];
                else if(definition==731 || definition==732) ++counts[2];
            }
    const bool selected=type==704?counts[0]<40 && int(chance)<(60-counts[0])*2:type==705?counts[1]<15:counts[2]<20;
    std::optional<PointTarget> position;int definition=-1;
    if(selected) {
        definition=pool[pd2CrtRandom(ports_.pd2Random)%pool.size()];
        const int radius=std::clamp(int(unit.rule.ai.pd2Distances[size_t(unit.rule.difficulty)])-(target?target->distance:INT32_MAX),3,20);
        const auto origin=target?target->position:Vec{}-area.origin;
        const int x=int(origin.x)+int(pd2UnitEventRandom(controller.random)%uint32_t(radius*2))-radius;
        const int y=int(origin.y)+int(pd2UnitEventRandom(controller.random)%uint32_t(radius*2))-radius;
        const Vec point{float(x),float(y)};
        const auto free=ports_.world.nativeFreeSpace(unit.area,unit.position,point,3);
        if(free) position=PointTarget{free->area,ports_.areas.at(free->area).generation,free->position};
        if(!position) position=PointTarget{unit.area,ports_.areas.at(unit.area).generation,point};
    }
    if(!selected && cleanup.empty()) return {};
    const auto result=perform(controller,[this,id,cleanup=std::move(cleanup),next=size_t{},position,definition,mode=type==704?8:1,tick]() mutable ->DomainResult<> {
        while(next<cleanup.size()) {
            const auto removed=ports_.monsters.remove(cleanup[next]);if(isDeferred(removed.status)) return removed;
            ++next;
        }
        if(position) {
            const auto born=ports_.monsters.admitPd2NativeChild(id,position->area,position->position,definition,mode,0x4020000,true,
                tick.tick,ports_.spatial.nativeCollisionSample());
            if(isDeferred(born.status) || born.status==DomainStatus::NotImplemented) return {born.status,{}};
        }
        return {DomainStatus::Applied,std::monostate{}};
    });
    if(!result) return isDeferred(result.status)?StepStatus::Blocked:StepStatus::NotImplemented;
    // The delegate continues from the live seed after this prefix. Its own
    // subsequent draws must not reuse the already-published loan flag.
    controller.random=controller.randomExpected=ports_.monsters.find(id)->combatRandom;controller.randomPublished=false;
    return {};
}
}
