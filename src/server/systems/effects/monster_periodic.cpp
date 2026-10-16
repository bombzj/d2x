#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/area_store.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include <algorithm>
namespace d2x::server::effects {
StepStatus System::advanceMonsterSkills(EntityId only,uint64_t tick) {
    bool blocked=false;
    const auto *unit=ports_.monsters.find(only);
    if(!unit || unit->life<=0 || !unit->rule.web || !unitStates(only,tick).contains(unit->rule.web->aura.id)) {webTrails_.erase(only);return StepStatus::Complete;}
    if(const auto found=webTrails_.find(only);found!=webTrails_.end() && (found->second.area!=unit->area || found->second.until!=unit->webUntil)) webTrails_.erase(found);
    for(auto entry=ports_.monsters.read().actors.find(only);entry!=ports_.monsters.read().actors.end() && entry->first==only;++entry) {
        const auto &[id,m]=*entry;
        if(m.life<=0 || (!m.rule.web || !unitStates(id,tick).contains(m.rule.web->aura.id)) || !m.rule.web) continue;
        auto [it,fresh]=webTrails_.try_emplace(id);auto &trail=it->second;
        if(fresh) trail={m.webOrigin,{},{},m.area,m.webUntil,false};
        if(!trail.pending && missileChangedCell(trail.previous,m.position)) {
            const auto points=monsterWebTrailPoints(m.position,m.position-trail.previous);
            trail.origin=points.first;trail.target=points.second;trail.pending=true;
        }
        if(trail.pending) {
            MonsterAttackRule attack;attack.missile=m.rule.web->lay;
            MonsterHitStates states=m.rule.hitStates;states.slow=m.rule.web->slow;
            const auto &area=ports_.areas.at(m.area);
            const auto spawned=ports_.missiles.spawnMonster({id,m.area,area.generation,tick,trail.origin,trail.target,attack,states,m.rule.level,0,0,m.rule.web});
            if(spawned.status==DomainStatus::Capacity) {blocked=true;continue;}
            trail.pending=false;
        }
        trail.previous=m.position;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
