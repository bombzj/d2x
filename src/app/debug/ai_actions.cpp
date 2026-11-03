#include "ai_impl.hpp"
#include "contracts/online_npc_speech.hpp"
#include "gameplay/quest/catalog.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
Vec position(OnlinePoint p) {return {float(p.x),float(p.y)};}
}
std::optional<OnlineUnit> DebugAi::Impl::find(const Context &c,const Selector &selector,bool corpses) const {
    std::optional<OnlineUnit> best;float nearest=std::numeric_limits<float>::max();
    for(const auto &[key,unit]:c.session.read().world.units) {
        if(key.type!=selector.type || !unit.position || !unit.classId) continue;
        const auto identity=key.type==1?c.combat.monsterClass(unit):unit.classId;
        if(identity!=selector.identity || (key.type==1 && !corpses && onlineMonsterCorpse(unit))) continue;
        if(selector.superUnique && c.combat.superUnique(unit)!=selector.superUnique) continue;
        if(selector.eliteOnly && !c.combat.eliteMonster(unit)) continue;
        float d=(position(*unit.position)-c.position).length();
        // Prefer a live target, but an actually assigned corpse is evidence of
        // the "target dead" postcondition, never evidence of personal kill credit.
        if(key.type==1 && onlineMonsterCorpse(unit)) d+=10000;
        if(d<nearest) {nearest=d;best=unit;}
    }
    return best;
}
bool DebugAi::Impl::owned(const Context &c,std::string_view code) const {
    for(const auto &[id,item]:c.session.read().world.items) {
        (void)id;
        if(item.code==code && onlineItemOwnedBy(item,c.session.read().load.playerUnitId) &&
            (item.mode==0 || item.mode==1 || item.mode==2 || item.mode==4)) return true;
    }
    return false;
}
void DebugAi::Impl::acquire(Context &c,const Goal &goal) {
    if(acquireItem(c,goal)) advance(c,"Owned item observed: "+goal.item);
}
bool DebugAi::Impl::settleItemInput(Context &c) {
    // A released missile/teleport is not the end of the player's skill mode.
    // This is debug input pacing, not a change to ItemMode's busy rules. Keep
    // observing the normal animation and wait for a quiet interval before sending one
    // inventory request; never use a rejected/unknown request as a retry loop.
    const auto now=Clock::now();const auto cast=std::max(lastAttack,mobilityDeadline-std::chrono::seconds(3));
    const auto &view=c.session.read();
    const auto player=view.load.playerUnitId?view.world.units.find({0,*view.load.playerUnitId}):view.world.units.end();
    const auto milliseconds=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    // Hit recovery may start after our last cast. The normal action receipt
    // must also settle before inventory input; this is conservative pacing,
    // not a client-side claim that the server has ended an animation.
    const bool quiet=player==view.world.units.end() || !player->second.actionReceivedMilliseconds ||
        (milliseconds>=player->second.actionReceivedMilliseconds && milliseconds-player->second.actionReceivedMilliseconds>=600);
    const bool castFinished=c.supplies && c.supplies->castInputBusy?
        !*c.supplies->castInputBusy:now-cast>=std::chrono::milliseconds(1500);
    if(quiet && castFinished && !mobilityTarget &&
        (!view.world.combatRequest || view.world.combatRequest->state!=OnlineCombatRequest::State::Pending)) return true;
    cancelWalk(c);wait("item-input-settle","Waiting for the last normal skill input to finish before inventory input");return false;
}
bool DebugAi::Impl::approachPickup(Context &c,const OnlineItem &item) {
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    if(!map || !origin) {wait("pickup-map","Waiting for the current client collision");return false;}
    const Vec offset{float(origin->x),float(origin->y)},at{float(item.groundX),float(item.groundY)};
    const auto key=[&](OnlinePoint p) {return std::tuple{item.id,item.groundX,item.groundY,p.x,p.y};};
    const bool own=walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="pickup-approach" &&
        walkIntent.value("itemId",0u)==item.id && walkIntent.value("groundX",0)==item.groundX &&
        walkIntent.value("groundY",0)==item.groundY;
    if(nativePickupReach(map->grid,c.position-offset,at-offset,{})) {
        if(own) cancelWalk(c);
        return settleItemInput(c);
    }
    if(own) {
        if(c.walk.active()) {wait("pickup-approach","Continuing the selected pickup position");return false;}
        const auto state=walkState(c);
        if(state=="completed" || state=="failed") {
            pickupApproaches.insert(key({walkIntent.at("x").get<uint16_t>(),walkIntent.at("y").get<uint16_t>()}));
            note("pickup-approach-unavailable",c.walk.snapshot().value("reason",std::string{}));cancelWalk(c);
        }
    }
    const auto attempts=std::count_if(pickupApproaches.begin(),pickupApproaches.end(),[&](const auto &p) {
        return std::get<0>(p)==item.id && std::get<1>(p)==item.groundX && std::get<2>(p)==item.groundY;
    });
    if(attempts>=8) throw std::runtime_error("No usable pickup position after eight different approaches; item ownership was not assumed");
    if(openDoor(c)) return false;
    const bool teleport=teleportAvailable(c);
    std::vector<OnlinePoint> candidates;
    // This is an AI search budget around the assigned item, not an expanded
    // pickup radius. Every candidate must pass the ordinary reach/ray test.
    for(int y=std::max(0,int(item.groundY)-6);y<=std::min(65535,int(item.groundY)+6);++y)
        for(int x=std::max(0,int(item.groundX)-6);x<=std::min(65535,int(item.groundX)+6);++x) {
            const OnlinePoint p{uint16_t(x),uint16_t(y)};const Vec world{float(x),float(y)},local=world-offset;
            const bool tried=std::any_of(pickupApproaches.begin(),pickupApproaches.end(),[&](const auto &old) {
                return std::get<0>(old)==item.id && std::get<1>(old)==item.groundX && std::get<2>(old)==item.groundY &&
                    std::abs(int(std::get<3>(old))-x)<=2 && std::abs(int(std::get<4>(old))-y)<=2;
            });
            if(tried || !map->grid.nativeMovementSegment(local+Vec{.5f,.5f},local+Vec{.5f,.5f},playerMovement) ||
                !nativePickupReach(map->grid,local,at-offset,{})) continue;
            candidates.push_back(p);
        }
    const auto score=[&](OnlinePoint p) {
        const Vec world{float(p.x),float(p.y)};
        return teleport && (world-c.position).length()<=mobilityPolicy.distance?
            (world-at).length():(world-c.position).length()+.25f*(world-at).length();
    };
    std::stable_sort(candidates.begin(),candidates.end(),[&](auto a,auto b){return score(a)<score(b);});
    for(const auto p:candidates) {
        const Vec world{float(p.x),float(p.y)},local=world-offset+Vec{.5f,.5f};
        const auto trial=std::tuple{area.value_or(0),p.x,p.y,p.x,p.y};
        if(teleport) {
            if(mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2) continue;
            if(!teleportLanding(c,world,false)) continue;
        } else {
            ++pathSearches;
            if(c.walk.navigationSearch().path(map->grid,c.position-offset+Vec{.5f,.5f},local,false,playerMovement,true).empty()) continue;
        }
        wait("pickup-approach",teleport?"Approaching the item's pickup position by Teleport":"Walking to a valid pickup position");
        walk(c,{{"goal","point"},{"x",p.x},{"y",p.y},{"interact",false},{"purpose","pickup-approach"},
            {"itemId",item.id},{"groundX",item.groundX},{"groundY",item.groundY}});
        // Quest reward topics can still own the parent's pending snapshot in
        // this decision. Prepare normal teleport input before the child tries
        // an impossible walking path; no coordinates are changed here.
        if(teleport) mobility(c);
        return false;
    }
    if((at-c.position).length()>12) {
        if(!approachPoint(c,{item.groundX,item.groundY},"assigned ground item",8))
            explore(c,"reachable approach to the assigned ground item");
        return false;
    }
    wait("pickup-approach-wait","No current legal pickup approach; waiting for normal collision or skill availability");return false;
}
const OnlineItem *DebugAi::Impl::observedDrop(const Context &c,std::string_view code,bool landedOnly) const {
    const OnlineItem *best=nullptr;float nearest=std::numeric_limits<float>::max();
    for(const auto &[id,item]:c.session.read().world.items) {
        if(item.code!=code || !item.positionKnown || (item.mode!=3 && (landedOnly || item.mode!=5))) continue;
        const float d=(Vec{float(item.groundX),float(item.groundY)}-c.position).length();
        if(!best || (item.mode==3 && best->mode!=3) || (item.mode==best->mode && d<nearest)) {
            best=&item;nearest=d;
        }
    }
    return best;
}
void DebugAi::Impl::observeDrops(const Context &c,const Goal &goal) {
    if(!area) return;
    const auto &world=c.session.read().world;
    std::erase_if(rememberedDrops,[&](const auto &entry) {
        const auto item=world.items.find(entry.second.id);
        return owned(c,entry.first) || (item!=world.items.end() && (item->second.code!=entry.first ||
            (item->second.mode!=3 && item->second.mode!=5)));
    });
    const auto remember=[&](const std::string &code,std::optional<uint16_t> source) {
        if(code.empty() || owned(c,code)) return;
        const auto *item=observedDrop(c,code);
        if(!item) return;
        const OnlinePoint at{item->groundX,item->groundY};
        const auto &rooms=c.town.navigationRooms();
        const auto room=std::find_if(rooms.begin(),rooms.end(),[&](const auto &r) {
            return r.loaded && at.x>=r.x && at.x<r.x+r.width && at.y>=r.y && at.y<r.y+r.height;
        });
        const auto level=room==rooms.end()?*area:uint16_t(room->level);
        if(source && level!=source) return;
        auto [entry,added]=rememberedDrops.try_emplace(code);
        if(added || entry->second.id!=item->id)
            note("quest-drop-remembered",code+" #"+std::to_string(item->id)+" in area "+std::to_string(level));
        entry->second={item->id,level,at,{}};
    };
    if(goal.kind==Kind::Acquire) remember(goal.item,goal.area);
    if(goal.kind!=Kind::Quest) return;
    if(goal.routeDrop) remember(goal.steps.at(*goal.routeDrop).item,{});
    if(questRun.step<goal.steps.size() && goal.routeDrop!=questRun.step) {
        const auto &step=goal.steps[questRun.step];
        if(step.action==QuestAction::Acquire || step.action==QuestAction::SearchDrop) remember(step.item,step.area);
    }
}
bool DebugAi::Impl::acquireItem(Context &c,const Goal &goal) {
    if(owned(c,goal.item)) return true;
    const auto now=Clock::now();const auto &world=c.session.read().world;
    if(pickup) {
        wait("pickup-wait","Waiting for actual item ownership");return false;
    }
    if(const auto *observed=observedDrop(c,goal.item)) {
        const auto ground=*observed;
        if(!approachPickup(c,ground)) return false;
        cancelWalk(c);
        if(ground.mode==5) {wait("pickup-landing","Drop observed; waiting for the assigned item to land");return false;}
        OnlineItemCommand take;take.action=OnlineItemAction::Pickup;take.item=ground.id;take.itemRevision=ground.revision;
        if(submitItem(c,take,"item-pickup")) {
            pickup=PickupReceipt{goal.item,ground.id,c.session.read().world.itemRequest->sequence,now+std::chrono::seconds(12)};note("pickup-submitted",goal.item);
        } else wait("pickup-blocked",c.inventory.reason());
        return false;
    }
    if(const auto found=rememberedDrops.find(goal.item);found!=rememberedDrops.end()) {
        auto &drop=found->second;
        if(area!=drop.area) {drop.inspectSince={};travel(c,drop.area);return false;}
        if((position(drop.position)-c.position).length()>6) {
            drop.inspectSince={};
            if(!approachPoint(c,drop.position,"last observed quest drop",5))
                explore(c,"reachable approach to the last observed quest drop");
            return false;
        }
        const auto &rooms=c.town.navigationRooms();
        const bool subscribed=std::any_of(rooms.begin(),rooms.end(),[&](const auto &r) {
            return r.level==drop.area && r.loaded && drop.position.x>=r.x && drop.position.x<r.x+r.width &&
                drop.position.y>=r.y && drop.position.y<r.y+r.height &&
                world.rooms.contains({uint8_t(r.level),uint16_t(r.x/5),uint16_t(r.y/5)});
        });
        if(!subscribed) {
            drop.inspectSince={};
            if(!approachPoint(c,drop.position,"resubscribe to the recorded drop room",1))
                wait("pickup-room","Waiting for the host to assign the recorded drop room");
            return false;
        }
        if(drop.inspectSince==Clock::time_point{}) drop.inspectSince=now;
        const auto grace=std::chrono::milliseconds(std::clamp<uint64_t>(2ULL*c.session.read().latencyMilliseconds.value_or(0)+500,1000,3000));
        cancelWalk(c);
        if(now-drop.inspectSince<grace) {wait("pickup-reobserve","Waiting at the recorded drop for a fresh ground assignment");return false;}
        note("quest-drop-no-longer-present",goal.item+" #"+std::to_string(drop.id));rememberedDrops.erase(found);
        return false;
    }
    if(objectIssued) {
        wait("item-source-wait","Source interaction submitted; waiting for the actual ground item");
        if(now>=actionDeadline) throw std::runtime_error("Source did not publish the requested item; no drop or quest eligibility is fabricated");
        return false;
    }
    if(goal.sourceObject) {
        const auto source=find(c,Selector{2,*goal.sourceObject,"item source"},true);
        if(source) {
            target=source->key;targetAssignment=source->assignmentRevision;
            wait("item-source","Operating the actual assigned item source");
            if(walk(c,{{"goal","unit"},{"unitType",source->key.type},{"unitId",source->key.id},{"interact",true}})) {
                objectIssued=true;setActionDeadline(std::chrono::seconds(12));++interactions;cancelWalk(c);note("item-source-submitted",goal.item);
            }
            return false;
        }
    }
    if(goal.sourceObject && approachKnown(c,Selector{2,*goal.sourceObject,"item source"})) return false;
    explore(c,"item "+goal.item,false,ExplorationFocus::Units);return false;
}
void DebugAi::Impl::returnTown(Context &c) {
    if(reachTown(c)) advance(c,"Native town scene observed");
}
bool DebugAi::Impl::reachTown(Context &c) {
    if(c.town.read().town) return true;
    if(!portalIssued && waypointBeforeDeparture(c)) return false;
    const auto &view=c.session.read();const auto now=Clock::now();
    if(!view.load.townArea) throw std::runtime_error("Current native act has no assigned town");
    // An unlocked waypoint already within interaction approach is cheaper
    // than casting a portal beside it. Continue a chosen return route across
    // frames, but never replace a portal request that is already submitted.
    const bool nearbyWaypoint=area && knownWaypointAreas.contains(*area) && area!=unavailableWaypointArea &&
        std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry) {
            if(entry.interaction!=OnlineMapInteraction::Waypoint || c.town.assignedUnitLevel(entry.unit)!=area || (position(entry.position)-c.position).length()>16) return false;
            const auto unit=view.world.units.find(entry.unit);
            return unit!=view.world.units.end() && !(deferredWaypoint && deferredWaypoint->unit==entry.unit &&
                deferredWaypoint->assignment==unit->second.assignmentRevision && now<deferredWaypoint->retryAt);
        });
    if(!portalIssued && knownWaypointAreas.contains(*view.load.townArea) &&
        (travelDestination==view.load.townArea || nearbyWaypoint)) {
        wait("return-waypoint","Using the nearby unlocked waypoint or continuing the selected town return route");
        return travel(c,*view.load.townArea);
    }
    if(portalIssued) {
        if(portalUsed) {
            wait("town-portal-wait","Waiting for the native town scene");
            if(recoverTravel(c,now<actionDeadline)) portalUsed=false;
            else if(now>=actionDeadline) throw std::runtime_error("Own portal was used but town was not observed");
            return false;
        }
        std::optional<OnlineMapTarget> portal;
        for(const auto &entry:c.town.read().mapTargets) if(entry.interaction==OnlineMapInteraction::Portal && entry.destination==view.load.townArea) {
            const auto u=view.world.units.find(entry.unit);
            if(u!=view.world.units.end() && u->second.portalOwner==view.load.playerUnitId && !oldPortals.contains(entry.unit.id)) {portal=entry;break;}
        }
        if(portal) {
            wait("town-portal","Using the newly observed own town portal");
            if(walk(c,{{"goal","unit"},{"unitType",portal->unit.type},{"unitId",portal->unit.id},{"interact",true}})) {
                // Request accepted is not a successful return. Keep observing the scene.
                rememberTravel(c,portal->unit);
                portalUsed=true;setActionDeadline(std::chrono::seconds(15));
                wait("town-portal-wait","Waiting for the native town scene");
            }
            return false;
        }
        if(now>=actionDeadline && !view.world.townPortalPending) {
            portalIssued=false;cancelWalk(c);
            note("town-portal-unconfirmed","No usable own portal observed; continuing by ordinary travel without recasting");
            return false;
        }
        wait("town-portal-wait","Waiting for a newly assigned own portal");return false;
    }
    if(!portalAttempted) for(const auto &entry:c.town.read().mapTargets)
        if(entry.interaction==OnlineMapInteraction::Portal && entry.destination==view.load.townArea) {
            const auto unit=view.world.units.find(entry.unit);
            if(unit!=view.world.units.end() && unit->second.portalOwner==view.load.playerUnitId) {
                portalAttempted=portalIssued=true;oldPortals.clear();setActionDeadline(std::chrono::seconds(12));
                note("existing-own-portal-observed");return false;
            }
        }
    // A scroll/tome is a real resource. If none is available, use normal
    // walking/known unlocked waypoints rather than granting one.
    if(!portalAttempted && !c.town.read().townPortalSkills.empty()) {
        oldPortals.clear();for(const auto &[key,unit]:view.world.units)
            if(key.type==2 && unit.portalOwner==view.load.playerUnitId) oldPortals.insert(key.id);
        cancelWalk(c);
        const auto submitted=submitInput(c,DebugActionOwner::Goal,"town-portal-input",DebugMotion|DebugSkill,
            [&]{return c.control.townPortal();},[&]{return c.control.reason();},std::chrono::seconds(2));
        if(submitted==DebugSubmitResult::Accepted) {
            portalAttempted=portalIssued=true;setActionDeadline(std::chrono::seconds(12));note("town-portal-submitted");return false;
        }
        // A rejected local input is not a submitted portal. Crossing a level
        // can briefly retain the movement throttle; let it settle before
        // permanently choosing the ordinary route. Accepted casts are never replayed.
        if(submitted==DebugSubmitResult::Waiting) return false;
        portalAttempted=true;
        note("town-portal-input-unavailable",submissionWaits.at(DebugActionOwner::Goal).reason);
        submissionWaits.erase(DebugActionOwner::Goal);
    }
    wait("return-walking","No usable town portal; using normal travel back to town");
    return travel(c,*view.load.townArea);
}
void DebugAi::Impl::deliver(Context &c,const Goal &goal) {
    const auto &view=c.session.read();const auto slot=goal.questSlot;
    if(view.world.quests.playerFlags && ((*view.world.quests.playerFlags)[slot]&3) && !owned(c,goal.item)) {
        if(view.world.npcRequested) c.control.closeNpc();
        advance(c,"Item consumed and actual native reward flag observed: "+goal.quest);return;
    }
    if(!owned(c,goal.item)) throw std::runtime_error("No required item is owned and no delivery reward flag confirms completion");
    const auto npc=find(c,*goal.target);
    if(!npc) {if(!approachKnown(c,*goal.target)) explore(c,"delivery NPC "+goal.target->name,false,ExplorationFocus::Units);return;}
    const auto now=Clock::now();
    if(!view.world.npcConversation || view.world.npcConversation->type!=1 || view.world.npcConversation->source!=npc->key.id) {
        wait("delivery-approach","Opening the actual delivery NPC conversation");
        walk(c,{{"goal","unit"},{"unitType",1},{"unitId",npc->key.id},{"interact",true}});return;
    }
    wait("delivery-dialogue","Acknowledging only actually offered automatic text or the verified Malus topic");
    if(!greetingIssued && npc->classId) {greetingIssued=c.control.greetNpc(*npc->classId);if(greetingIssued)return;}
    const auto conversation=*view.world.npcConversation;
    const auto automatic = nextAutomaticNpcSpeech(conversation);
    for(const auto &message:conversation.messages) {
        if (automatic && *automatic != message.stringId) continue;
        // D2MOO A1Q3 ScrollMessage 163 deletes hdm and sets REWARDPENDING.
        // It is acknowledged only when the real NPC offered it to this client.
        if((message.menu==0 || std::find(goal.topics.begin(),goal.topics.end(),message.stringId)!=goal.topics.end()) &&
            !conversation.acknowledged.contains(message.stringId)) {
            if(c.control.acknowledgeNpc(message.stringId)) {
                ++interactions;setActionDeadline(std::chrono::seconds(12));note("npc-topic-acknowledged",std::to_string(message.stringId));
            }
            return;
        }
    }
    if(actionDeadline==Clock::time_point{}) setActionDeadline(std::chrono::seconds(12),false);
    if(now>=actionDeadline) throw std::runtime_error("NPC did not confirm item consumption and reward readiness");
}
void DebugAi::Impl::executeGoal(Context &c,const Goal &goal) {
    if(localPad && localPad->waiting) {wait("local-pad-result","Waiting for the actual same-level relocation");return;}
    if(goal.kind==Kind::Quest) {quest(c,goal);return;}
    if (goal.requiredWaypoint && goal.area && knownWaypointAreas.contains(*goal.area)) {
        advance(c, "Personal waypoint activation observed"); return;
    }
    if(goal.kind==Kind::Acquire && owned(c,goal.item)) {advance(c,"Owned item already satisfies acquire: "+goal.item);return;}
    if(goal.area && (area!=goal.area || travelDestination) && !travel(c,*goal.area)) return;
    switch(goal.kind) {
    case Kind::Travel:
        if (goal.requiredWaypoint) {
            if (!activateWaypoint(c,true)) explore(c, "required waypoint",false,ExplorationFocus::Units);
            return;
        }
        advance(c,"Native target area observed");return;
    case Kind::ReturnTown: returnTown(c);return;
    case Kind::Acquire: acquire(c,goal);return;
    case Kind::Deliver: deliver(c,goal);return;
    case Kind::Restock:
        if(reachTown(c) && restock(c)) advance(c,"Observed potion reserves satisfy restock");
        return;
    case Kind::Quest: return;
    case Kind::Find: case Kind::Hunt: break;
    }
    const auto unit=find(c,*goal.target,goal.kind==Kind::Hunt || !goal.interact);
    if(!unit) {target.reset();if(!approachKnown(c,*goal.target)) explore(c,goal.target->name,false,ExplorationFocus::Units);return;}
    target=unit->key;targetAssignment=unit->assignmentRevision;
    if(goal.kind==Kind::Hunt) {
        if(onlineMonsterCorpse(*unit)) {advance(c,"Assigned target death mode observed: "+goal.target->name);return;}
        if(!c.combat.hostile(*unit)) throw std::runtime_error("Hunt target is not an eligible hostile monster");
        if(!policy.enabled) throw std::runtime_error("Hunt requires combat to be enabled");
        if(!c.combat.monsterTargetEligible(*unit,false)) {
            cancelWalk(c);wait("hunt-target-state","Waiting for the observed target's actual attack eligibility");return;
        }
        if(!approachPoint(c,*unit->position,"hunt target",2)) explore(c,"reachable approach to "+goal.target->name);
        return;
    }
    if(goal.interact) {
        if(c.session.read().world.npcConversation && c.session.read().world.npcConversation->type==1 &&
            c.session.read().world.npcConversation->source==unit->key.id) {
            advance(c,"Assigned NPC conversation observed");return;
        }
        walk(c,{{"goal","unit"},{"unitType",unit->key.type},{"unitId",unit->key.id},{"interact",true}});return;
    }
    if((position(*unit->position)-c.position).length()>6) {
        if(!approachPoint(c,*unit->position,"assigned find target",4))
            explore(c,"reachable approach to "+goal.target->name);
        return;
    }
    advance(c,"Assigned target found and approached: "+goal.target->name);
}
}
