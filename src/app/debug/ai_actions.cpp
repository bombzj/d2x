#include "ai_impl.hpp"
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
OnlinePoint point(Vec p) {
    if(!std::isfinite(p.x) || !std::isfinite(p.y) || p.x<0 || p.y<0 || p.x>65535 || p.y>65535)
        throw std::runtime_error("AI input outside native coordinates");
    return {uint16_t(std::floor(p.x)),uint16_t(std::floor(p.y))};
}
}
std::optional<OnlineUnit> DebugAi::Impl::find(const Context &c,const Selector &selector,bool corpses) const {
    std::optional<OnlineUnit> best;float nearest=std::numeric_limits<float>::max();
    for(const auto &[key,unit]:c.session.read().world.units) {
        if(key.type!=selector.type || !unit.position || !unit.classId) continue;
        const auto identity=key.type==1?c.combat.monsterClass(unit):unit.classId;
        if(identity!=selector.identity || (key.type==1 && !corpses && onlineMonsterCorpse(unit))) continue;
        if(selector.superUnique && c.combat.superUnique(unit)!=selector.superUnique) continue;
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
bool DebugAi::Impl::combat(Context &c,const Goal &goal) {
    const auto &view=c.session.read();
    if(!policy.enabled || c.town.read().town || view.world.npcRequested || view.world.waypointSource) return false;
    const auto now=Clock::now();
    std::erase_if(bypassedThreats,[&](const auto &entry){return now>=entry.second;});
    const auto clearLine=[&](const OnlineUnit &unit) {
        const auto *map=c.town.map();const auto origin=c.town.read().origin;if(!map || !origin) return false;
        const Vec offset{float(origin->x),float(origin->y)};
        return map->grid.lightSegment(c.position-offset+Vec{.5f,.5f},position(*unit.position)-offset+Vec{.5f,.5f});
    };
    const auto objective=goal.kind==Kind::Hunt && goal.target?find(c,*goal.target):std::nullopt;
    const bool nearObjective=objective && (position(*objective->position)-c.position).length()<=policy.engage;
    std::vector<OnlineUnit> enemies;std::optional<OnlineUnit> chosen;float nearest=float(policy.engage);
    for(const auto &[key,unit]:view.world.units) {
        if(key.type!=1 || !unit.position || !c.combat.hostile(unit) || !c.combat.monsterTargetEligible(unit,false) ||
            bypassedThreats.contains({key,unit.assignmentRevision})) continue;
        const float d=(position(*unit.position)-c.position).length();
        const bool visible=clearLine(unit);
        if(d<=policy.engage && visible) enemies.push_back(unit);
        const bool clearing=goal.kind==Kind::Quest && goal.area==area && cursor<goals.size() && questRun.step<goals[cursor].steps.size() &&
            goals[cursor].steps[questRun.step].action==QuestAction::Clear;
        const bool relevant=policy.scope=="clear" || clearing || nearObjective || (policy.scope=="defensive" && d<=policy.radius);
        if(relevant && visible && d<nearest) {nearest=d;chosen=unit;}
    }
    if(nearObjective && c.combat.hostile(*objective)) chosen=objective;
    if(!chosen) {
        if(combatTarget || combatOwned) {
            stop(c.walk,c.control,c.combat);walkIntent=nullptr;combatTarget.reset();retreatPoint.reset();note("combat-clear");
        }
        return false;
    }
    if(combatTarget!=chosen->key) {
        cancelWalk(c);combatTarget=chosen->key;note("combat-target",std::to_string(chosen->key.id));
        combatLife=chosen->lifePercent;combatProgress=now;
        if(travelPhase==TravelPhase::CrossWait && !legUnit) {
            travelPhase=TravelPhase::Route;legDestination.reset();
        }
    }
    if(combatLife!=chosen->lifePercent) {combatLife=chosen->lifePercent;combatProgress=now;}
    const bool required=(goal.kind==Kind::Hunt && objective && chosen->key==objective->key) ||
        (goal.kind==Kind::Quest && goal.area==area && cursor<goals.size() && questRun.step<goals[cursor].steps.size() &&
         goals[cursor].steps[questRun.step].action==QuestAction::Clear);
    if(!required && chosen->lifePercent && now-combatProgress>=std::chrono::seconds(6)) {
        // A queued cast is not a hit. Do not let an incidental target with no
        // observed life change indefinitely consume the high-level journey.
        // This only resumes normal input; required kills/clear remain intact.
        bypassedThreats[{chosen->key,chosen->assignmentRevision}]=now+std::chrono::seconds(10);
        note("unproductive-threat-bypassed",std::to_string(chosen->key.id));
        stop(c.walk,c.control,c.combat);walkIntent=nullptr;combatTarget.reset();retreatPoint.reset();return false;
    }
    if(retreatPoint) {
        if(walkState(c)=="failed") {
            note("retreat-unavailable",c.walk.snapshot().value("reason",std::string{}));
        } else if(now<retreatUntil && (position(*retreatPoint)-c.position).length()>2) {
            wait("retreat","Keeping distance from the observed close crowd");walkPoint(c,*retreatPoint);return true;
        }
        cancelWalk(c);retreatPoint.reset();
    }
    const auto close=std::count_if(enemies.begin(),enemies.end(),[&](const auto &u){return (position(*u.position)-c.position).length()<=policy.radius;});
    if(close>=policy.crowd && now>=retreatReady) {
        const auto *map=c.town.map();const auto origin=c.town.read().origin;
        float best=0;std::optional<OnlinePoint> safe;
        if(map && origin) {
            const Vec offset{float(origin->x),float(origin->y)};
            const auto safety=[&](Vec p) {
                float minimum=100;for(const auto &enemy:enemies) minimum=std::min(minimum,(position(*enemy.position)-p).length());return minimum;
            };
            best=safety(c.position)+1;
            for(int direction=0;direction<16;++direction) {
                const float angle=float(direction)*6.28318530718f/16;
                const Vec candidate=c.position+Vec{std::cos(angle),std::sin(angle)}*float(policy.retreat);
                if(candidate.x<0 || candidate.y<0 || candidate.x>65535 || candidate.y>65535) continue;
                const auto requested=point(candidate);const auto actual=position(requested);
                const Vec local=actual-offset+Vec{.5f,.5f};
                const auto path=map->grid.path(c.position-offset+Vec{.5f,.5f},local,false,playerMovement,true);
                if(path.empty() || point(path.back()+offset)!=requested) continue;
                const float score=safety(actual);if(score>best) {best=score;safe=requested;}
            }
        }
        retreatReady=now+std::chrono::seconds(4);
        if(safe) {
            cancelWalk(c);OnlineCombatCommand stopSkill;stopSkill.action=OnlineCombatCommand::Action::Stop;c.combat.submit(stopSkill);
            retreatPoint=safe;retreatUntil=now+std::chrono::seconds(2);wait("retreat","Crowd threshold reached; moving to a safer known point");
            note("retreat-started",std::to_string(close));walkPoint(c,*safe);return true;
        }
    }
    uint16_t skill=policy.skill.value_or(definitions->skill("Attack"));
    auto entry=std::find_if(c.combat.skills().begin(),c.combat.skills().end(),[&](const auto &v){return v.id==skill && (v.level || v.innate) && !v.passive;});
    if(entry==c.combat.skills().end()) {
        if(policy.skill) throw std::runtime_error("Configured combat skill is not a known active skill for this character");
        skill=definitions->skill("Attack");
    }
    const auto attack=definitions->skill("Attack");int range=skill==attack?3:policy.range;
    for(size_t row=0;row<definitions->skills.rows().size();++row)
        if(definitions->skills.number(row,"Id")==skill && definitions->skills.value(row,"range")=="h2h") range=3;
    bool clear=true;
    if(const auto *map=c.town.map();map && c.town.read().origin) {
        const auto origin=*c.town.read().origin;const Vec offset{float(origin.x),float(origin.y)};
        clear=map->grid.lightSegment(c.position-offset+Vec{.5f,.5f},position(*chosen->position)-offset+Vec{.5f,.5f});
    }
    if((position(*chosen->position)-c.position).length()>range || !clear) {
        if(openDoor(c)) return true;
        if(approachPoint(c,*chosen->position,"assigned hostile target",2)) {phase="combat-approach";return true;}
        cancelWalk(c);combatTarget.reset();return false;
    }
    if(!c.combat.skillTargetEligible(*chosen,skill)) {
        wait("combat-wait","Configured skill cannot currently target this observed monster");return true;
    }
    const auto aura=battleAura(c);const auto hand=aura?OnlineSkillHand::Left:OnlineSkillHand::Right;
    if(aura) {
        if(!knownSkill(c,*aura)) throw std::runtime_error("Battle aura is no longer available");
        if(!knownSkill(c,skill,true)) throw std::runtime_error("A battle aura requires a left-hand attack skill");
        if(!selectSkill(c,*aura,OnlineSkillHand::Right)) {
            cancelWalk(c);wait("combat-aura","Switching from travel to the battle aura");return true;
        }
    }
    if(!selectSkill(c,skill,hand)) {
        cancelWalk(c);wait("combat-select","Waiting for normal attack skill selection");
        return true;
    }
    wait("combat-cast","Fighting an observed hostile through the normal skill interface");
    if(now-lastAttack<std::chrono::milliseconds(600)) return true;
    if(view.world.combatRequest && view.world.combatRequest->state==OnlineCombatRequest::State::Pending) return true;
    lastAttack=now;
    cancelWalk(c);OnlineCombatCommand cast;cast.action=OnlineCombatCommand::Action::Cast;
    cast.hand=hand;cast.target=chosen->key;cast.stationary=true;cast.repeat=false;
    if(c.combat.submit(cast)) {combatOwned=true;++castRequests;}
    else reason=c.combat.reason();
    return true;
}
void DebugAi::Impl::acquire(Context &c,const Goal &goal) {
    if(acquireItem(c,goal)) advance(c,"Owned item observed: "+goal.item);
}
bool DebugAi::Impl::settleItemInput(Context &c) {
    // A released missile/teleport is not the end of the player's skill mode.
    // This is debug input pacing, not a change to ItemMode's busy rules. Keep
    // judging every 200 ms and wait for a quiet interval before sending one
    // inventory request; never use a rejected/unknown request as a retry loop.
    const auto cast=std::max(lastAttack,mobilityDeadline-std::chrono::seconds(3));
    if(Clock::now()-cast>=std::chrono::milliseconds(1500)) return true;
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
        const bool flight=teleport && Clock::now()>=nextMobility && (world-c.position).length()>=1 &&
            (world-c.position).length()<=mobilityPolicy.distance &&
            (!mobilityTrials.contains(trial) || mobilityTrials.at(trial)<2);
        ++pathSearches;const auto path=map->grid.path(c.position-offset+Vec{.5f,.5f},local,false,playerMovement,true);
        if(path.empty() && !flight) continue;
        wait("pickup-approach",flight?"Selecting a normal teleport landing at the item or its reachable side":"Walking around to another valid pickup position");
        walk(c,{{"goal","point"},{"x",p.x},{"y",p.y},{"interact",false},{"purpose","pickup-approach"},
            {"itemId",item.id},{"groundX",item.groundX},{"groundY",item.groundY}});
        // Quest reward topics can still own the parent's pending snapshot in
        // this decision. Prepare normal teleport input before the child tries
        // an impossible walking path; no coordinates are changed here.
        if(path.empty()) mobility(c);
        return false;
    }
    if((at-c.position).length()>12) {
        if(!approachPoint(c,{item.groundX,item.groundY},"assigned ground item",8))
            explore(c,"reachable approach to the assigned ground item");
        return false;
    }
    wait("pickup-approach-wait","No current legal pickup approach; waiting for normal collision or skill availability");return false;
}
bool DebugAi::Impl::acquireItem(Context &c,const Goal &goal) {
    if(owned(c,goal.item)) return true;
    const auto now=Clock::now();const auto &world=c.session.read().world;
    if(pickupIssued) {
        wait("pickup-wait","Waiting for actual item ownership, not merely a related inventory update");
        if(world.itemRequest && world.itemRequest->command.item==pickupId &&
            (world.itemRequest->state==OnlineItemRequest::State::Rejected || world.itemRequest->state==OnlineItemRequest::State::Interrupted ||
             world.itemRequest->state==OnlineItemRequest::State::TimedOut)) throw std::runtime_error("Native pickup did not confirm ownership");
        if(now>=actionDeadline) throw std::runtime_error("Pickup ownership was not observed; request is not blindly repeated");
        return false;
    }
    std::optional<OnlineItem> ground;float nearest=std::numeric_limits<float>::max();
    for(const auto &[id,item]:world.items) {
        (void)id;if(item.code!=goal.item || item.mode!=3 || !item.positionKnown) continue;
        const float d=(Vec{float(item.groundX),float(item.groundY)}-c.position).length();if(d<nearest) {nearest=d;ground=item;}
    }
    if(ground) {
        if(!approachPickup(c,*ground)) return false;
        cancelWalk(c);OnlineItemCommand take;take.action=OnlineItemAction::Pickup;take.item=ground->id;take.itemRevision=ground->revision;
        if(c.inventory.submit(c.session,take)) {
            pickupId=ground->id;pickupIssued=true;actionDeadline=now+std::chrono::seconds(12);note("pickup-submitted",goal.item);
        } else wait("pickup-blocked",c.inventory.reason());
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
                objectIssued=true;actionDeadline=now+std::chrono::seconds(12);++interactions;cancelWalk(c);note("item-source-submitted",goal.item);
            }
            return false;
        }
    }
    if(goal.sourceObject && approachKnown(c,Selector{2,*goal.sourceObject,"item source"})) return false;
    explore(c,"item "+goal.item);return false;
}
void DebugAi::Impl::returnTown(Context &c) {
    if(reachTown(c)) advance(c,"Native town scene observed");
}
bool DebugAi::Impl::reachTown(Context &c) {
    if(c.town.read().town) return true;
    const auto &view=c.session.read();const auto now=Clock::now();
    if(!view.load.townArea) throw std::runtime_error("Current native act has no assigned town");
    if(portalIssued) {
        if(portalUsed) {
            wait("town-portal-wait","Waiting for the native town scene");
            if(now>=actionDeadline) throw std::runtime_error("Own portal was used but town was not observed");
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
                portalUsed=true;actionDeadline=now+std::chrono::seconds(15);
                phase="town-portal-wait";reason="Waiting for the native town scene";
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
                portalAttempted=portalIssued=true;oldPortals.clear();actionDeadline=now+std::chrono::seconds(12);
                note("existing-own-portal-observed");return false;
            }
        }
    // A scroll/tome is a real resource. If none is available, use normal
    // walking/known unlocked waypoints rather than granting one.
    if(!portalAttempted && !c.town.read().townPortalSkills.empty()) {
        portalAttempted=true;
        oldPortals.clear();for(const auto &[key,unit]:view.world.units)
            if(key.type==2 && unit.portalOwner==view.load.playerUnitId) oldPortals.insert(key.id);
        cancelWalk(c);
        if(c.control.townPortal()) {portalIssued=true;actionDeadline=now+std::chrono::seconds(12);note("town-portal-submitted");return false;}
    }
    wait("return-walking","No usable town portal; using normal travel back to town");
    return travel(c,*view.load.townArea);
}
void DebugAi::Impl::deliver(Context &c,const Goal &goal) {
    const auto &view=c.session.read();const auto slot=goal.questSlot;
    if(view.world.quests.playerFlags && ((*view.world.quests.playerFlags)[slot]&3) && !owned(c,goal.item)) {
        if(view.world.npcRequested) c.session.close_npc();
        advance(c,"Item consumed and actual native reward flag observed: "+goal.quest);return;
    }
    if(!owned(c,goal.item)) throw std::runtime_error("No required item is owned and no delivery reward flag confirms completion");
    const auto npc=find(c,*goal.target);
    if(!npc) {if(!approachKnown(c,*goal.target)) explore(c,"delivery NPC "+goal.target->name);return;}
    const auto now=Clock::now();
    if(!view.world.npcConversation || view.world.npcConversation->type!=1 || view.world.npcConversation->source!=npc->key.id) {
        if(view.world.npcRequested && view.world.npcRequested!=npc->key.id) {c.session.close_npc();return;}
        wait("delivery-approach","Opening the actual delivery NPC conversation");
        walk(c,{{"goal","unit"},{"unitType",1},{"unitId",npc->key.id},{"interact",true}});return;
    }
    wait("delivery-dialogue","Acknowledging only actually offered automatic text or the verified Malus topic");
    if(!greetingIssued && npc->classId) {greetingIssued=c.session.confirm_npc_voice(*npc->classId);if(greetingIssued)return;}
    const auto conversation=*view.world.npcConversation;
    for(const auto &message:conversation.messages) {
        // D2MOO A1Q3 ScrollMessage 163 deletes hdm and sets REWARDPENDING.
        // It is acknowledged only when the real NPC offered it to this client.
        if((message.menu==0 || std::find(goal.topics.begin(),goal.topics.end(),message.stringId)!=goal.topics.end()) &&
            !conversation.acknowledged.contains(message.stringId)) {
            if(c.session.acknowledge_npc_message(message.stringId)) {
                ++interactions;actionDeadline=now+std::chrono::seconds(12);note("npc-topic-acknowledged",std::to_string(message.stringId));
            }
            return;
        }
    }
    if(actionDeadline==Clock::time_point{}) actionDeadline=now+std::chrono::seconds(12);
    if(now>=actionDeadline) throw std::runtime_error("NPC did not confirm item consumption and reward readiness");
}
void DebugAi::Impl::execute(Context &c,const Goal &goal) {
    if(goal.kind==Kind::Quest) {quest(c,goal);return;}
    if(goal.kind==Kind::Acquire && owned(c,goal.item)) {advance(c,"Owned item already satisfies acquire: "+goal.item);return;}
    if(goal.area && (area!=goal.area || travelDestination) && !travel(c,*goal.area)) return;
    switch(goal.kind) {
    case Kind::Travel: advance(c,"Native target area observed");return;
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
    if(!unit) {target.reset();if(!approachKnown(c,*goal.target)) explore(c,goal.target->name);return;}
    target=unit->key;targetAssignment=unit->assignmentRevision;
    if(goal.kind==Kind::Hunt) {
        if(onlineMonsterCorpse(*unit)) {advance(c,"Assigned target death mode observed: "+goal.target->name);return;}
        if(!c.combat.hostile(*unit)) throw std::runtime_error("Hunt target is not an eligible hostile monster");
        if(!policy.enabled) throw std::runtime_error("Hunt requires combat to be enabled");
        if(!approachPoint(c,*unit->position,"hunt target",2)) explore(c,"reachable approach to "+goal.target->name);
        return;
    }
    if((position(*unit->position)-c.position).length()>6) {
        if(!approachPoint(c,*unit->position,"assigned find target",4))
            explore(c,"reachable approach to "+goal.target->name);
        return;
    }
    if(goal.interact) {
        if(c.session.read().world.npcConversation && c.session.read().world.npcConversation->type==1 &&
            c.session.read().world.npcConversation->source==unit->key.id) {
            advance(c,"Assigned NPC conversation observed");return;
        }
        walk(c,{{"goal","unit"},{"unitType",unit->key.type},{"unitId",unit->key.id},{"interact",true}});return;
    }
    advance(c,"Assigned target found and approached: "+goal.target->name);
}
}
