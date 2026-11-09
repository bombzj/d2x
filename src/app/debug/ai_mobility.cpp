#include "ai_impl.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace d2x {
namespace {
bool roomContains(const RemoteTown::NavigationRoom &room,Vec at) {
    return room.loaded && at.x>=room.x && at.y>=room.y && at.x<room.x+room.width && at.y<room.y+room.height;
}
}
DebugAi::Impl::TeleportRooms::TeleportRooms(const Context &c):rooms(c.town.navigationRooms()) {
    const auto feet=DebugExplorationMemory::playerCell(c.position);
    for(const auto &room:rooms) if(room.level==c.town.read().area && roomContains(room,{float(feet.first),float(feet.second)})) {
        current=&room;break;
    }
}
bool DebugAi::Impl::TeleportRooms::contains(Vec at) const {
    if(!current) return false;
    if(roomContains(*current,at)) return true;
    return std::any_of(current->near.begin(),current->near.end(),[&](size_t i) {
        return i<rooms.size() && roomContains(rooms[i],at);
    });
}
bool DebugAi::Impl::knownSkill(const Context &c,uint16_t id,bool left) const {
    return std::any_of(c.combat.skills().begin(),c.combat.skills().end(),[&](const auto &s){
        return s.id==id && !s.passive && (s.level || s.innate) && (!left || s.left);
    });
}
bool DebugAi::Impl::selectSkill(Context &c,uint16_t skill,OnlineSkillHand hand) {
    const auto &world=c.session.read().world;
    const auto selected=hand==OnlineSkillHand::Left?world.leftSkill:world.rightSkill;
    if(selected && selected->skill==skill && selected->owner==UINT32_MAX) return true;
    const auto now=Clock::now();
    if((world.combatRequest && world.combatRequest->state==OnlineCombatRequest::State::Pending) ||
        now-lastSelection<std::chrono::milliseconds(100)) return false;
    lastSelection=now;OnlineCombatCommand request;request.action=OnlineCombatCommand::Action::SelectSkill;
    request.hand=hand;request.skill=skill;
    submitCombat(c,request,DebugActionOwner::Combat,"skill-select");
    return false; // A queued selection is never a confirmed hand assignment.
}
std::optional<uint16_t> DebugAi::Impl::battleAura(const Context &c) const {
    if(policy.aura) return policy.aura;
    for(const auto name:{"Fanaticism","Concentration","Might"}) {
        const auto skill=definitions->skill(name);if(knownSkill(c,skill)) return skill;
    }
    return {};
}
bool DebugAi::Impl::maintainArmor(Context &c) {
    if (Clock::now() < nextArmorAttempt) return false;
    const auto &view = c.session.read();
    if (!view.load.playerUnitId || view.world.npcRequested || view.world.waypointSource) return false;
    std::optional<uint16_t> selected;
    for (const auto name : {"Shiver Armor", "Chilling Armor", "Frozen Armor"}) {
        const auto skill = definitions->skill(name);
        const auto row = definitions->skillRows.find(skill);
        if (row == definitions->skillRows.end()) continue;
        const auto stateName = definitions->skills.value(row->second, "aurastate");
        for (size_t state = 0; state < definitions->states.rows().size(); ++state)
            if (definitions->states.value(state, "state") == stateName) {
                const auto id = definitions->states.number(state, "Id");
                if (id && *id >= 0 && *id <= UINT8_MAX && recovering(c, uint8_t(*id))) return false;
            }
        if (!selected && knownSkill(c, skill) && !stateName.empty() && c.supplies && c.supplies->skillUsable &&
            c.supplies->skillUsable(skill).value_or(false)) selected = skill;
    }
    if (!selected) return false;
    if (!selectSkill(c, *selected, OnlineSkillHand::Right)) {
        wait("armor-select", "Selecting the available defensive armor skill");
        return true;
    }
    releaseCombat(c); cancelWalk(c);
    OnlineCombatCommand cast; cast.action = OnlineCombatCommand::Action::Cast;
    cast.hand = OnlineSkillHand::Right; cast.stationary = true;
    cast.point = OnlinePoint{uint16_t(c.position.x), uint16_t(c.position.y)};
    if (submitCombat(c, cast, DebugActionOwner::Combat, "armor-cast", c.position)) {
        nextArmorAttempt = Clock::now() + std::chrono::seconds(3);
        ++castRequests;
        note("armor-cast", "Waiting for the normal server state assignment");
    }
    return true;
}
bool DebugAi::Impl::teleportAvailable(const Context &c,bool resourceReady) const {
    if(townExitArea || c.town.read().town || (mobilityPolicy.mode!="auto" && mobilityPolicy.mode!="teleport") ||
        !knownSkill(c,definitions->skill("Teleport"))) return false;
    if(resourceReady && c.supplies && c.supplies->skillUsable)
        if(const auto usable=c.supplies->skillUsable(definitions->skill("Teleport"));usable && !*usable) return false;
    for(size_t row=0;row<definitions->levels.rows().size();++row)
        if(definitions->levels.number(row,"Id")==area)
            return definitions->levels.number(row,"Teleport").value_or(0)!=0;
    return false;
}
bool DebugAi::Impl::observeMobility(Context &c) {
    const auto now=Clock::now();const auto &world=c.session.read().world;
    // Observe the crossing independently of the movement chosen for it.
    // A walking child must be able to unlock field casting with its receipt.
    if(townExitArea && world.playerPosition) {
        const auto at=*world.playerPosition;
        const auto &rooms=c.town.navigationRooms();
        if(std::any_of(rooms.begin(),rooms.end(),[&](const auto &room) {
            return room.level==*townExitArea && at.x>=room.x && at.x<room.x+room.width &&
                at.y>=room.y && at.y<room.y+room.height &&
                world.rooms.contains({uint8_t(room.level),uint16_t(room.x/5),uint16_t(room.y/5)});
        })) {
            townExitArea.reset();
            note("town-exit-position-observed","Server position now belongs to the field; travel casting is eligible");
        }
    }
    if(mobilityTarget) {
        // Observe the normal path's relocation/progress. Casting acceptance
        // never writes coordinates or proves arrival at the intended endpoint.
        const Vec target{float(mobilityTarget->x),float(mobilityTarget->y)};
        const auto player=world.units.find({0,c.session.read().load.playerUnitId.value_or(0)});
        const bool progress=mobilityTeleport?player!=world.units.end() && player->second.positionDiscontinuity!=mobilityDiscontinuity:
            (c.position-target).length()<3 || (c.position-mobilityFrom).length()>3;
        if(progress) {
            const auto at=mobilityTeleport && world.playerPosition?
                Vec{float(world.playerPosition->x),float(world.playerPosition->y)}:c.position;
            const float miss=(at-target).length();
            const bool arrived=(!mobilityTeleport || world.playerPosition.has_value()) && miss<=4 &&
                miss<(mobilityFrom-target).length();
            if(arrived) {
                if(mobilityTeleport && c.town.read().origin) {
                    const auto origin=*c.town.read().origin;
                    const Vec offset{float(origin.x)-.5f,float(origin.y)-.5f};
                    c.walk.navigationSearch().confirmTeleportLanding(target-offset,at-offset);
                }
                // A near landing can be short of its requested route sample.
                mobilityRouteProgress=std::max(mobilityRouteProgress,mobilityPendingProgress-miss);
                // A confirmed arrival is not a failed landing attempt. A
                // later legitimate return to this point keeps the same goal.
                if(mobilityGoal) {
                    mobilityTrials.erase({mobilityArea,mobilityGoal->x,mobilityGoal->y,mobilityTarget->x,mobilityTarget->y});
                }
            }
            note(arrived?"mobility-progress-observed":"mobility-relocated-off-target");mobilityTarget.reset();nextMobility=now;
            return false;
        }
        if(now>=mobilityDeadline) {
            note("mobility-no-progress","No confirmed displacement; reconsider the next movement input");
            mobilityTarget.reset();nextMobility=now+std::chrono::seconds(3);return false;
        }
        mobilityHold=true;wait("mobility-wait","Waiting for normal skill movement/position feedback");return true;
    }
    return false;
}
std::vector<Vec> DebugAi::Impl::travelLandingThreats(const Context &c) const {
    std::vector<Vec> result;
    if(!policy.enabled || policy.range<=3 || c.town.read().town) return result;
    const auto attack=excursionSkill.value_or(policy.skill.value_or(definitions->skill("Attack")));
    if(attack==definitions->skill("Attack")) return result;
    const auto row=definitions->skillRows.find(attack);
    if(row!=definitions->skillRows.end() && definitions->skills.value(row->second,"range")=="h2h") return result;
    for(const auto &[key,enemy]:c.session.read().world.units)
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
std::optional<DebugAi::Impl::TeleportApproach> DebugAi::Impl::teleportLanding(Context &c,Vec wanted,bool exploration) const {
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    if(!map || !origin) return {};
    const TeleportRooms neighborhood(c);
    if(!neighborhood.current) return {};
    const Vec offset{float(origin->x),float(origin->y)};
    const auto live=c.town.navigationGrid(c.session.read());
    const auto threats=travelLandingThreats(c);
    const float range=float(mobilityPolicy.distance);
    const auto *knowledge=c.walk.navigationSearch().explorationMemory.area(*area);
    const auto permitted=[&](Vec at) {
        if(!knowledge || !knowledge->stand(int(at.x),int(at.y))) return false;
        if(at.x<0 || at.y<0 || at.x>65535 || at.y>65535 || !neighborhood.contains(at) ||
            (at-c.position).length()>range || crowdedTravelLanding(threats,at)) return false;
        const auto trial=std::tuple{*area,uint16_t(wanted.x),uint16_t(wanted.y),uint16_t(at.x),uint16_t(at.y)};
        return (!mobilityTrials.contains(trial) || mobilityTrials.at(trial)<2) &&
            live.walkable(at-offset+Vec{.5f,.5f},playerMovement) &&
            c.combat.teleportPathAllowed(*area,live,c.position-offset,at-offset);
    };
    if(permitted(wanted)) return TeleportApproach{wanted,true};
    auto &search=c.walk.navigationSearch();
    const auto *memory=search.explorationMemory.area(*area);
    if(!memory) return {};
    bool restricted=false;
    for(size_t row=0;row<definitions->levels.rows().size();++row)
        if(definitions->levels.number(row,"Id")==area) restricted=definitions->levels.number(row,"Teleport").value_or(0)==2;
    const auto route=search.observedRoute(*area,c.position,mobilityPolicy.distance,restricted,wanted,1);
    for(auto it=route.points.rbegin();it!=route.points.rend();++it) {
        const Vec anchor{float(it->x),float(it->y)};
        if((anchor-c.position).length()<2 || (anchor-c.position).length()>range) continue;
        std::optional<Vec> landing;float best=std::numeric_limits<float>::max();
        for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
            const Vec at=anchor+Vec{float(dx),float(dy)};
            if(!memory->stand(int(at.x),int(at.y)) || !permitted(at)) continue;
            const float cost=float(dx*dx+dy*dy);
            if(cost<best) {best=cost;landing=at;}
        }
        if(landing) return TeleportApproach{*landing,true};
    }
    (void)exploration;
    return {};
}

