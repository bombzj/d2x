#include "target_query.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <map>
#include <utility>
#include <vector>
namespace d2x::server::ai {
namespace {
struct NativeTargetLists {
    using List=std::vector<std::pair<uint64_t,EntityId>>;
    std::map<PlayerId,List> pets;
    List wanderers,special;
    explicit NativeTargetLists(const monsters::System &monsters) {
        // One read-only query, no cross-callback cache. Keep PlayerPets'
        // eligibility and Targets' reverse insertion order independently.
        for(const auto &[id,unit]:monsters.read().actors) {
            if(unit.owner && unit.petRegistrationOrder && !unit.inactivePet && !unit.conversion)
                pets[*unit.owner].emplace_back(unit.petRegistrationOrder,id);
            if(unit.wanderTargetOrder) wanderers.emplace_back(unit.wanderTargetOrder,id);
            if(unit.specialTargetOrder) special.emplace_back(unit.specialTargetOrder,id);
        }
        const auto sort=[](List &list) {
            std::sort(list.begin(),list.end(),[](const auto &a,const auto &b){return a.first>b.first;});
        };
        for(auto &[owner,list]:pets) {(void)owner;sort(list);}
        sort(wanderers);sort(special);
    }
};
}
NativeBossTarget acquireNativeBossTarget(const NativeAiTargetPorts &ports,const monsters::Actor &source,
    const std::function<int(EntityId)> &score) {
    const auto &area=ports.areas.at(source.area);
    const combat::Participants participants{ports.players,ports.monsters,ports.social};
    NativeBossTarget result;std::optional<NativeAiTarget> alternative;int alternativeScore{};
    const auto position=[&](combat::ParticipantView unit) {
        return unit.position()+ports.areas.at(unit.player?unit.player->area:unit.monster->area).definition.origin-area.definition.origin;
    };
    const auto sameAct=[&](combat::ParticipantView unit) {
        return unit.id() && ports.areas.at(unit.player?unit.player->area:unit.monster->area).definition.act==area.definition.act;
    };
    const auto consider=[&](EntityId id,bool special=false,bool deadHead=false) {
        const auto unit=participants.find(id);if(!unit.id()) return;
        const int value=deadHead?0:score(id);
        auto &maximum=special?alternativeScore:result.score;
        auto &target=special?alternative:result.target;
        if(value>maximum) {
            maximum=value;const auto at=position(unit);
            target=NativeAiTarget{{id,0,uint8_t(unit.monster?1:0)},at,unit.size(),monsterAiDistance(at,0,source.position)};
        }
        if(!special) ++result.count;
    };
    // A4610 walks each player head and its reverse-inserted pet chain. Only
    // the head is culled; a dead head still contributes one to nCounter.
    for(uint8_t slot=0;slot<8;++slot) for(const auto &[key,player]:ports.players.all()) {
        (void)key;if(player.targetNodeIndex!=slot || !player.entered || !sameAct({&player,nullptr})) continue;
        if(uint32_t(monsterAiDistance(position({&player,nullptr}),0,source.position))>=1020) continue;
        consider(player.actor,false,player.persistent.player.hp<=0);
        auto pets=ports.monsters.pets(player.player);
        std::sort(pets.begin(),pets.end(),[&](EntityId left,EntityId right) {
            return ports.monsters.find(left)->petRegistrationOrder>ports.monsters.find(right)->petRegistrationOrder;
        });
        for(const auto id:pets) consider(id);
    }
    std::vector<EntityId> wanderers;
    for(const auto &[id,unit]:ports.monsters.read().actors) if(unit.wanderTargetOrder) wanderers.push_back(id);
    std::sort(wanderers.begin(),wanderers.end(),[&](EntityId left,EntityId right) {
        return ports.monsters.find(left)->wanderTargetOrder>ports.monsters.find(right)->wanderTargetOrder;
    });
    for(const auto id:wanderers) if(sameAct(participants.find(id))) consider(id);
    for(const auto id:ports.monsters.specialTargets()) if(sameAct(participants.find(id))) consider(id,true);
    if(alternative && alternative->distance<5 && (!result.target ||
        !unitMeleeReach(area.definition.collision,source.position,source.rule.size,result.target->position,result.target->size,source.rule.meleeRange))) {
        const Vec center{.5f,.5f};
        if(!result.target || area.definition.collision.nativeUnitPath(source.position+center,result.target->position+center,
            2,source.pathDistance,source.rule.collision).empty()) {
            result.target=alternative;result.score=alternativeScore;
        }
    }
    return result;
}
std::optional<NativeAiTarget> acquireNativeOwnerTarget(const NativeAiTargetPorts &ports,const PlayerState &source) {
    const auto &area=ports.areas.at(source.area);const auto *room=area.definition.activation.room(source.position);
    if(!room) return {};
    const combat::Participants participants{ports.players,ports.monsters,ports.social};
    std::optional<NativeAiTarget> primary,alternative;
    const auto index=size_t(room-area.definition.activation.rooms().data());
    for(const auto near:area.definition.roomNear.at(index)) for(const auto id:ports.areas.roomUnitOrder(near)) {
        const auto target=participants.find(id);
        if(!target.aliveIn(near.area) || !participants.canHarm(source.actor,id)) continue;
        const auto point=target.position()+ports.areas.at(near.area).definition.origin-area.definition.origin;
        const int distance=monsterAiDistance(point,target.monster?target.size():0,source.position);
        if(distance>35) continue;
        auto &selected=target.monster && target.monster->rule.threat<=1?alternative:primary;
        if(!selected || distance<selected->distance) selected=NativeAiTarget{{id,0,uint8_t(target.monster?1:0)},point,target.size(),distance};
    }
    if(alternative && (!primary || (alternative->distance<=5 && area.definition.collision.path(source.position,primary->position,true,{}).empty()))) return alternative;
    return primary;
}
std::optional<NativeAiTarget> acquireNativeAiTarget(const NativeAiTargetPorts &ports_,const monsters::Actor &source,TickContext,bool missileRay,bool requireSight,int searchDistance,uint64_t *borrowedRandom) {
    const auto &area=ports_.areas.at(source.area);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const bool confused=source.curseControl.ai==CurseAi::Confuse;
    std::optional<monsters::Actor> search;
    if(confused) {
        search.emplace(source);
        auto random=borrowedRandom?*borrowedRandom:source.combatRandom;
        const bool flip=(rollRandom(random)&1)!=0;
        const auto original=source.alignment();
        const auto alignment=original==UnitAlignment::Neutral?(flip?UnitAlignment::Good:UnitAlignment::Evil):
            flip?(original==UnitAlignment::Evil?UnitAlignment::Good:UnitAlignment::Evil):original;
        if(!search->nativeStats || !search->rule.stats) return {};
        search->nativeStats->values.values[{search->rule.stats->names.at("alignment"),0}]=int(alignment);
        search->curseControl.ai=CurseAi::None;
        // AI callers already own a candidate seed for this callback. Keep
        // selector and decision draws in that same loan, rather than publishing
        // the selector behind the decision's expected seed.
        if(borrowedRandom) *borrowedRandom=random;
        else if(!ports_.monsters.consumeRandom(source.id,source.combatRandom,random)) return {};
    }
    const auto position=[&](combat::ParticipantView target) {
        const auto region=target.player?target.player->area:target.monster->area;
        return target.position()+ports_.areas.at(region).definition.origin-area.definition.origin;
    };
    const auto alive=[&](combat::ParticipantView target) {
        return target.player?target.player->entered && target.player->persistent.player.hp>0:target.monster && target.monster->alive();
    };
    const auto clearRay=[&](Vec point) {return (!missileRay && !requireSight) || area.definition.collision.missileSegment(source.position,point,{4,1});};
    // MonsterData's direct selector survives list removal until AIRESET. Its
    // target bypasses normal alignment/range acquisition, but never death/rays.
    const auto selector=source.attractionTarget?source.attractionTarget:source.curseControl.ai==CurseAi::Terror?source.curseControl.target:EntityId{};
    if(selector) {
        const auto target=participants.find(selector);
        if(alive(target)) {
            const auto point=position(target);
            if(clearRay(point)) return NativeAiTarget{{target.id(),0,uint8_t(target.monster?1:0)},point,target.size(),monsterAiDistance(point,target.monster?target.size():0,source.position)};
        }
    }
    std::optional<NativeAiTarget> primary,alternative;
    const bool nodeQuery=!confused && !missileRay && source.alignment()==UnitAlignment::Evil;
    const int maximum=searchDistance>0?searchDistance:missileRay?48:nodeQuery?(source.rule.ai.searchDistance?source.rule.ai.searchDistance:35):35;
    const auto consider=[&](EntityId id,bool special=false,bool wander=false) {
        const auto target=participants.find(id);if(!alive(target) || id==source.id || !target.damageable()) return;
        const auto region=target.player?target.player->area:target.monster->area;
        if(ports_.areas.at(region).definition.act!=area.definition.act) return;
        if(!special && !wander && !participants.canHarm({nullptr,search?&*search:&source},target)) return;
        const auto point=position(target);
        const int distance=monsterAiDistance(point,nodeQuery?0:target.monster?target.size():0,source.position);
        if((!special && (nodeQuery?distance>=maximum:distance>maximum)) || !clearRay(point)) return;
        const int threat=target.player?14:target.monster->rule.threat;
        auto &selected=special || (!nodeQuery && !wander && threat<=1)?alternative:primary;
        if(!selected || distance<selected->distance) selected=NativeAiTarget{{id,0,uint8_t(target.monster?1:0)},point,target.size(),distance};
    };
    if(nodeQuery) {
        // Targets::First reserves 0..7; UpdateSummonAI inserts immediately
        // after its player head, independently of PlayerPets' append order.
        const NativeTargetLists targets(ports_.monsters);
        for(uint8_t slot=0;slot<8;++slot) for(const auto &[key,player]:ports_.players.all()) {
            (void)key;if(player.targetNodeIndex!=slot || !player.entered) continue;
            const auto &ownerArea=ports_.areas.at(player.area);
            if(ownerArea.definition.act!=area.definition.act || ownerArea.definition.town ||
               monsterAiDistance(position({&player,nullptr}),0,source.position)>=55) continue;
            consider(player.actor);
            if(const auto pets=targets.pets.find(player.player);pets!=targets.pets.end())
                for(const auto &[order,id]:pets->second) {(void)order;consider(id);}
        }
        for(const auto &[order,id]:targets.wanderers) {(void)order;consider(id,false,true);}
        for(const auto &[order,id]:targets.special) {(void)order;consider(id,true);}
    } else if(const auto *room=area.definition.activation.room(source.position)) {
        const auto current=RoomView{source.area,(int(area.definition.origin.x)+room->x)/5,(int(area.definition.origin.y)+room->y)/5};
        const auto visit=[&](RoomView view) {for(const auto id:ports_.areas.roomUnitOrder(view)) consider(id);};
        if(confused && !missileRay) visit(current);
        else {
            const auto index=size_t(room-area.definition.activation.rooms().data());
            for(const auto near:area.definition.roomNear.at(index)) visit(near);
        }
    }
    if(confused) {
        if(!primary) ports_.monsters.curseControl(source.id,{});
        return primary;
    }
    if(alternative && (!primary || (alternative->distance<=5 &&
       area.definition.collision.path(source.position,primary->position,true,source.rule.collision).empty()))) return alternative;
    return primary;
}
}
