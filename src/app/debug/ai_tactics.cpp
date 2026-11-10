#include "ai_impl.hpp"
#include <algorithm>
#include <limits>
#include <cmath>
#include "ai_combat_observation.hpp"
#include "world/collision.hpp"

namespace d2x {
using debug_ai::position;
using debug_ai::point;
bool debug_ai::CombatSight::clear(Vec from,Vec to) const {
    // Keep the attack mask independent from walking WALL/occupancy rules.
    return grid && grid->collisionSegment(from-offset+Vec{.5f,.5f},to-offset+Vec{.5f,.5f},mask);
}

int DebugAi::Impl::routeAttackRange(const Context &c) const {
    const auto primary=battle.excursionSkill.value_or(definitions->skill("Attack"));
    int range=meleeAttack(c,primary)?3:policy.range;
    if(policy.groupSkill && attackRankQualified(c,*policy.groupSkill,bool(battleAura(c))) &&
        !meleeAttack(c,*policy.groupSkill) && c.supplies && c.supplies->skillUsable &&
        c.supplies->skillUsable(*policy.groupSkill).value_or(false)) range=std::max(range,policy.range);
    return range;
}
std::optional<OnlinePoint> DebugAi::Impl::selectCombatLanding(const Context &c,OnlinePoint center,
        const RouteCombatTask &task,bool transit) const {
    if(!area || !battle.excursionSkill) return {};
    const auto origin=c.town.read().origin;
    const auto *memory=c.walk.navigationSearch().explorationMemory.area(*area);
    if(!origin || !memory) return {};
    const auto &view=c.session.read();const Vec offset{float(origin->x),float(origin->y)};
    const auto grid=c.town.navigationGrid(view);const TeleportRooms rooms(c);
    const auto hazards=visibleThreats(c);const auto enemies=travelLandingThreats(c);
    const int range=meleeAttack(c,*battle.excursionSkill)?3:policy.range;
    const auto mask=definitions->attackMasks.find(*battle.excursionSkill);
    const uint16_t attackMask=mask==definitions->attackMasks.end()?uint16_t(4):mask->second;
    const Vec destination{float(center.x),float(center.y)},forward=destination-c.position;
    std::optional<OnlinePoint> best;float bestScore=std::numeric_limits<float>::lowest();
    // A bounded local stance search uses current assignments and loaded-room
    // legality. It neither changes the terrain route nor predicts distant units.
    for(int y=std::max(0,int(center.y)-range);y<=std::min(65535,int(center.y)+range);++y)
        for(int x=std::max(0,int(center.x)-range);x<=std::min(65535,int(center.x)+range);++x) {
            const Vec at{float(x),float(y)},delta=at-c.position;const float span=delta.length();
            if(span>mobilityPolicy.distance || !memory->stand(x,y) || !rooms.contains(at) ||
                !tacticalPositionAllowed(c,at) || threatClearance(hazards,at)<0 ||
                !grid.walkable(at-offset+Vec{.5f,.5f},playerMovement)) continue;
            if(transit && (span<2 || span>=forward.length() || delta.x*forward.x+delta.y*forward.y<=0)) continue;
            if(span>=2 && !c.combat.teleportPathAllowed(*area,grid,c.position-offset,at-offset)) continue;
            float clearance=float(range);
            for(const auto enemy:enemies) clearance=std::min(clearance,(at-enemy).length());
            // A firing stance needs room to remain there. Merely being less
            // crowded than a transit landing does not make it a safe stop.
            if(clearance<float(range>3?policy.kiteMinDistance:2)) continue;
            const bool canAttack=std::any_of(task.targets.begin(),task.targets.end(),[&](const auto &entry) {
                const auto found=view.world.units.find(entry.first);
                if(found==view.world.units.end() || found->second.assignmentRevision!=entry.second || !found->second.position) return false;
                const auto &unit=found->second;const Vec target{float(unit.position->x),float(unit.position->y)};
                return (at-target).length()<=range && c.combat.skillTargetEligible(unit,*battle.excursionSkill) &&
                    grid.collisionSegment(at-offset+Vec{.5f,.5f},target-offset+Vec{.5f,.5f},attackMask);
            });
            if(!canAttack) continue;
            const float score=transit?-span:std::min(clearance,float(policy.kiteMinDistance))-.1f*span;
            if(score>bestScore) {bestScore=score;best=OnlinePoint{uint16_t(x),uint16_t(y)};}
        }
    return best;
}
bool DebugAi::Impl::approachEnemy(Context &c,const OnlineUnit &enemy,std::string purpose,int attackRange) {
    if(!enemy.position) return false;
    auto *knowledge=observeTerrain(c);const auto origin=c.town.read().origin;
    if(!knowledge || !origin) {wait("combat-observation","Waiting for acquired approach terrain");return true;}
    const auto skill=attackRange && battle.combatTarget==enemy.key && battle.combatRun.skillIndex<battle.combatRun.skills.size()?
        battle.combatRun.skills[battle.combatRun.skillIndex]:battle.excursionSkill.value_or(policy.skill.value_or(definitions->skill("Attack")));
    const bool melee=meleeAttack(c,skill);
    const int range=attackRange?attackRange:melee?3:std::min(policy.range,policy.engage);
    const auto mask=definitions->attackMasks.find(skill);
    const uint16_t attackMask=skill==definitions->skill("Attack") && !melee?4:
        mask==definitions->attackMasks.end()?4:mask->second;
    const Vec target{float(enemy.position->x),float(enemy.position->y)},offset{float(origin->x),float(origin->y)};
    const auto &terrain=c.town.map()->grid;
    const auto hazards=visibleThreats(c);
    std::vector<DebugExplorationMemory::Cell> stances;
    for(int y=std::max(0,int(target.y)-range);y<=std::min(65535,int(target.y)+range);++y)
        for(int x=std::max(0,int(target.x)-range);x<=std::min(65535,int(target.x)+range);++x) {
            const Vec at{float(x),float(y)},local=at-offset+Vec{.5f,.5f};
            if((at-target).length()>range || !knowledge->stand(x,y) || !tacticalPositionAllowed(c,at) ||
                threatClearance(hazards,at)<0 ||
                !terrain.collisionSegment(local,target-offset+Vec{.5f,.5f},attackMask)) continue;
            stances.emplace_back(x,y);
        }
    // A real enemy supplies an identity and an attack perimeter, never a
    // standable centre. Hunt/clear/drop and rescue gates share this route model.
    if(approachPoint(c,*enemy.position,std::move(purpose),0,false,{},std::move(stances))) return true;
    wait("combat-ready","At an observed attack stance; awaiting normal combat selection");return true;
}
std::optional<OnlinePoint> DebugAi::Impl::selectCombatPosition(Context &c,const CombatSelection &selection,
        const CombatSight &sight,std::span<const Vec> localThreats,bool retreat,int range) {
    const auto &view=c.session.read();const Vec offset=sight.offset;
    const auto &chosen=selection.chosen;const Vec targetPosition=position(*chosen->position);
    auto &run=battle.combatRun;
    if(!sight.grid) return {};
    if(!retreat && run.triedPositions.size()>=128) return {};
    const auto grid=c.town.navigationGrid(view);const auto hazards=visibleThreats(c);
    const auto safety=[&](Vec at) {
        float nearestEnemy=(at-targetPosition).length();
        for(const auto enemy:localThreats) nearestEnemy=std::min(nearestEnemy,(at-enemy).length());
        return nearestEnemy;
    };
    struct Candidate {OnlinePoint point;float score;};std::vector<Candidate> candidates;
    std::set<std::pair<uint16_t,uint16_t>> unique;
    const float baseline=safety(c.position);
    // Short radii are essential in corridors and small boss chambers.
    for(int radius:retreat?std::vector<int>{2,std::max(2,policy.retreat/2),policy.retreat}:
        std::vector<int>{2,4,8,std::max(2,range/2),std::max(2,range-2)}) for(int direction=0;direction<16;++direction) {
        const float angle=float(direction)*6.28318530718f/16;
        const Vec center=retreat?c.position:targetPosition;
        const Vec at=center+Vec{std::cos(angle),std::sin(angle)}*float(radius);
        if(at.x<0 || at.y<0 || at.x>65535 || at.y>65535) continue;
        const auto destination=point(at);const auto exact=position(destination);
        if(!unique.emplace(destination.x,destination.y).second || !tacticalPositionAllowed(c,exact) ||
            !grid.nativeMovementSegment(exact-offset+Vec{.5f,.5f},exact-offset+Vec{.5f,.5f},playerMovement) ||
            threatClearance(hazards,exact)<0) continue;
        if(!retreat && (run.triedPositions.contains({destination.x,destination.y}) || !sight.clear(exact,targetPosition))) continue;
        const float targetDistance=(exact-targetPosition).length();
        // Preferred firing range is a score, not a hard exclusion of
        // the only legal close stance in a narrow chamber or corridor.
        if(!retreat && (targetDistance>range || targetDistance<(range<=3?1.f:2.f))) continue;
        if(retreat && safety(exact)<=baseline+1) continue;
        const float score=(retreat?safety(exact):-std::abs(targetDistance-std::max(2.f,float(range)*.75f))+.1f*safety(exact))-
            .05f*(exact-c.position).length();
        candidates.push_back({destination,score});
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto &a,const auto &b){return a.score>b.score;});
    // Cheap ranking precedes full paths. Budget yields preserve the
    // search cache, and no failed candidate is retried ahead of a better one.
    for(const auto &candidate:candidates) {
        const auto destination=candidate.point;const auto exact=position(destination);
        if(teleportAvailable(c)) {
            if((exact-c.position).length()<=mobilityPolicy.distance && TeleportRooms(c).contains(exact)) return destination;
            continue;
        }
        const auto path=c.walk.navigationSearch().path(*sight.grid,c.position-offset+Vec{.5f,.5f},exact-offset+Vec{.5f,.5f},false,playerMovement,true);
        if(path.empty() || point(path.back()+offset-Vec{.5f,.5f})!=destination || !tacticalPathSafe(c,path,offset,hazards)) continue;
        return destination;
    }
    return {};
}
std::optional<OnlinePoint> DebugAi::Impl::selectKitePosition(Context &c,const CombatSelection &selection,
        const CombatSight &sight,std::span<const Vec> localThreats,int range) {
    const auto &view=c.session.read();const Vec offset=sight.offset;
    const auto &chosen=selection.chosen;const Vec targetPosition=position(*chosen->position);
    auto &run=battle.combatRun;
    const auto &enemies=selection.enemies;
    if(!sight.grid) return {};
    const auto hazards=visibleThreats(c);const auto grid=c.town.navigationGrid(view);
    const auto &rooms=c.town.navigationRooms();
    const auto contains=[](const auto &room,Vec at) {return room.loaded && at.x>=room.x && at.y>=room.y && at.x<room.x+room.width && at.y<room.y+room.height;};
    const auto source=std::find_if(rooms.begin(),rooms.end(),[&](const auto &room){return room.level==area && contains(room,c.position);});
    bool teleport=teleportAvailable(c);
    if(teleport && c.supplies && c.supplies->skillUsable)
        if(const auto usable=c.supplies->skillUsable(definitions->skill("Teleport"));usable && !*usable) teleport=false;
    Vec center=targetPosition;unsigned count=1;
    for(const auto &enemy:enemies) if(enemy.key!=chosen->key) {center=center+position(*enemy.position);++count;}
    center={center.x/count,center.y/count};
    const Vec radial=c.position-center;const float length=radial.length();
    const Vec tangent=length>0?Vec{-radial.y/length,radial.x/length}:Vec{1,0};
    const auto clearance=[&](Vec at) {
        float nearest=(at-targetPosition).length();
        for(const auto enemy:localThreats) nearest=std::min(nearest,(at-enemy).length());
        return nearest;
    };
    struct Candidate {OnlinePoint point;float score;};std::vector<Candidate> candidates;
    const float minimum=std::min(float(policy.kiteMinDistance),float(range)*.65f);
    const float baseline=clearance(c.position),hazardBaseline=threatClearance(hazards,c.position);
    auto &assessment=battle.combatDiagnostics["kiteAssessment"];
    assessment={{"enemyClearance",baseline},{"hazardClearance",hazardBaseline},{"desiredClearance",minimum},
        {"teleportSeconds",navigation.teleportTiming.seconds?Json(*navigation.teleportTiming.seconds):Json(nullptr)}};
    // A valid shot with adequate separation beats optional orbiting.
    // These are AI spacing preferences, not monster reach/damage rules.
    if(baseline>=minimum && hazardBaseline>=1) {
        assessment["result"]="hold-effective-position";return {};
    }
    assessment["trigger"]=hazardBaseline<1?"observed-hazard":"close-enemies";
    assessment["result"]="no-improving-position";
    for(const int radius:{policy.kiteDistance,std::max(3,policy.kiteDistance/2)}) for(int direction=0;direction<16;++direction) {
        const float angle=float(direction)*6.28318530718f/16;
        const Vec at=c.position+Vec{std::cos(angle),std::sin(angle)}*float(radius);
        if(at.x<0 || at.y<0 || at.x>65535 || at.y>65535) continue;
        const auto destination=point(at);const Vec exact=position(destination),delta=exact-c.position;
        const float distance=delta.length(),toTarget=(exact-targetPosition).length(),safe=clearance(exact);
        const float hazardSafety=threatClearance(hazards,exact);
        if(distance<3 || toTarget>range || safe<minimum || !tacticalPositionAllowed(c,exact) || !sight.clear(exact,targetPosition) || threatClearance(hazards,exact)<1 ||
            !grid.nativeMovementSegment(exact-offset+Vec{.5f,.5f},exact-offset+Vec{.5f,.5f},playerMovement)) continue;
        if(hazardBaseline>=1 && safe<baseline+1) continue;
        if(hazardBaseline<1 && hazardSafety<hazardBaseline+1) continue;
        const auto landing=std::find_if(rooms.begin(),rooms.end(),[&](const auto &room){return room.level==area && contains(room,exact);});
        if(landing==rooms.end() || (teleport && (source==rooms.end() || (landing!=source &&
            std::find(source->near.begin(),source->near.end(),size_t(landing-rooms.begin()))==source->near.end())))) continue;
        const float turn=(delta.x*tangent.x+delta.y*tangent.y)*run.orbitDirection;
        candidates.push_back({destination,turn*.7f+std::min(safe,float(range))*.5f-
            std::abs(toTarget-float(range)*.8f)+std::min(threatClearance(hazards,exact),8.f)*.1f-distance*.05f});
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto &first,const auto &second){return first.score>second.score;});
    // Score cheap local observations first. At most four candidates may
    // consume the shared full-path budget; Teleport validates known landings.
    for(size_t i=0;i<std::min<size_t>(4,candidates.size());++i) {
        const auto destination=candidates[i].point;const auto exact=position(destination);
        const auto record=[&](std::optional<float> walking) {
            assessment["result"]="improve-safety";assessment["destination"]={destination.x,destination.y};
            assessment["candidateEnemyClearance"]=clearance(exact);
            assessment["candidateHazardClearance"]=threatClearance(hazards,exact);
            assessment["walkSeconds"]=walking?Json(*walking):Json(nullptr);
            assessment["movement"]=!teleport || (walking && navigation.teleportTiming.seconds && *walking<=*navigation.teleportTiming.seconds)?"walk":"teleport";
        };
        if(teleport) {
            if(TeleportRooms(c).contains(exact) && (exact-c.position).length()<=mobilityPolicy.distance &&
                c.combat.teleportPathAllowed(*area,grid,c.position-offset,exact-offset)) {
                record(kiteWalkSeconds(c,exact));return destination;
            }
            continue;
        }
        const auto path=c.walk.navigationSearch().path(*sight.grid,c.position-offset+Vec{.5f,.5f},exact-offset+Vec{.5f,.5f},false,playerMovement,true);
        if(path.empty() || point(path.back()+offset-Vec{.5f,.5f})!=destination || path.size()>size_t(policy.kiteDistance*3+4) ||
            !tacticalPathSafe(c,path,offset,hazards)) continue;
        if(std::any_of(path.begin(),path.end(),[&](Vec p) {const Vec at=p+offset-Vec{.5f,.5f};return (at-c.position).length()>2 &&
            (clearance(at)<std::min(4.f,minimum) || threatClearance(hazards,at)<0);})) continue;
        float length=0;Vec previous=c.position-offset+Vec{.5f,.5f};
        for(const auto at:path) {length+=(at-previous).length();previous=at;}
        record(c.supplies && c.supplies->movementSecondsPerUnit?
            std::optional<float>{length * *c.supplies->movementSecondsPerUnit}:std::nullopt);
        return destination;
    }
    return {};
}
} // namespace d2x
