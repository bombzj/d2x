#include "ai_impl.hpp"
#include "content/world/world_catalog.hpp"
#include "gameplay/units/coordinates.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace d2x {
namespace {
bool roomContains(const RemoteTown::NavigationRoom &room,Vec at) {
    return room.loaded && at.x>=room.x && at.y>=room.y && at.x<room.x+room.width && at.y<room.y+room.height;
}
}
DebugAi::Impl::TeleportTargets::TeleportTargets(const Context &c):rooms(c.town.navigationRooms()) {
    if(const auto snapshot=c.town.navigationSnapshot(true)) {
        terrain=c.walk.navigationSearch().teleportGrid(c.town);offset={float(snapshot->originX),float(snapshot->originY)};
    }
    const auto feet=DebugExplorationMemory::playerCell(c.position);
    for(const auto &room:rooms) if(room.level==c.town.read().area && roomContains(room,{float(feet.first),float(feet.second)})) {
        current=&room;break;
    }
}
bool DebugAi::Impl::TeleportTargets::contains(Vec at) const {
    if(!current) return false;
    if(roomContains(*current,at)) return true;
    return std::any_of(current->near.begin(),current->near.end(),[&](size_t i) {
        return i<rooms.size() && roomContains(rooms[i],at);
    });
}
bool DebugAi::Impl::TeleportTargets::permits(const Context &c,Vec at,int range) const {
    const auto level=c.town.read().area;
    if(!level || !terrain || !std::isfinite(at.x) || !std::isfinite(at.y) ||
        at.x<0 || at.y<0 || at.x>65535 || at.y>65535) return false;
    // The planner and normal combat input use native integer feet; the
    // emitted point uses floor(landing). A fractional display position must
    // not veto a boundary-range edge that is legal in those same coordinates.
    const Vec from=nativeFeetCell(c.position),target{std::floor(at.x),std::floor(at.y)};
    return (target-from).length()<=range && contains(target) &&
        terrain->walkable(target-offset+Vec{.5f,.5f},playerMovement) &&
        c.combat.teleportPathAllowed(*level,*terrain,from-offset,target-offset);
}
bool DebugAi::Impl::knownSkill(const Context &c,uint16_t id,bool left) const {
    return std::any_of(c.combat.skills().begin(),c.combat.skills().end(),[&](const auto &s){
        return s.id==id && !s.passive && (s.level || s.innate) && (!left || s.left);
    });
}
bool DebugAi::Impl::selectSkill(Context &c,uint16_t skill,OnlineSkillHand hand) {
    const auto &world=c.observation.read().world;
    const auto selected=hand==OnlineSkillHand::Left?world.leftSkill:world.rightSkill;
    if(selected && selected->skill==skill && selected->owner==UINT32_MAX) return true;
    const auto now=Clock::now();
    if((world.combatRequest && world.combatRequest->state==OnlineCombatRequest::State::Pending) ||
        now-navigation.lastSelection<std::chrono::milliseconds(100)) return false;
    navigation.lastSelection=now;OnlineCombatCommand request;request.action=OnlineCombatCommand::Action::SelectSkill;
    request.hand=hand;request.skill=skill;
    submitCombat(c,request,DebugActionOwner::Combat,"skill-select");
    return false; // A queued selection is never a confirmed hand assignment.
}
bool DebugAi::Impl::teleportAvailable(const Context &c,bool resourceReady) const {
    return area && !navigation.townExitArea && !c.town.read().town && travelMobility(c,*area,resourceReady).first>0;
}
std::pair<int,bool> DebugAi::Impl::travelMobility(const Context &c,int level,bool resourceReady) const {
    // Price each leg in its own level: being in town does not prevent a
    // later field leg from using a learned, currently usable Teleport.
    if(std::find(actTownLevels.begin(),actTownLevels.end(),level)!=actTownLevels.end() ||
        (mobilityPolicy.mode!="auto" && mobilityPolicy.mode!="teleport") ||
        !knownSkill(c,definitions->skill("Teleport"))) return {};
    if(resourceReady && c.supplies) {
        const auto &eligible=c.town.read().town?c.supplies->skillFieldUsable:c.supplies->skillUsable;
        if(eligible) if(const auto usable=eligible(definitions->skill("Teleport"));usable && !*usable) return {};
    }
    for(size_t row=0;row<definitions->levels.rows().size();++row)
        if(definitions->levels.number(row,"Id")==level) {
            const int rule=definitions->levels.number(row,"Teleport").value_or(0);
            if(!rule || (level==area && c.town.read().town)) return {};
            return {mobilityPolicy.distance,rule==2};
        }
    return {};
}
bool DebugAi::Impl::observeMobility(Context &c) {
    const auto now=Clock::now();const auto &world=c.observation.read().world;
    // Observe the crossing independently of the movement chosen for it.
    // A walking child must be able to unlock field casting with its receipt.
    if(navigation.townExitArea && world.playerPosition) {
        const auto at=*world.playerPosition;
        const auto &rooms=c.town.navigationRooms();
        if(std::any_of(rooms.begin(),rooms.end(),[&](const auto &room) {
            return room.level==*navigation.townExitArea && at.x>=room.x && at.x<room.x+room.width &&
                at.y>=room.y && at.y<room.y+room.height &&
                world.rooms.contains({uint8_t(room.level),uint16_t(room.x/5),uint16_t(room.y/5)});
        })) {
            navigation.townExitArea.reset();
            note("town-exit-position-observed","Server position now belongs to the field; travel casting is eligible");
        }
    }
    if(navigation.mobilityTarget) {
        // Observe the normal path's relocation/progress. Casting acceptance
        // never writes coordinates or proves arrival at the intended endpoint.
        const Vec target{float(navigation.mobilityTarget->x),float(navigation.mobilityTarget->y)};
        const auto player=world.units.find({0,c.observation.read().load.playerUnitId.value_or(0)});
        const bool progress=navigation.mobilityTeleport?player!=world.units.end() && player->second.positionDiscontinuity!=navigation.mobilityDiscontinuity:
            (c.position-target).length()<3 || (c.position-navigation.mobilityFrom).length()>3;
        if(progress) {
            const auto at=navigation.mobilityTeleport && world.playerPosition?
                Vec{float(world.playerPosition->x),float(world.playerPosition->y)}:c.position;
            const float miss=(at-target).length();
            const bool arrived=(!navigation.mobilityTeleport || world.playerPosition.has_value()) && miss<=4 &&
                miss<(navigation.mobilityFrom-target).length();
            const bool relocated=navigation.mobilityTeleport && world.playerPosition && (at-navigation.mobilityFrom).length()>=1;
            const bool releaseRelay=relocated && navigation.walkIntent.routeRelay &&
                navigation.walkIntent.point==navigation.mobilityTarget;
            if(relocated) {
                auto &search=c.walk.navigationSearch();
                const DebugExplorationMemory::Cell requested{navigation.mobilityTarget->x,navigation.mobilityTarget->y};
                search.observeTeleportLanding(requested,DebugExplorationMemory::playerCell(at));
                // A server-corrected landing is a displacement, not evidence
                // that the requested ground is permanently unavailable.
                if(navigation.mobilityGoal)
                    navigation.mobilityTrials.erase({navigation.mobilityArea,navigation.mobilityGoal->x,navigation.mobilityGoal->y,
                        navigation.mobilityTarget->x,navigation.mobilityTarget->y});
            }
            if(arrived) {
                if(navigation.mobilityTeleport && c.town.read().origin) {
                    const auto origin=*c.town.read().origin;
                    const Vec offset{float(origin.x)-.5f,float(origin.y)-.5f};
                    c.walk.navigationSearch().confirmTeleportLanding(target-offset,at-offset);
                }
                // A near landing can be short of its requested route sample.
                navigation.mobilityRouteProgress=std::max(navigation.mobilityRouteProgress,navigation.mobilityPendingProgress-miss);
                // A confirmed arrival is not a failed landing attempt. A
                // later legitimate return to this point keeps the same goal.
                if(navigation.mobilityGoal) {
                    navigation.mobilityTrials.erase({navigation.mobilityArea,navigation.mobilityGoal->x,navigation.mobilityGoal->y,navigation.mobilityTarget->x,navigation.mobilityTarget->y});
                }
                if(navigation.routeExecution && navigation.routeExecution->phase==RouteExecution::Phase::Relocate &&
                    navigation.mobilityTarget==navigation.routeExecution->position) navigation.routeExecution->relocated=true;
            }
            note(arrived?"mobility-progress-observed":"mobility-relocated-off-target",
                Json{{"requested",{target.x,target.y}},{"actual",{at.x,at.y}},{"relayReleased",releaseRelay}}.dump());
            navigation.mobilityTarget.reset();navigation.nextMobility=now;
            if(releaseRelay) cancelWalk(c);
            return false;
        }
        if(now>=navigation.mobilityDeadline) {
            note("mobility-no-progress","No confirmed displacement; reconsider the next movement input");
            navigation.mobilityTarget.reset();navigation.nextMobility=now+std::chrono::seconds(3);return false;
        }
        navigation.mobilityHold=true;wait("mobility-wait","Waiting for normal skill movement/position feedback");return true;
    }
    return false;
}
std::vector<Vec> DebugAi::Impl::travelLandingThreats(const Context &c) const {
    std::vector<Vec> result;
    if(!policy.enabled || c.town.read().town) return result;
    for(const auto &[key,enemy]:c.observation.read().world.units)
        if(key.type==1 && enemy.position && c.combat.hostileSource(enemy) && !onlineMonsterCorpse(enemy) &&
            (!c.combat.hostile(enemy) || c.combat.monsterTargetEligible(enemy,false)))
            result.push_back({float(enemy.position->x),float(enemy.position->y)});
    return result;
}
bool DebugAi::Impl::crowdedTravelLanding(std::span<const Vec> threats,Vec at) const {
    int nearby=0;
    for(const Vec enemy:threats) if((at-enemy).length()<4 && ++nearby>=std::max(1,policy.crowd)) return true;
    return false;
}
// Ordinary Teleport ignores intermediate walking obstacles. Restricted levels
// additionally check the native flying ray; actors never block that ray.
std::optional<DebugAi::Impl::TeleportApproach> DebugAi::Impl::teleportLanding(Context &c,Vec wanted,bool exploration,bool directOnly) const {
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    if(!map || !origin) return {};
    const TeleportTargets neighborhood(c);
    if(!neighborhood.current) return {};
    const auto *knowledge=c.walk.navigationSearch().explorationMemory.area(*area);
    auto &search=c.walk.navigationSearch();
    const auto permitted=[&](Vec at) {
        if(!neighborhood.permits(c,at,mobilityPolicy.distance)) return false;
        if(!knowledge || !knowledge->stand(int(at.x),int(at.y))) return false;
        const auto trial=std::tuple{*area,uint16_t(wanted.x),uint16_t(wanted.y),uint16_t(at.x),uint16_t(at.y)};
        return !navigation.mobilityTrials.contains(trial) || navigation.mobilityTrials.at(trial)<2;
    };
    if(permitted(wanted)) return TeleportApproach{wanted,true};
    if(directOnly) return {}; // A local stance or final walking segment needs its own endpoint.
    const auto *memory=search.explorationMemory.area(*area);
    if(!memory) return {};
    bool restricted=false;
    for(size_t row=0;row<definitions->levels.rows().size();++row)
        if(definitions->levels.number(row,"Id")==area) restricted=definitions->levels.number(row,"Teleport").value_or(0)==2;
    const auto route=search.observedRoute(*area,c.position,mobilityPolicy.distance,restricted,wanted,1);
    for(auto it=route.points.rbegin();it!=route.points.rend();++it) {
        const Vec anchor{float(it->x),float(it->y)};
        if((anchor-nativeFeetCell(c.position)).length()<2) continue;
        if(permitted(anchor)) return TeleportApproach{anchor,route.outcome==DebugExplorationMemory::Plan::Outcome::Goal};
    }
    (void)exploration;
    return {};
}

