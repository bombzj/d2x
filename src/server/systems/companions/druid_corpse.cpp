#include "druid_corpse.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/units/distance.hpp"
namespace d2x::server::companions {
EntityId findDruidCorpse(const AreaStore &areas,const PlayerStore &players,const monsters::System &monsters,const social::System &social,EntityId source,int radius,uint64_t tick) {
    const auto *body=monsters.find(source);if(!body || radius<0) return {};
    const auto &area=areas.at(body->area);const auto *room=area.definition.activation.room(body->position);if(!room) return {};
    const auto index=size_t(room-area.definition.activation.rooms().data());
    const combat::Participants participants{players,monsters,social};
    for(const auto near:area.definition.roomNear.at(index)) for(const auto id:areas.roomUnitOrder(near)) {
        const auto *corpse=monsters.find(id);if(!corpse || !monsters.nativeUsableCorpse(id,tick) || !participants.canHarm(source,id)) continue;
        const auto point=corpse->position+areas.at(corpse->area).definition.origin-area.definition.origin;
        const int x=int(body->position.x)-int(point.x),y=int(body->position.y)-int(point.y);
        if(x*x+y*y<=radius*radius) return id;
    }
    return {};
}
}
