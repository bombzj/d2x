#include "ai_impl.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
std::string_view DebugAi::Impl::passageName(Passage kind) {
    switch(kind) {
    case Passage::Door:return "door";
    case Passage::CreatePortal:return "scroll";
    case Passage::WorldEvent:return "world";
    case Passage::Transfer:return "enter";
    }
    return "unknown";
}
DebugAi::Impl::PassageWait &DebugAi::Impl::beginPassage(Passage kind,std::chrono::milliseconds initial,Clock::time_point deadline) {
    const auto [it,created]=navigation.passageWaits.try_emplace(kind);
    auto &pending=it->second;
    if(created) {
        pending.retryAt=Clock::now()+initial;pending.deadline=deadline;
        note("passage-wait",std::string(passageName(kind))+" initial="+std::to_string(initial.count())+"ms");
    }
    return pending;
}
bool DebugAi::Impl::retryPassage(Passage kind,std::string_view detail) {
    const auto found=navigation.passageWaits.find(kind);if(found==navigation.passageWaits.end()) return false;
    auto &pending=found->second;const auto now=Clock::now();
    if(now<pending.retryAt || now>=pending.deadline || inputBlocked) return false;
    pending.retryAt=now+passageRetryInterval;++pending.attempts;
    note("passage-retry",std::string(passageName(kind))+" n="+std::to_string(pending.attempts)+" "+std::string(detail));
    return true;
}
bool DebugAi::Impl::servicePassage(Context &c,const Goal &goal,bool prepared) {
    const bool transfer=navigation.passageWaits.contains(Passage::Transfer);
    const bool questEntrance=goal.kind==Kind::Quest && questState.questRun.step<goal.steps.size() &&
        goal.steps[questState.questRun.step].action==QuestAction::Portal;
    const bool assignedEntrance=navigation.travelPhase==TravelPhase::WarpExit && navigation.legUnit.has_value();
    const bool clearing=goal.kind==Kind::Quest && questState.questRun.step<goal.steps.size() && goal.steps[questState.questRun.step].clearCombat;
    const bool opening=questState.questRun.worldWait.has_value() && !clearing;
    if(!navigation.pendingDoor && !portalIssued && !transfer && !opening && !questEntrance && !assignedEntrance) return false;
    using Owner=DebugActionOwner;
    // Passage continuation runs before optional combat, even when a combat
    // child is still active. Consumptive operations keep their own receipts.
    if(navigation.pendingDoor) {
        if(canInput(c,Owner::Goal,DebugAllInputs) && retryPassage(Passage::Door,"operate assigned door")) {
            const auto key=navigation.pendingDoor->first;
            releaseCombat(c);
            walk(c,{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{key.type,key.id},.interact=true,.purpose="door"});
        }
    } else if(prepared && canInput(c,supply.supplyStage==SupplyStage::None?Owner::Goal:Owner::Supply,DebugAllInputs)) {
        const auto previous=cursor;
        releaseCombat(c);
        if(supply.supplyStage!=SupplyStage::None) supplyDetour(c,goal);
        else executeGoal(c,goal);
        if(!active() || cursor!=previous) return true;
    }
    const bool stillWaiting=navigation.pendingDoor || portalIssued || questState.questRun.worldWait || questEntrance || assignedEntrance ||
        navigation.passageWaits.contains(Passage::Transfer) ||
        (navigation.submittedTravel && ((navigation.travelPhase==TravelPhase::CrossWait && navigation.legUnit) ||
         (questState.questRun.pending && questState.questRun.pending->action==QuestPending::Action::Portal)));
    if(!stillWaiting) return false;
    // This branch returns before the main decision loop's mobility tail.
    // Let the same career movement planner own an entrance approach too;
    // a remembered waiting target must not keep it in combat mode.
    if(c.walk.active()) {
        if(battle.combatTarget && battle.combatRun.suspendedAt==Clock::time_point{}) {
            battle.combatRun.suspendedAt=Clock::now();note("combat-suspended","Passage approach owns navigation and mobility");
        }
        if(canInput(c,Owner::Mobility,DebugMotion|DebugSkill)) mobility(c);
    }
    // Never replace an active entrance approach with an attack. After the
    // approach settles, allow a single attack; the next passage attempt wins.
    if(!c.moving && !c.walk.active()) {
        const bool activating=questEntrance && questState.questRun.pending && questState.questRun.pending->action==QuestPending::Action::Object;
        const bool stationary=transfer || navigation.pendingDoor || portalIssued || activating;
        navigation.passageDefending=stationary;
        struct Reset {bool &flag;~Reset(){flag=false;}} reset{navigation.passageDefending};
        const auto approach=navigation.submittedTravel?std::optional{navigation.submittedTravel->unit}:
            activating?questState.questRun.pending->unit:navigation.pendingDoor?std::optional{navigation.pendingDoor->first}:std::nullopt;
        const bool approachSettled=!approach || c.town.interactionReady(c.observation.read(),*approach);
        const auto creation=navigation.passageWaits.find(Passage::CreatePortal);
        const bool creationSettled=creation==navigation.passageWaits.end() || creation->second.attempts>0;
        if(approachSettled && creationSettled && canInput(c,Owner::Combat,DebugMotion|DebugSkill)) {
            if(!stationary && avoidHazards(c)) return true;
            // Ranged waiting defense is scheduled after the continuation,
            // with its own small radius and no crowd/life-loss threshold.
            if(!battle.excursionSkill || meleeAttack(c,*battle.excursionSkill)) combat(c,goal,true,stationary);
        }
    }
    return true;
}
void DebugAi::Impl::observePopulation(const Context &c) {
    const auto &view=c.observation.read();const auto &world=view.world;
    if(navigation.populationMemoryGame!=view.gameGeneration) {
        navigation.populationMemoryGame=view.gameGeneration;navigation.populationMemoryRevision=0;navigation.populationMemory.clear();
        navigation.openedObjects.clear();navigation.targetSearches.clear();navigation.activeTargetSearch.reset();
        navigation.searchSeeds.clear();
        navigation.targetSearchDiagnostics=nullptr;navigation.searchEventSequence=0;
    }
    if(area) {
        auto [seed,fresh]=navigation.searchSeeds.try_emplace(*area,view.load.mapSeed);
        if(!fresh && seed->second!=view.load.mapSeed) {
            seed->second=view.load.mapSeed;
            std::erase_if(navigation.openedObjects,[&](const auto &entry){return entry.first.first==*area;});
            std::erase_if(navigation.targetSearches,[&](const auto &entry){return entry.second.area==*area;});
            navigation.activeTargetSearch.reset();
        }
    }
    observeSearchEvents(c);
    const auto now=Clock::now();
    // Seeing map tiles does not imply that their units have been assigned.
    // Keep settled subscriptions after 08 releases the room, so walking away
    // does not turn the same town corner into a fresh search objective.
    for(auto &[level,memory]:navigation.populationMemory) {
        (void)level;
        std::erase_if(memory.settlingRooms,[&](const auto &entry) {
            const auto [roomLevel,x,y]=entry.first;
            const auto key=std::tuple{uint8_t(roomLevel),uint16_t(x/5),uint16_t(y/5)};
            if(!world.rooms.contains(key)) return true;
            const auto receipt=world.roomAssignmentRevisions.find(key);
            if(receipt!=world.roomAssignmentRevisions.end() && memory.rooms.at(entry.first)!=receipt->second) return false;
            if(now<entry.second) return false;
            memory.searchedRooms.insert(entry.first);return true;
        });
    }
    if(navigation.populationMemoryRevision==world.revision) return;
    navigation.populationMemoryRevision=world.revision;
    // Original 07 precedes unit assignments. A renewed subscription, including
    // one to an already visited room, needs its own short settling window.
    for(const auto &[key,size]:world.rooms) {
        (void)size;
        const auto [level,x,y]=key;auto &memory=navigation.populationMemory[level];
        if(memory.seed!=view.load.mapSeed) {memory={};memory.seed=view.load.mapSeed;}
        const auto receipt=world.roomAssignmentRevisions.find(key);
        const auto revision=receipt==world.roomAssignmentRevisions.end()?0:receipt->second;
        const auto [entry,inserted]=memory.rooms.try_emplace(std::tuple{int(level),int(x)*5,int(y)*5},revision);
        if(inserted || entry->second!=revision) {
            entry->second=revision;memory.readyAt=now+std::chrono::milliseconds(250);
            memory.settlingRooms[entry->first]=memory.readyAt;
        }
    }
    const auto &rooms=c.town.navigationRooms();
    for(const auto &[key,unit]:world.units) if(key.type==1) {
        const bool dead=onlineMonsterCorpse(unit);
        for(auto &[level,memory]:navigation.populationMemory) {
            (void)level;
            const auto previous=memory.enemies.find(key);if(previous==memory.enemies.end()) continue;
            if(dead || previous->second.assignment!=unit.assignmentRevision || previous->second.identity!=c.combat.monsterClass(unit))
                memory.enemies.erase(previous);
        }
        if(dead || !unit.position) continue;
        const auto point=*unit.position;
        const auto room=std::find_if(rooms.begin(),rooms.end(),[&](const auto &r) {
            return r.loaded && point.x>=r.x && point.x<r.x+r.width && point.y>=r.y && point.y<r.y+r.height;
        });
        if(room==rooms.end()) continue;
        const bool hostile=c.combat.hostile(unit);
        for(auto &[level,memory]:navigation.populationMemory) {
            const auto previous=memory.enemies.find(key);if(previous==memory.enemies.end()) continue;
            if(level!=room->level || c.combat.knownHostileSource(unit)==false) memory.enemies.erase(previous);
        }
        if(hostile) navigation.populationMemory[uint16_t(room->level)].enemies[key]={point,unit.assignmentRevision,c.combat.monsterClass(unit),
            c.combat.superUnique(unit),c.combat.eliteMonster(unit),now};
    }
}
int DebugAi::Impl::encounterMovementRadius() const {
    const auto &pending=questState.questRun.worldWait;
    return pending && pending->residualRange?
        std::max(eventPolicy.encounterRadius,pending->residualRange+2*pending->witnessSize):eventPolicy.encounterRadius;
}
bool DebugAi::Impl::encounterEnemy(const Context &c,const OnlineUnit &unit) const {
    if(!unit.position || unit.key.type!=1 || !c.combat.hostile(unit) || !c.combat.monsterTargetEligible(unit,false)) return false;
    if(questState.questRun.encounterArea!=c.town.read().area || !questState.questRun.encounterPosition) return false;
    if(c.town.assignedUnitLevel(unit.key)!=questState.questRun.encounterArea) return false;
    const auto anchor=*questState.questRun.encounterPosition;
    const int dx=std::abs(int(unit.position->x)-anchor.x),dy=std::abs(int(unit.position->y)-anchor.y);
    if(std::hypot(dx,dy)<=eventPolicy.encounterRadius) return true;
    const auto &pending=questState.questRun.worldWait;
    if(!pending || !pending->residualRange) return false;
    // AIUTIL_GetDistanceToCoordinates_FullUnitSize uses the throne witness's SizeX.
    const int x=std::abs(dx-pending->witnessSize),y=std::abs(dy-pending->witnessSize);
    return std::max(x,y)+std::min(x,y)/2<pending->residualRange &&
        std::any_of(c.town.navigationRooms().begin(),c.town.navigationRooms().end(),[&](const auto &room) {
            return room.loaded && room.level==pending->area && unit.position->x>=room.x && unit.position->x<room.x+room.width &&
                unit.position->y>=room.y && unit.position->y<room.y+room.height;
        });
}
bool DebugAi::Impl::worldResultReady(const Context &c,const QuestStep &step) const {
    const auto &goal=goals.at(cursor);const auto &world=c.observation.read().world;
    if(!goal.requirements.empty() && questRequirementSatisfied(c,goal,questState.questRun.step)) return true;
    switch(step.action) {
    case QuestAction::Talk:
        return step.target && find(c,*step.target,true).has_value();
    case QuestAction::Hunt:
        if(step.target) if(const auto unit=find(c,*step.target,true)) return onlineMonsterCorpse(*unit) || c.combat.monsterTargetEligible(*unit,false);
        return false;
    case QuestAction::Operate:
        return step.spawnSource && step.target && find(c,*step.target,true).has_value();
    case QuestAction::Portal:
        if(step.cinematic && world.cinematic==step.cinematic) return true;
        if(step.area==c.town.read().area) return !step.cinematic;
        if(step.proximityTrigger) {
            const auto trigger=find(c,step.proximityTrigger->target,true);
            if(!trigger || trigger->mode!=2) return false;
        }
        if(step.target) if(const auto portal=find(c,*step.target,true)) {
            for(size_t row=0;row<definitions->objects.rows().size();++row)
                if(definitions->objects.number(row,"Id")==step.target->identity &&
                    (definitions->objects.number(row,"OperateFn")==34 || definitions->objects.number(row,"OperateFn")==66) &&
                    c.town.projectedObjectMode(c.observation.read(),portal->key)==1) return false;
            return c.town.permitsInteraction(c.observation.read(),portal->key);
        }
        if(step.cinematic) return false;
        return std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &target) {
            return target.interaction==OnlineMapInteraction::Portal && target.destination==step.area &&
                c.town.permitsInteraction(c.observation.read(),target.unit);
        });
    case QuestAction::Encounter:
        if(step.target) if(const auto portal=find(c,*step.target,true))
            return portal->mode && *portal->mode!=0 && c.town.permitsInteraction(c.observation.read(),portal->key) &&
                (!step.status || world.quests.statuses[questDefinition(step.stateQuest.value_or(goal.questId)).nativeSlot]==step.status);
        return false;
    default:return false;
    }
}
void DebugAi::Impl::waitForWorldResult(Context &c,const QuestStep &step,std::string_view expected) {
    if(!questState.questRun.worldWait) {
        cancelWalk(c);
        navigation.frontierRoute.reset();
        navigation.approachHint.reset();navigation.approachFrontier.reset();
        if(!c.town.read().area) return;
        WorldWait pending;pending.area=*c.town.read().area;pending.anchor=c.position;
        if(questState.questRun.encounterArea==pending.area && questState.questRun.encounterPosition)
            pending.anchor={float(questState.questRun.encounterPosition->x),float(questState.questRun.encounterPosition->y)};
        if(step.action==QuestAction::Encounter && step.clearCombat && step.witness && step.residualRange) {
            pending.residualRange=step.residualRange;pending.witnessSize=c.town.monsterSizeX(step.witness->identity);
            pending.clearanceRevision=++navigation.searchEventSequence;
            pending.soundSequence=c.observation.read().world.combatSequence;
        }
        pending.seconds=step.resultWaitSeconds?step.resultWaitSeconds:unsigned(eventPolicy.waitSeconds);
        pending.deadline=Clock::now()+std::chrono::seconds(pending.seconds);pending.expected=expected;
        if(step.action==QuestAction::Portal) {
            if(questState.questRun.portalDeadline==Clock::time_point{}) questState.questRun.portalDeadline=pending.deadline;
            pending.deadline=questState.questRun.portalDeadline;
        }
        beginPassage(Passage::WorldEvent,std::chrono::milliseconds(step.initialWaitMs?step.initialWaitMs:unsigned(eventPolicy.initialWaitMs)),pending.deadline);
        questState.questRun.worldWait=std::move(pending);note("world-wait-started",std::string(expected));
    }
    retryPassage(Passage::WorldEvent,"observe actual world result / entrance readiness");
    wait("quest-world-result",std::string(expected));
    if(!step.clearCombat) continuation=Continuation::WorldResult;
}
void DebugAi::Impl::waitAtWorldEvent(Context &c,const QuestStep &step,OnlinePoint at,std::string_view expected,float vicinity) {
    waitForWorldResult(c,step,expected);
    if(!questState.questRun.worldWait) return;
    auto &pending=*questState.questRun.worldWait;
    pending.anchor={float(at.x),float(at.y)};pending.vicinity=vicinity;
    // The same event owns approach, defensive interruptions and resumption.
    // A blocked approach never falls through to a whole-level search.
    if(!battle.retreatPoint && !battle.combatRun.reposition && !battle.combatRun.kitePoint) {
        if((c.position-pending.anchor).length()>vicinity) approachPoint(c,at,"observed world event",vicinity);
        else if(navigation.approachHint && c.walk.active()) {
            cancelWalk(c);navigation.approachHint.reset();navigation.approachFrontier.reset();
        }
    }
}
bool DebugAi::Impl::approachWorldTrigger(Context &c,const QuestStep &step) {
    if(!step.proximityTrigger || !area || (step.sourceArea && step.sourceArea!=area)) return false;
    const auto &rule=*step.proximityTrigger;
    const auto trigger=find(c,rule.target,true);
    // The server advances this object through QUESTFN, not silent ENDANIM.
    if(trigger && trigger->mode==2) return false;
    const auto marker=trigger?trigger->position:c.town.knownTargetPosition(*area,rule.target.type,rule.target.identity);
    if(!marker) {
        const auto portal=step.target?find(c,*step.target,true):std::nullopt;
        if(!portal || !portal->position) return false;
        waitAtWorldEvent(c,step,*portal->position,"Approaching the observed portal to receive its passage trigger");return true;
    }
    waitForWorldResult(c,step,"Waiting for the observed proximity passage to open");
    const Vec center{float(marker->x),float(marker->y)};
    const auto &rooms=c.town.navigationRooms();
    const auto room=std::find_if(rooms.begin(),rooms.end(),[&](const auto &r) {
        return r.loaded && r.level==*area && center.x>=r.x && center.x<r.x+r.width && center.y>=r.y && center.y<r.y+r.height;
    });
    int size=0;
    for(size_t row=0;row<definitions->objects.rows().size();++row)
        if(definitions->objects.number(row,"Id")==rule.target.identity) {size=definitions->objects.number(row,"SizeX").value_or(0);break;}
    const auto inside=[&](Vec at) {
        return room!=rooms.end() && at.x>=room->x && at.x<room->x+room->width && at.y>=room->y && at.y<room->y+room->height &&
            nativeUnitDistance(at,2,center,size)<rule.distance;
    };
    if(inside(c.position)) {
        waitAtWorldEvent(c,step,{uint16_t(c.position.x+.5f),uint16_t(c.position.y+.5f)},
            "Waiting inside the observed proximity trigger for the server to open the passage",1);return true;
    }
    const auto origin=c.town.read().origin;
    if(!origin || room==rooms.end()) {
        waitAtWorldEvent(c,step,*marker,"Approaching the observed passage trigger room");return true;
    }
    const Vec offset{float(origin->x),float(origin->y)};
    const auto grid=c.town.navigationGrid(c.observation.read());
    std::vector<DebugExplorationMemory::Cell> stances;
    // Either travel mode must first reach the same server trigger. Plan to
    // legal cells around it, since the unopened bridge center can be solid.
    const int radius=rule.distance+size/2+1;
    for(int y=std::max({0,room->y,int(marker->y)-radius});y<std::min({65536,room->y+room->height,int(marker->y)+radius+1});++y)
        for(int x=std::max({0,room->x,int(marker->x)-radius});x<std::min({65536,room->x+room->width,int(marker->x)+radius+1});++x) {
            const Vec at{float(x),float(y)};
            if(inside(at) && grid.walkable(at-offset+Vec{.5f,.5f},playerMovement) &&
                !frontierCoolingDown({uint16_t(x),uint16_t(y)},1)) stances.emplace_back(x,y);
        }
    if(!battle.retreatPoint && !battle.combatRun.reposition && !battle.combatRun.kitePoint)
        approachPoint(c,*marker,"observed passage proximity trigger",0,false,{},std::move(stances));
    return true;
}
void DebugAi::Impl::observeWorldWait(Context &c) {
    if(!questState.questRun.worldWait || cursor>=goals.size() || questState.questRun.step>=goals[cursor].steps.size()) return;
    const auto &step=goals[cursor].steps[questState.questRun.step];auto &pending=*questState.questRun.worldWait;
    // Facts are checked even while input is suspended and before deadlines.
    if(worldResultReady(c,step)) {
        note("world-result-observed",pending.expected);questState.questRun.worldWait.reset();navigation.passageWaits.erase(Passage::WorldEvent);
        continuation=Continuation::WorldResult;return;
    }
    if(c.town.read().area && c.town.read().area!=pending.area) {
        note("world-wait-left-scene",pending.expected);questState.questRun.worldWait.reset();navigation.passageWaits.erase(Passage::WorldEvent);return;
    }
    bool progress=false;
    if(pending.residualRange && step.stageSound && questState.questRun.encounterWitness) {
        const auto &world=c.observation.read().world;
        const auto [key,revision]=*questState.questRun.encounterWitness;
        const auto witness=world.units.find(key);
        if(witness!=world.units.end() && witness->second.assignmentRevision==revision)
            for(const auto &event:world.combatEvents) if(event.sequence>pending.soundSequence && event.kind==OnlineCombatEvent::Kind::Sound &&
                event.packet==0x2c && event.source==key && event.auxiliary==*step.stageSound) {
                pending.clearanceRevision=++navigation.searchEventSequence;
                if(pending.lastProgress!="witness stage sound") {progress=true;pending.lastProgress="witness stage sound";}
            }
        pending.soundSequence=world.combatSequence;
    }
    if(step.action==QuestAction::Encounter && step.clearCombat) for(const auto &[key,unit]:c.observation.read().world.units) {
        const auto identity=std::pair{key,unit.assignmentRevision};
        auto found=pending.enemies.find(identity);
        if(found==pending.enemies.end()) {
            if(!encounterEnemy(c,unit) || pending.enemies.size()>=256) continue;
            found=pending.enemies.emplace(identity,WorldWait::Enemy{}).first;
            if(pending.residualRange) pending.clearanceRevision=++navigation.searchEventSequence;
            progress=true;pending.lastProgress="new hostile assigned in encounter";
        }
        auto &enemy=found->second;
        if(!enemy.dead && onlineMonsterCorpse(unit)) {
            if(pending.residualRange) pending.clearanceRevision=++navigation.searchEventSequence;
            enemy.dead=true;progress=true;pending.lastProgress="actual encounter corpse";
        }
        if(unit.lifePercent) {
            const auto health=uint8_t(std::min(127,int(unit.lifeCarriesTriggerFlag?*unit.lifePercent&0x7f:*unit.lifePercent)));
            if(enemy.bestLife && health<*enemy.bestLife) {progress=true;pending.lastProgress="net encounter life decrease";}
            if(!enemy.bestLife || health<*enemy.bestLife) enemy.bestLife=health;
        }
    }
    if(progress) {
        ++pending.progress;pending.deadline=Clock::now()+std::chrono::seconds(pending.seconds);
        if(auto found=navigation.passageWaits.find(Passage::WorldEvent);found!=navigation.passageWaits.end()) found->second.deadline=pending.deadline;
    }
    if(!inputBlocked && Clock::now()>=pending.deadline)
        throw std::runtime_error("World event made no relevant progress: "+pending.expected+"; no operation replayed");
}
bool DebugAi::Impl::tacticalPositionAllowed(const Context &c,Vec at) const {
    const auto &rooms=c.town.navigationRooms();
    if(std::none_of(rooms.begin(),rooms.end(),[&](const auto &room) {
        return room.loaded && room.level==c.town.read().area && at.x>=room.x && at.y>=room.y && at.x<room.x+room.width && at.y<room.y+room.height;
    })) return false;
    if(supply.supplyStage==SupplyStage::None && questState.questRun.encounterArea==c.town.read().area && questState.questRun.encounterPosition) {
        const Vec anchor{float(questState.questRun.encounterPosition->x),float(questState.questRun.encounterPosition->y)};
        // When already displaced, permit movement back toward the scene.
        if((at-anchor).length()>std::max(float(encounterMovementRadius()),(c.position-anchor).length())) return false;
    }
    if(supply.supplyStage==SupplyStage::None && questState.questRun.worldWait && questState.questRun.worldWait->area==c.town.read().area) {
        const auto anchor=questState.questRun.worldWait->anchor;
        if((at-anchor).length()>std::max(float(encounterMovementRadius()),(c.position-anchor).length())) return false;
    }
    return true;
}
bool DebugAi::Impl::tacticalPathSafe(const Context &c,const std::deque<Vec> &path,Vec offset,const std::vector<VisualThreat> &hazards) const {
    const float initial=threatClearance(hazards,c.position);
    Vec previous=c.position;
    for(const auto local:path) {
        const Vec at=local+offset-Vec{.5f,.5f};const Vec delta=at-previous;
        const int count=std::max(1,int(std::ceil(delta.length())));
        for(int i=1;i<=count;++i) {
            const Vec sample=previous+delta*(float(i)/float(count));
            if(!tacticalPositionAllowed(c,sample)) return false;
            // Escape may start inside a hazard; it must not dive deeper or
            // cross another danger after leaving the initial few cells.
            if(threatClearance(hazards,sample)<((sample-c.position).length()<=3?std::min(0.f,initial):0.f)) return false;
        }
        previous=at;
    }
    return true;
}
std::optional<OnlinePoint> DebugAi::Impl::encounterStance(Context &c,OnlinePoint anchor) {
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    auto &diagnostics=questState.questRun.encounterStanceDiagnostics;
    diagnostics={{"anchor",{anchor.x,anchor.y}},{"from",{c.position.x,c.position.y}},{"result","terrain-unavailable"}};
    if(!map || !origin || !area) return {};
    const auto *memory=observeTerrain(c);if(!memory) return {};
    const Vec offset{float(origin->x),float(origin->y)},center{float(anchor.x),float(anchor.y)};
    const auto hazards=visibleThreats(c);
    std::vector<DebugExplorationMemory::Cell> candidates;
    size_t sampled=0,unknown=0,blocked=0,cooling=0,occluded=0,dangerous=0;
    const int minimum=eventPolicy.standoffMin,maximum=eventPolicy.standoffMax;
    // These are final observation goals, not single-cast escape landings.
    // Inspect known floor throughout the annulus, including narrow lanes
    // between the old radial samples. Dynamic occupancy belongs to execution.
    for(int y=std::max(0,int(anchor.y)-maximum);y<=std::min(65535,int(anchor.y)+maximum);++y)
        for(int x=std::max(0,int(anchor.x)-maximum);x<=std::min(65535,int(anchor.x)+maximum);++x) {
            const int dx=x-anchor.x,dy=y-anchor.y,squared=dx*dx+dy*dy;
            if(squared<minimum*minimum || squared>maximum*maximum) continue;
            ++sampled;const Vec at{float(x),float(y)};
            if(!tacticalPositionAllowed(c,at)) {++unknown;continue;}
            if(!memory->stand(x,y)) {++blocked;continue;}
            if(frontierCoolingDown({uint16_t(x),uint16_t(y)},2)) {++cooling;continue;}
            if(!map->grid.collisionSegment(at-offset+Vec{.5f,.5f},center-offset+Vec{.5f,.5f},4)) {++occluded;continue;}
            if(threatClearance(hazards,at)<0) {++dangerous;continue;}
            candidates.emplace_back(x,y);
        }
    const auto [range,restricted]=travelMobility(c,*area,false);
    diagnostics["sampled"]=sampled;diagnostics["outsideLoadedScene"]=unknown;diagnostics["blockedFloor"]=blocked;
    diagnostics["cooling"]=cooling;diagnostics["occluded"]=occluded;diagnostics["dangerous"]=dangerous;
    diagnostics["candidates"]=candidates.size();diagnostics["teleportRange"]=range;
    diagnostics["result"]=candidates.empty()?"no-safe-known-floor":"planning";
    if(candidates.empty()) return {};
    // The common route planner validates walking, legal multi-cast relays
    // and (for walkers) observed transport links. A Partial endpoint is only
    // a relay; keep its final objective as the encounter's retained stance.
    const auto plan=c.walk.navigationSearch().observedRoute(*area,c.position,range,restricted,center,0,0,false,
        std::move(candidates),{},range==0);
    std::optional<OnlinePoint> stance;
    if(!plan.points.empty()) stance=plan.objective.value_or(plan.points.back());
    diagnostics["expanded"]=plan.expanded;diagnostics["routePoints"]=plan.points.size();
    diagnostics["result"]=stance?"route-selected":plan.outcome==DebugExplorationMemory::Plan::Outcome::StartUnavailable?
        "start-unavailable":plan.outcome==DebugExplorationMemory::Plan::Outcome::ExecutionBlocked?"execution-blocked":"no-known-route";
    diagnostics["selected"]=stance?Json{stance->x,stance->y}:Json(nullptr);
    return stance;
}
} // namespace d2x