bool DebugAi::Impl::recoverExplorationLanding(Context &c,const DebugExplorationMemory::Area &memory) {
    if(navigation.mobilityTarget) return true;
    if(c.moving || Clock::now()<navigation.nextMobility || !teleportAvailable(c) ||
        !canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) return false;
    const auto feet=DebugExplorationMemory::playerCell(c.position);
    const int range=mobilityPolicy.distance;
    auto &recovery=navigation.landingRecovery;
    if(!recovery || recovery->revision!=memory.revision || recovery->from!=feet || recovery->range!=range)
        recovery=debug_ai::NavigationState::LandingRecovery{memory.revision,feet,range,
            DebugExplorationMemory::teleportReturnAnchors(memory,feet,range)};
    for(const auto &[x,y]:recovery->anchors) {
        const Vec anchor{float(x),float(y)};
        if(!teleportLanding(c,anchor,true,true)) continue;
        navigation.searchDiagnostics["result"]="return-to-connected-ground";
        navigation.searchDiagnostics["landingRecovery"]={x,y};
        wait("explore-landing-return","Returning to a confirmed prior position through a legal Teleport");
        navigation.mobilityGoal=OnlinePoint{uint16_t(x),uint16_t(y)};
        navigation.mobilityArea=memory.level;
        const bool waiting=submitMobilityCast(c,anchor,anchor,true,0);
        if(navigation.mobilityTarget) note("exploration-landing-return-submitted",Json{{"from",feet},{"to",{x,y}}}.dump());
        return waiting;
    }
    return false;
}