std::optional<float> DebugAi::Impl::kiteWalkSeconds(const Context &c,Vec destination) const {
    if(!c.supplies || !c.supplies->movementSecondsPerUnit || !area ||
        !std::isfinite(*c.supplies->movementSecondsPerUnit) || *c.supplies->movementSecondsPerUnit<=0) return {};
    const auto origin=c.town.read().origin;
    const auto *knowledge=c.walk.navigationSearch().explorationMemory.area(*area);
    if(!origin || !knowledge) return {};
    const Vec offset{float(origin->x),float(origin->y)};
    const auto grid=c.town.navigationGrid(c.session.read());
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
    if(mobilityTarget) return observeMobility(c);
    if(c.walk.openingRouteDoor()) return false;
    const auto now=Clock::now();const auto &world=c.session.read().world;
    if(townExitArea) {
        mobilityHold=now<townExitObserveUntil;
        wait("mobility-town-exit",mobilityHold?"Waiting briefly for the host's field position while the crossing completes":
            "Walking into the field until the host position confirms the crossing");
        return mobilityHold;
    }
    const bool combatMovement=!combatOwned && (retreatPoint || combatRun.reposition || combatRun.kitePoint || hazardDodge || phase=="combat-approach");
    const bool combatActive=combatTarget && combatRun.suspendedAt==Clock::time_point{} && !combatMovement;
    if(combatActive || !c.walk.active() || now<nextMobility || world.npcRequested || world.waypointSource ||
        portalUsed || travelPhase==TravelPhase::WaypointWait || travelPhase==TravelPhase::CrossWait) {
        mobilityDiagnostics["skip"]={{"combat",combatActive},{"retainedCombat",bool(combatTarget)},{"childActive",c.walk.active()},
            {"cooldown",now<nextMobility},{"npc",bool(world.npcRequested)},{"waypoint",bool(world.waypointSource)},
            {"portalUsed",portalUsed},{"travelPhase",unsigned(travelPhase)}};
        return false;
    }
    const auto child=c.walk.snapshot();const auto &target=child.at("target");
    std::optional<Vec> destination;
    if(target.is_object()) destination=Vec{target.at("x").get<float>(),target.at("y").get<float>()};
    // A newly created unit walk has not ticked yet. Resolve its already
    // assigned target now so Teleport can own the first approach frame.
    const auto &childUnit=child.at("unit");
    if(!destination && childUnit.is_object()) {
        const auto unit=world.units.find({childUnit.at("type").get<uint8_t>(),childUnit.at("id").get<uint32_t>()});
        if(unit!=world.units.end() && unit->second.position)
            destination=Vec{float(unit->second.position->x),float(unit->second.position->y)};
    }
    if(!destination) return false;
    Vec wanted=*destination;
    Vec delta=wanted-c.position;float distance=delta.length();
    const bool assignedApproach=childUnit.is_object() && world.units.contains({
        childUnit.at("type").get<uint8_t>(),childUnit.at("id").get<uint32_t>()});
    if(assignedApproach && walkIntent.is_object() && walkIntent.value("interact",false) &&
        c.town.interactionReady(c.session.read(),{childUnit.at("type").get<uint8_t>(),childUnit.at("id").get<uint32_t>()},c.position))
        return false; // A normal click is already possible; do not spend a cast on the object's center.
    const auto *map=c.town.map();const auto origin=c.town.read().origin;if(!map || !origin || distance<1) return false;
    const Vec offset{float(origin->x),float(origin->y)};
    // Walk a few clear cells to the same objective. A nearby point across a
    // wall can still warrant a cast, as can a deliberate defensive escape.
    if(distance<8 && !combatMovement &&
        map->grid.nativeMovementSegment(c.position-offset+Vec{.5f,.5f},wanted-offset+Vec{.5f,.5f},playerMovement))
        return false;
    const auto teleport=definitions->skill("Teleport"),charge=definitions->skill("Charge"),vigor=definitions->skill("Vigor");
    const bool canTeleport=teleportAvailable(c);
    if(canTeleport && combatRun.kitePoint && walkIntent.is_object() &&
        walkIntent.value("purpose",std::string{})=="combat-kite") {
        const auto walking=kiteWalkSeconds(c,wanted);
        mobilityDiagnostics["kiteCost"]={{"walkSeconds",walking?Json(*walking):Json(nullptr)},
            {"teleportSeconds",teleportTiming.seconds?Json(*teleportTiming.seconds):Json(nullptr)}};
        if(walking && teleportTiming.seconds && *walking<=*teleportTiming.seconds) {
            mobilityDiagnostics["skip"]="safe direct kite walk costs no more than the observed Teleport cast";
            return false;
        }
    }
    if(distance<8 && !canTeleport) {mobilityDiagnostics["skip"]="short distance without Teleport";return false;}
    const bool canCharge=!canTeleport && !c.town.read().town && knownSkill(c,charge,true) &&
        (mobilityPolicy.mode=="auto" || mobilityPolicy.mode=="charge");
    const bool useVigor=mobilityPolicy.vigor && !canTeleport && knownSkill(c,vigor);
    if(useVigor && !selectSkill(c,vigor,OnlineSkillHand::Right)) {
        wait("mobility-select","Walking while waiting for normal Vigor selection");return false;
    }
    if(!canTeleport && !canCharge) {mobilityDiagnostics["skip"]="no usable travel skill";return false;}
    const auto threats=travelLandingThreats(c);
    const auto crowdedLanding=[&](Vec at){return crowdedTravelLanding(threats,at);};
    const TeleportRooms neighborhood(c);
    if(canTeleport && assignedApproach && walkIntent.value("interact",false)) {
        const OnlineUnitKey key{childUnit.at("type").get<uint8_t>(),childUnit.at("id").get<uint32_t>()};
        const auto entry=std::find_if(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),
            [&](const auto &value){return value.unit==key;});
        if(entry!=c.town.read().mapTargets.end()) {
            // An authored portal/door center can be solid or beyond a chasm.
            // Choose a real standing cell in its normal interaction perimeter,
            // rather than requiring a walking route to that center first.
            const auto grid=c.town.navigationGrid(c.session.read());
            const int width=std::clamp(entry->collisionWidth/2+4,4,16),height=std::clamp(entry->collisionHeight/2+4,4,16);
            std::optional<Vec> perimeter;float best=std::numeric_limits<float>::max();
            for(int y=std::max(0,int(entry->position.y)-height);y<=std::min(65535,int(entry->position.y)+height);++y)
                for(int x=std::max(0,int(entry->position.x)-width);x<=std::min(65535,int(entry->position.x)+width);++x) {
                    const Vec at{float(x),float(y)},local=at-offset+Vec{.5f,.5f};
                    const float span=(at-c.position).length();
                    if(span<1 || span>mobilityPolicy.distance || !neighborhood.contains(at) || crowdedLanding(at) ||
                        !grid.walkable(local,playerMovement) || !c.town.interactionReady(c.session.read(),key,at)) continue;
                    if(span<best) {best=span;perimeter=at;}
                }
            if(perimeter) {wanted=*perimeter;distance=(wanted-c.position).length();}
        }
    }
    const Vec localGoal=wanted-offset+Vec{.5f,.5f};
    const OnlinePoint goal{uint16_t(wanted.x),uint16_t(wanted.y)};
    if(mobilityGoal!=goal || mobilityArea!=area.value_or(0)) {
        mobilityGoal=goal;mobilityArea=area.value_or(0);mobilityRoute.clear();
        mobilityRouteProgress=mobilityPendingProgress=mobilityRouteLength=0;
    }
    const bool exploration=walkIntent.is_object() && walkIntent.value("exploration",false);
    const bool occupiedGoal=crowdedLanding(wanted);
    std::optional<Vec> stagedLanding;
    bool completeTeleportRoute=false;
    if(canTeleport) {
        if(const auto approach=teleportLanding(c,wanted,exploration)) {
            stagedLanding=approach->landing;
            completeTeleportRoute=approach->completeRoute;
        }
    }
    const bool directLanding=stagedLanding.has_value();
    if(canTeleport && !directLanding) {
        mobilityDiagnostics={{"decision",decisions},{"area",area.value_or(0)},{"movement","teleport"},
            {"from",{c.position.x,c.position.y}},{"target",{wanted.x,wanted.y}},{"result","no-known-teleport-landing"}};
        return false;
    }
    if(!canTeleport && mobilityRoute.empty()) {
        ++pathSearches;
        const auto route=c.walk.navigationSearch().travelRoute(map->grid,c.position-offset+Vec{.5f,.5f},localGoal);
        if(!route.points.empty() && (route.points.back()-localGoal).length()<=1.f) {
            mobilityRoute.push_back(route.origin+offset-Vec{.5f,.5f});
            for(const auto node:route.points) {
                const auto at=node+offset-Vec{.5f,.5f};
                mobilityRouteLength+=(at-mobilityRoute.back()).length();mobilityRoute.push_back(at);
            }
        }
    }
    mobilityDiagnostics={{"decision",decisions},{"area",area.value_or(0)},{"movement",canTeleport?"teleport":"charge"},
        {"from",{c.position.x,c.position.y}},{"target",{wanted.x,wanted.y}},
        {"goalOccupied",occupiedGoal},{"stagedLanding",stagedLanding?Json{stagedLanding->x,stagedLanding->y}:Json(nullptr)},
        {"completeTeleportRoute",completeTeleportRoute},
        {"routeLength",mobilityRouteLength},{"confirmedRouteProgress",mobilityRouteProgress},
        {"remainingRoute",std::max(0.f,mobilityRouteLength-mobilityRouteProgress)},
        {"routePoints",mobilityRoute.size()},{"candidates",Json::array()},{"selected",nullptr}};
    mobilityDiagnostics["nativeRoom"]=neighborhood.current?Json{{neighborhood.current->level,neighborhood.current->x,neighborhood.current->y}}:Json(nullptr);
    if(!directLanding && mobilityRoute.empty()) {
        mobilityDiagnostics["result"]="no-complete-known-route-to-child-target";return false;
    }
    std::optional<Vec> landing;float selectedProgress=mobilityRouteProgress,best=-1e9f;
    unsigned candidates=0;
    if(directLanding) {
        const Vec at=*stagedLanding,local=at-offset+Vec{.5f,.5f};
        const auto trial=std::tuple{area.value_or(0),goal.x,goal.y,uint16_t(at.x),uint16_t(at.y)};
        if(crowdedLanding(at) || !map->grid.nativeMovementSegment(local,local,playerMovement) ||
            (mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2)) {
            mobilityDiagnostics["result"]="direct landing blocked or attempts exhausted";return false;
        }
        landing=at;
    } else {
        float travelled=0;
        for(size_t i=1;i<mobilityRoute.size();++i) {
            const Vec from=mobilityRoute[i-1],leg=mobilityRoute[i]-from;const float length=leg.length();
            if(length>0) for(float t=std::min(4.f,length);;t=std::min(t+4.f,length)) {
                const float progress=travelled+t;
                const Vec sample=from+leg*(t/length);
                Vec point{std::floor(sample.x+.5f),std::floor(sample.y+.5f)};
                Vec local=point-offset+Vec{.5f,.5f};
                const float span=(point-c.position).length();
                if(progress>mobilityRouteProgress+1 && span>=6 && span<=mobilityPolicy.distance &&
                    point.x>=0 && point.y>=0 && point.x<=65535 && point.y<=65535) {
                    const auto trial=std::tuple{area.value_or(0),goal.x,goal.y,uint16_t(point.x),uint16_t(point.y)};
                    Json trace{{"point",{point.x,point.y}},{"routeProgress",progress}};
                    ++candidates;
                    if(crowdedLanding(point)) trace["result"]="enemy-too-close-to-ranged-landing";
                    else if(!map->grid.nativeMovementSegment(local,local,playerMovement)) trace["result"]="unavailable-landing";
                    else if(mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2) trace["result"]="landing-attempt-limit";
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
                    if(mobilityDiagnostics["candidates"].size()<15)
                        mobilityDiagnostics["candidates"].push_back(std::move(trace));
                }
                if(t>=length) break;
            }
            travelled+=length;
        }
    }
    mobilityDiagnostics["candidateCount"]=candidates;
    if(!landing) {mobilityDiagnostics["result"]="no-forward-known-route-sample";return false;}
    mobilityDiagnostics["selected"]={landing->x,landing->y};
    if(exploration) mobilityDiagnostics["wallClearance"]=c.walk.navigationSearch().wallClearance(map->grid,*landing-offset+Vec{.5f,.5f});
    mobilityDiagnostics["selectedRouteProgress"]=selectedProgress;
    const auto skill=canTeleport?teleport:charge;
    const auto hand=canTeleport?OnlineSkillHand::Right:OnlineSkillHand::Left;
    // Once a valid Teleport landing is chosen, preserve that movement intent
    // through skill selection, animation recovery and the native input rate
    // limit. A walking tick here would replace the cast between two landings.
    const bool ownsApproach=canTeleport;
    if(canTeleport && c.supplies && c.supplies->castInputBusy.value_or(false)) {
        mobilityHold=true;wait("mobility-animation","Waiting for the current normal cast animation to release input");return true;
    }
    if(!selectSkill(c,skill,hand)) {
        mobilityHold=ownsApproach;
        wait("mobility-select",ownsApproach?"Selecting the travel skill for the observed interaction / required landing":
            "Walking while waiting for the normal travel skill selection");return ownsApproach;
    }
    if(world.combatRequest && world.combatRequest->state==OnlineCombatRequest::State::Pending) {
        mobilityHold=ownsApproach;return ownsApproach;
    }
    // A transient input wait must not stop an otherwise useful walking leg.
    OnlineCombatCommand cast;cast.action=OnlineCombatCommand::Action::Cast;
    cast.hand=hand;cast.point=OnlinePoint{uint16_t(std::floor(landing->x)),uint16_t(std::floor(landing->y))};
    cast.stationary=true;cast.repeat=false;
    const auto submitted=submitInput(c,DebugActionOwner::Mobility,"mobility-cast:"+std::to_string(skill),DebugSkill,
        [&]{return c.combat.submit(cast,c.position);},[&]{return c.combat.reason();});
    if(submitted==DebugSubmitResult::Waiting) {mobilityHold=ownsApproach;return ownsApproach;}
    if(submitted==DebugSubmitResult::Expired) {
        note("mobility-unavailable",submissionWaits.at(DebugActionOwner::Mobility).reason);
        submissionWaits.erase(DebugActionOwner::Mobility);nextMobility=now+std::chrono::seconds(3);return false;
    }
    c.control.cancelMovement();
    ++castRequests;mobilityFrom=c.position;mobilityTarget=cast.point;
    mobilityPendingProgress=selectedProgress;
    ++mobilityTrials[{area.value_or(0),uint16_t(wanted.x),uint16_t(wanted.y),cast.point->x,cast.point->y}];
    mobilityTeleport=canTeleport;
    const auto player=world.units.find({0,c.session.read().load.playerUnitId.value_or(0)});
    mobilityDiscontinuity=player==world.units.end()?0:player->second.positionDiscontinuity;
    mobilityDeadline=now+std::chrono::seconds(3);mobilityHold=true;
    note(canTeleport?"teleport-submitted":"charge-submitted",std::to_string(cast.point->x)+","+std::to_string(cast.point->y));return true;
}
}
