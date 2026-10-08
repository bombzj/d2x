#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "core/random.hpp"
namespace d2x::server::ai {
std::optional<StepStatus> System::uniqueAction(EntityId id,UnitTarget target,Vec position,TickContext tick,Controller &controller) {
    const auto &m=*ports_.monsters.find(id);const auto &area=ports_.areas.at(m.area);
    if(m.rule.introduction && !controller.introduced && target.type==0 && monsterAiDistance(position,0,m.position)<20) {
        const auto result=ports_.monsters.introduction(id,tick.tick);
        if(result.status==DomainStatus::Capacity) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
        if(result) {controller.introduced=true;controller.nextDecision=tick.tick+20;return StepStatus::Complete;}
    }
    if(!m.rule.enchantment || !m.rule.enchantment->has(26)) return {};
    auto random=controller.random;const auto &mods=*m.rule.enchantment;
    const int distance=monsterAiDistance(position,0,m.position);
    const bool melee=mods.melee || m.rule.ai.kind==MonsterAiKind::Bighead;
    if(limitedRandom(random,100)>=40 || (m.life*100>=m.maximumLife*30 && (melee || distance>=10)) || limitedRandom(random,100)>=15) {controller.random=random;return {};}
    const auto *room=area.definition.activation.room(m.position);if(!room || room->width<3 || room->height<3) {controller.random=random;return {};}
    for(int attempt=0;attempt<20;++attempt) {
        const Vec point{float(room->x+1+int(limitedRandom(random,unsigned(room->width-1))))+.5f,float(room->y+1+int(limitedRandom(random,unsigned(room->height-1))))+.5f};
        if(!area.definition.collision.walkable(point,{0x3c01,m.rule.size}) || !area.definition.collision.segment(m.position,point,{}, {0x0c01,1})) continue;
        bool occupied=false;
        for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==m.area && p.persistent.player.hp>0 && monsterAiDistance(p.position,0,point)<=m.rule.size) occupied=true;}
        for(const auto &[key,p]:ports_.monsters.read().actors) if(key!=id && p.area==m.area && p.life>0 && monsterAiDistance(p.position,p.rule.size,point)<=m.rule.size) occupied=true;
        if(occupied) continue;
        int heal=0;
        if(m.life*100<m.maximumLife*30 && limitedRandom(random,100)<25) heal=mods.level;
        const auto result=ports_.skills.requestCast({id,184,target,tick.tick,4,point,heal});
        if(result.status==DomainStatus::Capacity) {controller.nextDecision=tick.tick+1;return StepStatus::Blocked;}
        if(result) {controller.random=random;controller.pursuing=false;return StepStatus::Complete;}
    }
    controller.random=random;return {};
}
}
