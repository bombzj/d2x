#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::companions {
std::vector<HirelingPreparation> System::pendingHirelings() const {
    std::vector<HirelingPreparation> result;
    for(const auto &[id,p]:ports_.players.all()) {
        const auto &record=p.persistent.player.hireling;
        if(!p.entered || record.sourceRow<0 || record.hp<=0) continue;
        const auto prior=hirelingRules_.find(id);
        if(prior!=hirelingRules_.end() && prior->second.source.record.sourceRow==record.sourceRow && prior->second.source.record.level==record.level && prior->second.source.actor.actor==p.actor) continue;
        result.push_back({{id,p.actor,p.area,ports_.areas.at(p.area).generation,0,0},record,p.persistent.difficulty});
    }
    return result;
}
DomainResult<> System::install(PreparedHireling prepared) {
    const auto *p=ports_.players.find(prepared.source.actor.player);
    if(!p || !p->entered || p->actor!=prepared.source.actor.actor || p->persistent.player.hireling.sourceRow!=prepared.source.record.sourceRow || p->persistent.player.hireling.level!=prepared.source.record.level) return {DomainStatus::Stale,{}};
    hirelingRules_.insert_or_assign(p->player,std::move(prepared));return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::synchronizeHirelings(TickContext tick) {
    std::erase_if(hirelingRules_,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered;});
    bool blocked=false;
    for(const auto &[id,prepared]:hirelingRules_) {
        const auto *p=ports_.players.find(id);if(!p || !p->entered || p->persistent.player.hp<=0 || p->persistent.player.hireling.hp<=0 || !prepared.deferred.empty()) continue;
        bool present=false;for(const auto &[key,pet]:state_.companions) {(void)key;if(pet.owner==id && pet.kind==Kind::Hireling) {present=true;break;}}
        if(present) continue;
        const auto &area=ports_.areas.at(p->area);ActorContext actor{id,p->actor,p->area,area.generation,0,tick.tick};
        for(int radius=1;radius<=4 && !present;++radius) for(int x=-radius;x<=radius && !present;++x) for(int y=-radius;y<=radius && !present;++y) {
            if(std::max(std::abs(x),std::abs(y))!=radius) continue;
            const Vec at{std::floor(p->position.x)+x+.5f,std::floor(p->position.y)+y+.5f};
            if(!area.definition.collision.walkable(at,prepared.rule.collision)) continue;
            bool occupied=false;
            for(const auto &[key,m]:ports_.monsters.read().actors) {(void)key;if(m.life>0 && m.area==p->area && (m.position-at).length()<float((m.rule.size+prepared.rule.size)/2)) {occupied=true;break;}}
            if(occupied || (p->position-at).length()<float((2+prepared.rule.size)/2)) continue;
            const auto maximum=int64_t(prepared.rule.minimumLife)*256;
            // Native save records alive/dead, not the old room's current life.
            auto bodies=ports_.monsters.prepareHireling(actor,prepared.rule,prepared.code,at,maximum,initialRandom(p->persistent.player.hireling.seed));
            if(!bodies) {blocked|=bodies.status==DomainStatus::Capacity;continue;}
            auto next=state_.companions;const auto key=bodies.value->begin()->first;
            Companion pet{key,id,Kind::Hireling,{}};pet.expires=UINT64_MAX;pet.nextDecision=tick.tick+uint64_t(prepared.think);pet.random=initialRandom(p->persistent.player.hireling.seed);
            next.emplace(key,std::move(pet));ports_.monsters.commitHydra(std::move(*bodies.value));state_.companions.swap(next);present=true;
        }
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
StepStatus System::hirelingStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *p=ports_.players.find(pet.owner);
    if(!body || !p) return StepStatus::Complete;
    const auto rule=hirelingRules_.find(pet.owner);if(rule==hirelingRules_.end() || !rule->second.deferred.empty()) return StepStatus::Complete;
    const auto &prepared=rule->second;const auto &area=ports_.areas.at(p->area);
    ActorContext actor{p->player,p->actor,p->area,area.generation,0,tick.tick};
    const float life=float(body->life)/256.f;
    if(p->persistent.player.hireling.hp!=life) {
        transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};edit.player.hireling.hp=life;
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan || !ports_.transactions.commit(std::move(*plan.value))) return StepStatus::Blocked;
    }
    if(body->life<=0 || p->persistent.player.hp<=0) {ports_.monsters.stop(pet.actor);return StepStatus::Complete;}
    if(body->area!=p->area || missileDistance(body->position,p->position)>prepared.warp) {
        for(int radius=1;radius<=4;++radius) for(int x=-radius;x<=radius;++x) for(int y=-radius;y<=radius;++y) {
            if(std::max(std::abs(x),std::abs(y))!=radius) continue;
            const Vec at{std::floor(p->position.x)+x+.5f,std::floor(p->position.y)+y+.5f};
            if(ports_.monsters.warpPet(pet.actor,actor,at)) {pet.nextDecision=tick.tick+1;return StepStatus::Complete;}
        }
        return StepStatus::Complete;
    }
    if(tick.tick<pet.nextDecision || tick.tick<body->busyUntil || body->frozenUntil>tick.tick) return StepStatus::Complete;
    if(missileDistance(body->position,p->position)>prepared.follow) {
        ports_.monsters.requestMove({pet.actor,{body->area,area.generation,p->position},p->actor,16,75,false});pet.nextDecision=tick.tick+uint64_t(prepared.think);return StepStatus::Complete;
    }
    EntityId target;int nearest=prepared.vision;
    if(!area.definition.town) for(const auto &[id,enemy]:ports_.monsters.read().actors) {
        if(enemy.owner || enemy.life<=0 || enemy.area!=body->area) continue;
        const int distance=missileDistance(body->position,enemy.position);
        if(distance<nearest && area.definition.collision.missileSegment(body->position,enemy.position,{4,1})) {nearest=distance;target=id;}
    }
    auto random=pet.random;
    if(target) {
        unsigned total=unsigned(prepared.defaultChance);for(const auto &action:prepared.actions) total+=unsigned(action.chance);
        const unsigned roll=limitedRandom(random,total+1);unsigned end=unsigned(prepared.defaultChance);
        const HirelingAction *chosen=nullptr;
        if(roll>=end) for(const auto &action:prepared.actions) {end+=unsigned(action.chance);if(roll<=end) {chosen=&action;break;}}
        DomainResult<> result;
        if(chosen && chosen->magic) result=ports_.effects.amazonMagic(actor,*chosen->magic,pet.actor);
        else result=ports_.skills.requestCast({pet.actor,uint16_t(chosen?chosen->skill:prepared.rule.skillIds[0]),UnitTarget{target,0,1},tick.tick,uint8_t(chosen?chosen->mode:4)});
        if(result.status==DomainStatus::Capacity) return StepStatus::Blocked;
        pet.nextDecision=tick.tick+uint64_t(chosen && chosen->magic?10:prepared.think);
    } else {
        if(missileDistance(body->position,p->position)>4) ports_.monsters.requestMove({pet.actor,{body->area,area.generation,p->position},p->actor,4,75,false});
        pet.nextDecision=tick.tick+uint64_t(prepared.think);
    }
    pet.random=random;return StepStatus::Complete;
}
}
