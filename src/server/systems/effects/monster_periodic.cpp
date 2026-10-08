#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/area_store.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include <algorithm>
namespace d2x::server::effects {
StepStatus System::advanceMonsterSkills(uint64_t tick) {
    bool blocked=false;
    std::erase_if(webTrails_,[&](const auto &entry){const auto *m=ports_.monsters.find(entry.first);return !m || m->life<=0 || m->webUntil<=tick || m->area!=entry.second.area || m->webUntil!=entry.second.until;});
    for(const auto &[id,m]:ports_.monsters.read().actors) {
        if(m.life<=0 || m.webUntil<=tick || !m.rule.web) continue;
        auto [it,fresh]=webTrails_.try_emplace(id);auto &trail=it->second;
        if(fresh) trail={m.webOrigin,{},{},m.area,m.webUntil,m.webRandom,false};
        if(!trail.pending && missileChangedCell(trail.previous,m.position)) {
            const auto points=monsterWebTrailPoints(m.position,m.position-trail.previous);
            trail.origin=points.first;trail.target=points.second;trail.pending=true;
        }
        if(trail.pending) {
            MonsterAttackRule attack;attack.missile=m.rule.web->lay;
            MonsterHitStates states=m.rule.hitStates;states.slow=m.rule.web->slow;
            const auto &area=ports_.areas.at(m.area);
            const auto spawned=ports_.missiles.spawnMonster({id,m.area,area.generation,tick,trail.origin,trail.target,attack,states,m.rule.level,0,0,trail.random,m.rule.web});
            if(spawned.status==DomainStatus::Capacity) {blocked=true;continue;}
            if(spawned) trail.random=spawned.value->random;
            trail.pending=false;
        }
        trail.previous=m.position;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