std::optional<float> DebugAi::Impl::kiteWalkSeconds(const Context &c,Vec destination) const {
    if(!c.supplies || !c.supplies->movementSecondsPerUnit || !area ||
        !std::isfinite(*c.supplies->movementSecondsPerUnit) || *c.supplies->movementSecondsPerUnit<=0) return {};
    const auto origin=c.town.read().origin;
    const auto *knowledge=c.walk.navigationSearch().explorationMemory.area(*area);
    if(!origin || !knowledge) return {};
    const Vec offset{float(origin->x),float(origin->y)};
    const auto grid=c.town.navigationGrid(c.observation.read());
    if(!grid.nativeMovementSegment(c.position-offset+Vec{.5f,.5f},destination-offset+Vec{.5f,.5f},playerMovement)) return {};
    const auto hazards=visibleThreats(c);
    const auto enemies=travelLandingThreats(c);
    const auto clearance=[&](Vec at) {
        float nearest=64;for(const auto enemy:enemies) nearest=std::min(nearest,(at-enemy).length());return nearest;
    };
    const float baseline=clearance(c.position),length=(destination-c.position).length();
    const int samples=std::max(1,int(std::ceil(length*2)));
    // Only compare a clear direct walk which stays outside observed hazards
    // and never closes on the observed group. Crossing walls/doors or escaping
    // a dangerous field keeps Teleport's existing priority.
    for(int i=0;i<=samples;++i) {
        const Vec at=c.position+(destination-c.position)*(float(i)/samples);
        const auto cell=DebugExplorationMemory::playerCell(at);
        if(knowledge->mask(cell.first,cell.second)==0xffff || !tacticalPositionAllowed(c,at) ||
            threatClearance(hazards,at)<1 || clearance(at)<baseline) return {};
    }
    return length * *c.supplies->movementSecondsPerUnit;
}

bool DebugAi::Impl::mobility(Context &c) {
    if(navigation.mobilityTarget) return observeMobility(c);
    if(c.walk.openingRouteDoor()) return false;
    const auto now=Clock::now();const auto &world=c.observation.read().world;
    if(navigation.townExitArea) {
        navigation.mobilityHold=now<navigation.townExitObserveUntil;
        wait("mobility-town-exit",navigation.mobilityHold?"Waiting briefly for the host's field position while the crossing completes":
            "Walking into the field until the host position confirms the crossing");
        return navigation.mobilityHold;
    }
    const bool encounterMove=navigation.routeExecution && navigation.routeExecution->phase==RouteExecution::Phase::Relocate;
    const bool combatMovement=encounterMove || (!battle.combatOwned && (battle.retreatPoint || battle.combatRun.reposition || battle.combatRun.kitePoint || battle.hazardDodge || phase=="combat-approach"));
    const bool combatActive=battle.combatTarget && battle.combatRun.suspendedAt==Clock::time_point{} && !combatMovement;
    if(combatActive || !c.walk.active() || now<navigation.nextMobility || world.npcRequested || world.waypointSource ||
        portalUsed || navigation.travelPhase==TravelPhase::WaypointWait || navigation.travelPhase==TravelPhase::CrossWait) {
        navigation.mobilityDiagnostics["skip"]={{"combat",combatActive},{"retainedCombat",bool(battle.combatTarget)},{"childActive",c.walk.active()},
            {"cooldown",now<navigation.nextMobility},{"npc",bool(world.npcRequested)},{"waypoint",bool(world.waypointSource)},
            {"portalUsed",portalUsed},{"travelPhase",unsigned(navigation.travelPhase)}};
        return false;
    }
    const auto child=c.walk.observe();const auto &target=child.target;
    std::optional<Vec> destination;
    if(target) destination=Vec{float(target->x),float(target->y)};
    // A newly created unit walk has not ticked yet. Resolve its already
    // assigned target now so Teleport can own the first approach frame.
    const auto &childUnit=child.unit;
    if(!destination && bool(childUnit)) {
        const auto unit=world.units.find(*childUnit);
        if(unit!=world.units.end() && unit->second.position)
            destination=Vec{float(unit->second.position->x),float(unit->second.position->y)};
    }
    if(!destination) return false;
    Vec wanted=*destination;
    Vec delta=wanted-c.position;float distance=delta.length();
    const bool assignedApproach=bool(childUnit) && world.units.contains(*childUnit);
    if(assignedApproach && bool(navigation.walkIntent) && navigation.walkIntent.interact.value_or(false) &&
        c.town.interactionReady(c.observation.read(),*childUnit,c.position))
        return false; // A normal click is already possible; do not spend a cast on the object's center.
    const auto *map=c.town.map();const auto origin=c.town.read().origin;if(!map || !origin || distance<1) return false;
    const Vec offset{float(origin->x),float(origin->y)};
    // Walk a few clear cells to the same objective. A nearby point across a
    // wall can still warrant a cast, as can a deliberate defensive escape.
    const bool shortApproach=distance<8 && !combatMovement && !encounterMove;
    if(shortApproach) {
        // Walking is an execution choice, not a property of terrain alone.
        // A confirmed Teleport may land beside its requested point while an
        // assigned monster still occupies the last one or two walking cells.
        const auto live=c.town.navigationGrid(c.observation.read(),childUnit);
        const bool clear=live.nativeMovementSegment(c.position-offset+Vec{.5f,.5f},wanted-offset+Vec{.5f,.5f},playerMovement);
        navigation.mobilityDiagnostics={{"decision",decisions},{"area",area.value_or(0)},
            {"from",{c.position.x,c.position.y}},{"target",{wanted.x,wanted.y}},
            {"shortWalkClear",clear},{"result",clear?"short-live-walk":"short-walk-occupied"}};
        if(clear) return false;
    }
    const auto charge=definitions->skill("Charge"),vigor=definitions->skill("Vigor");
    const bool canTeleport=teleportAvailable(c);
    if(canTeleport && battle.combatRun.kitePoint && bool(navigation.walkIntent) &&
        navigation.walkIntent.purpose.value_or(std::string{})=="combat-kite") {
        const auto walking=kiteWalkSeconds(c,wanted);
        navigation.mobilityDiagnostics["kiteCost"]={{"walkSeconds",walking?Json(*walking):Json(nullptr)},
            {"teleportSeconds",navigation.teleportTiming.seconds?Json(*navigation.teleportTiming.seconds):Json(nullptr)}};
        if(walking && navigation.teleportTiming.seconds && *walking<=*navigation.teleportTiming.seconds) {
            navigation.mobilityDiagnostics["skip"]="safe direct kite walk costs no more than the observed Teleport cast";
            return false;
        }
    }
    if(distance<8 && !canTeleport) {navigation.mobilityDiagnostics["skip"]="short distance without Teleport";return false;}
    const bool canCharge=!canTeleport && !c.town.read().town && knownSkill(c,charge,true) &&
        (mobilityPolicy.mode=="auto" || mobilityPolicy.mode=="charge");
    const bool useVigor=mobilityPolicy.vigor && !canTeleport && knownSkill(c,vigor);
    if(useVigor && !selectSkill(c,vigor,OnlineSkillHand::Right)) {
        wait("mobility-select","Walking while waiting for normal Vigor selection");return false;
    }
    if(!canTeleport && !canCharge) {navigation.mobilityDiagnostics["skip"]="no usable travel skill";return false;}
    const auto threats=travelLandingThreats(c);
    // Density affects a local tactical stop, never the geometric transit.
    const auto crowdedLanding=[&](Vec at){return !canTeleport && battle.excursionSkill && !meleeAttack(c,*battle.excursionSkill) && crowdedTravelLanding(threats,at);};
    const TeleportTargets neighborhood(c);
    if(canTeleport && assignedApproach && navigation.walkIntent.interact.value_or(false)) {
        const OnlineUnitKey key=*childUnit;
        const auto entry=std::find_if(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),
            [&](const auto &value){return value.unit==key;});
        if(entry!=c.town.read().mapTargets.end()) {
            // An authored portal/door center can be solid or beyond a chasm.
            // Choose a real standing cell in its normal interaction perimeter,
            // rather than requiring a walking route to that center first.
            const int width=std::clamp(entry->collisionWidth/2+4,4,16),height=std::clamp(entry->collisionHeight/2+4,4,16);
            std::optional<Vec> perimeter;float best=std::numeric_limits<float>::max();
            for(int y=std::max(0,int(entry->position.y)-height);y<=std::min(65535,int(entry->position.y)+height);++y)
                for(int x=std::max(0,int(entry->position.x)-width);x<=std::min(65535,int(entry->position.x)+width);++x) {
                    const Vec at{float(x),float(y)};
                    const float span=(at-c.position).length();
                    if(span<1 || !neighborhood.permits(c,at,mobilityPolicy.distance) ||
                        !c.town.interactionReady(c.observation.read(),key,at)) continue;
                    if(span<best) {best=span;perimeter=at;}
                }
            if(perimeter) {wanted=*perimeter;distance=(wanted-c.position).length();}
        }
    }
    const Vec localGoal=wanted-offset+Vec{.5f,.5f};
    const OnlinePoint goal{uint16_t(wanted.x),uint16_t(wanted.y)};
    if(navigation.mobilityGoal!=goal || navigation.mobilityArea!=area.value_or(0)) {
        navigation.mobilityGoal=goal;navigation.mobilityArea=area.value_or(0);navigation.mobilityRoute.clear();
        navigation.mobilityRouteProgress=navigation.mobilityPendingProgress=navigation.mobilityRouteLength=0;
    }
    const bool exploration=bool(navigation.walkIntent) && navigation.walkIntent.exploration.value_or(false);
    const bool occupiedGoal=crowdedTravelLanding(threats,wanted);
    std::optional<Vec> stagedLanding;
    bool completeTeleportRoute=false;
    if(canTeleport) {
        if(const auto approach=teleportLanding(c,wanted,exploration,encounterMove || shortApproach)) {
            if(!encounterMove || (approach->landing-wanted).length()<1) stagedLanding=approach->landing;
            completeTeleportRoute=approach->completeRoute;
        }
    }
    if(canTeleport && stagedLanding && !combatMovement &&
        beginTeleportTransit(c,{uint16_t(stagedLanding->x),uint16_t(stagedLanding->y)})) return true;
    const bool directLanding=stagedLanding.has_value();
    if(canTeleport && !directLanding) {
        navigation.mobilityDiagnostics={{"decision",decisions},{"area",area.value_or(0)},{"movement","teleport"},
            {"from",{c.position.x,c.position.y}},{"target",{wanted.x,wanted.y}},
            {"shortApproach",shortApproach},{"result","no-known-teleport-landing"}};
        return false;
    }
    if(!canTeleport && navigation.mobilityRoute.empty()) {
        ++navigation.pathSearches;
        const auto route=c.walk.navigationSearch().travelRoute(map->grid,c.position-offset+Vec{.5f,.5f},localGoal);
        if(!route.points.empty() && (route.points.back()-localGoal).length()<=1.f) {
            navigation.mobilityRoute.push_back(route.origin+offset-Vec{.5f,.5f});
            for(const auto node:route.points) {
                const auto at=node+offset-Vec{.5f,.5f};
                navigation.mobilityRouteLength+=(at-navigation.mobilityRoute.back()).length();navigation.mobilityRoute.push_back(at);
            }
        }
    }
    navigation.mobilityDiagnostics={{"decision",decisions},{"area",area.value_or(0)},{"movement",canTeleport?"teleport":"charge"},
        {"from",{c.position.x,c.position.y}},{"target",{wanted.x,wanted.y}},
        {"shortApproach",shortApproach},
        {"goalOccupied",occupiedGoal},{"stagedLanding",stagedLanding?Json{stagedLanding->x,stagedLanding->y}:Json(nullptr)},
        {"completeTeleportRoute",completeTeleportRoute},
        {"routeLength",navigation.mobilityRouteLength},{"confirmedRouteProgress",navigation.mobilityRouteProgress},
        {"remainingRoute",std::max(0.f,navigation.mobilityRouteLength-navigation.mobilityRouteProgress)},
        {"routePoints",navigation.mobilityRoute.size()},{"candidates",Json::array()},{"selected",nullptr}};
    navigation.mobilityDiagnostics["nativeRoom"]=neighborhood.current?Json{{neighborhood.current->level,neighborhood.current->x,neighborhood.current->y}}:Json(nullptr);
    if(!directLanding && navigation.mobilityRoute.empty()) {
        navigation.mobilityDiagnostics["result"]="no-complete-known-route-to-child-target";return false;
    }
    std::optional<Vec> landing;float selectedProgress=navigation.mobilityRouteProgress,best=-1e9f;
    unsigned candidates=0;
    if(directLanding) {
        const Vec at=*stagedLanding;
        const auto trial=std::tuple{area.value_or(0),goal.x,goal.y,uint16_t(at.x),uint16_t(at.y)};
        if(!neighborhood.permits(c,at,mobilityPolicy.distance) ||
            (navigation.mobilityTrials.contains(trial) && navigation.mobilityTrials.at(trial)>=2)) {
            navigation.mobilityDiagnostics["result"]="direct landing blocked or attempts exhausted";return false;
        }
        landing=at;
    } else {
        float travelled=0;
        for(size_t i=1;i<navigation.mobilityRoute.size();++i) {
            const Vec from=navigation.mobilityRoute[i-1],leg=navigation.mobilityRoute[i]-from;const float length=leg.length();
            if(length>0) for(float t=std::min(4.f,length);;t=std::min(t+4.f,length)) {
                const float progress=travelled+t;
                const Vec sample=from+leg*(t/length);
                Vec point{std::floor(sample.x+.5f),std::floor(sample.y+.5f)};
                Vec local=point-offset+Vec{.5f,.5f};
                const float span=(point-c.position).length();
                if(progress>navigation.mobilityRouteProgress+1 && span>=6 && span<=mobilityPolicy.distance &&
                    point.x>=0 && point.y>=0 && point.x<=65535 && point.y<=65535) {
                    const auto trial=std::tuple{area.value_or(0),goal.x,goal.y,uint16_t(point.x),uint16_t(point.y)};
                    Json trace{{"point",{point.x,point.y}},{"routeProgress",progress}};
                    ++candidates;
                    if(crowdedLanding(point)) trace["result"]="enemy-too-close-to-ranged-landing";
                    else if(!map->grid.nativeMovementSegment(local,local,playerMovement)) trace["result"]="unavailable-landing";
                    else if(navigation.mobilityTrials.contains(trial) && navigation.mobilityTrials.at(trial)>=2) trace["result"]="landing-attempt-limit";
                    else if(canCharge && (!map->grid.segment(c.position-offset+Vec{.5f,.5f},local,{},playerMovement) ||
                        !map->grid.nativeMovementSegment(c.position-offset+Vec{.5f,.5f},local,playerMovement)))
                        trace["result"]="charge-ray-blocked";
                    else {
                        int clearance=0;
                        if(exploration) clearance=c.walk.navigationSearch().wallClearance(map->grid,local);
                        else for(int radius:{2,4,6}) {
                            bool clear=true;
                            for(int x:{-1,0,1}) for(int y:{-1,0,1}) if(x || y)
                                clear=clear && map->grid.walkable(local+Vec{float(x*radius),float(y*radius)},playerMovement);
                            if(!clear) break;
                            clearance=radius;
                        }
                        const float score=progress+(exploration?-DebugNavigationSearch::wallCost(clearance):.25f*clearance);
                        trace["result"]="candidate";trace["score"]=score;
                        if(score>best) {best=score;landing=point;selectedProgress=progress;}
                    }
                    if(navigation.mobilityDiagnostics["candidates"].size()<15)
                        navigation.mobilityDiagnostics["candidates"].push_back(std::move(trace));
                }
                if(t>=length) break;
            }
            travelled+=length;
        }
    }
    navigation.mobilityDiagnostics["candidateCount"]=candidates;
    if(!landing) {navigation.mobilityDiagnostics["result"]="no-forward-known-route-sample";return false;}
    navigation.mobilityDiagnostics["selected"]={landing->x,landing->y};
    if(exploration) navigation.mobilityDiagnostics["wallClearance"]=c.walk.navigationSearch().wallClearance(map->grid,*landing-offset+Vec{.5f,.5f});
    navigation.mobilityDiagnostics["selectedRouteProgress"]=selectedProgress;
    return submitMobilityCast(c,wanted,*landing,canTeleport,selectedProgress);
}

bool DebugAi::Impl::submitMobilityCast(Context &c,Vec wanted,Vec landing,bool teleport,float progress) {
    const auto now=Clock::now();const auto &world=c.observation.read().world;
    const auto skill=definitions->skill(teleport?"Teleport":"Charge");
    const auto hand=teleport?OnlineSkillHand::Right:OnlineSkillHand::Left;
    // Once a valid Teleport landing is chosen, preserve that movement intent
    // through skill selection, animation recovery and the native input rate
    // limit. A walking tick here would replace the cast between two landings.
    const bool ownsApproach=teleport;
    if(teleport && c.supplies && c.supplies->castInputBusy.value_or(false)) {
        navigation.mobilityHold=true;wait("mobility-animation","Waiting for the current normal cast animation to release input");return true;
    }
    if(!selectSkill(c,skill,hand)) {
        navigation.mobilityHold=ownsApproach;
        wait("mobility-select",ownsApproach?"Selecting the travel skill for the observed interaction / required landing":
            "Walking while waiting for the normal travel skill selection");return ownsApproach;
    }
    if(world.combatRequest && world.combatRequest->state==OnlineCombatRequest::State::Pending) {
        navigation.mobilityHold=ownsApproach;return ownsApproach;
    }
    // A transient input wait must not stop an otherwise useful walking leg.
    OnlineCombatCommand cast;cast.action=OnlineCombatCommand::Action::Cast;
    cast.hand=hand;cast.point=OnlinePoint{uint16_t(std::floor(landing.x)),uint16_t(std::floor(landing.y))};
    cast.stationary=true;cast.repeat=false;
    const auto submitted=submitInput(c,DebugActionOwner::Mobility,"mobility-cast:"+std::to_string(skill),DebugSkill,
        [&]{return c.combat.submit(cast,c.position);},[&]{return c.combat.reason();});
    if(submitted==DebugSubmitResult::Waiting) {navigation.mobilityHold=ownsApproach;return ownsApproach;}
    if(submitted==DebugSubmitResult::Expired) {
        note("mobility-unavailable",submissionWaits.at(DebugActionOwner::Mobility).reason);
        submissionWaits.erase(DebugActionOwner::Mobility);navigation.nextMobility=now+std::chrono::seconds(3);return false;
    }
    c.control.cancelMovement();
    ++castRequests;navigation.mobilityFrom=c.position;navigation.mobilityTarget=cast.point;
    navigation.mobilityPendingProgress=progress;
    ++navigation.mobilityTrials[{area.value_or(0),uint16_t(wanted.x),uint16_t(wanted.y),cast.point->x,cast.point->y}];
    navigation.mobilityTeleport=teleport;
    const auto player=world.units.find({0,c.observation.read().load.playerUnitId.value_or(0)});
    navigation.mobilityDiscontinuity=player==world.units.end()?0:player->second.positionDiscontinuity;
    navigation.mobilityDeadline=now+std::chrono::seconds(3);navigation.mobilityHold=true;
    note(teleport?"teleport-submitted":"charge-submitted",std::to_string(cast.point->x)+","+std::to_string(cast.point->y));return true;
}
}
