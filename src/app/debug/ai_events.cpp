#include "ai_impl.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::observePopulation(const Context &c) {
    const auto &view=c.session.read();const auto &world=view.world;
    if(populationMemoryGame!=view.gameGeneration) {
        populationMemoryGame=view.gameGeneration;populationMemoryRevision=0;populationMemory.clear();
    }
    if(populationMemoryRevision==world.revision) return;
    populationMemoryRevision=world.revision;
    const auto now=Clock::now();
    // Original 07 precedes unit assignments. A renewed subscription, including
    // one to an already visited room, needs its own short settling window.
    for(const auto &[key,size]:world.rooms) {
        (void)size;
        const auto [level,x,y]=key;auto &memory=populationMemory[level];
        if(memory.seed!=view.load.mapSeed) {memory={};memory.seed=view.load.mapSeed;}
        const auto receipt=world.roomAssignmentRevisions.find(key);
        const auto revision=receipt==world.roomAssignmentRevisions.end()?0:receipt->second;
        const auto [entry,inserted]=memory.rooms.try_emplace(std::tuple{int(level),int(x)*5,int(y)*5},revision);
        if(inserted || entry->second!=revision) {
            entry->second=revision;memory.readyAt=now+std::chrono::milliseconds(250);
        }
    }
    const auto &rooms=c.town.navigationRooms();
    for(const auto &[key,unit]:world.units) if(key.type==1) {
        const bool dead=onlineMonsterCorpse(unit);
        for(auto &[level,memory]:populationMemory) {
            (void)level;
            const auto previous=memory.enemies.find(key);if(previous==memory.enemies.end()) continue;
            if(dead || previous->second.assignment!=unit.assignmentRevision || previous->second.identity!=unit.classId)
                memory.enemies.erase(previous);
        }
        if(dead || !unit.position) continue;
        const auto point=*unit.position;
        const auto room=std::find_if(rooms.begin(),rooms.end(),[&](const auto &r) {
            return r.loaded && point.x>=r.x && point.x<r.x+r.width && point.y>=r.y && point.y<r.y+r.height;
        });
        if(room==rooms.end()) continue;
        const bool hostile=c.combat.hostile(unit);
        for(auto &[level,memory]:populationMemory) {
            const auto previous=memory.enemies.find(key);if(previous==memory.enemies.end()) continue;
            if(level!=room->level || c.combat.knownHostileSource(unit)==false) memory.enemies.erase(previous);
        }
        if(hostile) populationMemory[uint16_t(room->level)].enemies[key]={point,unit.assignmentRevision,unit.classId};
    }
}
int DebugAi::Impl::encounterMovementRadius() const {
    const auto &pending=questRun.worldWait;
    return pending && pending->residualRange && Clock::now()>=pending->inspectResidualAt?
        std::max(eventPolicy.encounterRadius,pending->residualRange+2*pending->witnessSize):eventPolicy.encounterRadius;
}
bool DebugAi::Impl::encounterEnemy(const Context &c,const OnlineUnit &unit) const {
    if(!unit.position || unit.key.type!=1 || !c.combat.hostile(unit) || !c.combat.monsterTargetEligible(unit,false)) return false;
    if(questRun.encounterArea!=c.town.read().area || !questRun.encounterPosition) return false;
    const auto anchor=*questRun.encounterPosition;
    const int dx=std::abs(int(unit.position->x)-anchor.x),dy=std::abs(int(unit.position->y)-anchor.y);
    if(std::hypot(dx,dy)<=eventPolicy.encounterRadius) return true;
    const auto &pending=questRun.worldWait;
    if(!pending || !pending->residualRange || Clock::now()<pending->inspectResidualAt) return false;
    // AIUTIL_GetDistanceToCoordinates_FullUnitSize uses the throne witness's SizeX.
    const int x=std::abs(dx-pending->witnessSize),y=std::abs(dy-pending->witnessSize);
    return std::max(x,y)+std::min(x,y)/2<pending->residualRange &&
        std::any_of(c.town.navigationRooms().begin(),c.town.navigationRooms().end(),[&](const auto &room) {
            return room.loaded && room.level==pending->area && unit.position->x>=room.x && unit.position->x<room.x+room.width &&
                unit.position->y>=room.y && unit.position->y<room.y+room.height;
        });
}
bool DebugAi::Impl::worldResultReady(const Context &c,const QuestStep &step) const {
    const auto &goal=goals.at(cursor);const auto &world=c.session.read().world;
    if(!goal.requirements.empty() && questRequirementSatisfied(c,goal,questRun.step)) return true;
    switch(step.action) {
    case QuestAction::Talk:
        return step.target && find(c,*step.target,true).has_value();
    case QuestAction::Hunt:
        if(step.target) if(const auto unit=find(c,*step.target,true)) return onlineMonsterCorpse(*unit) || c.combat.monsterTargetEligible(*unit,false);
        return false;
    case QuestAction::Portal:
        if(step.cinematic) return world.cinematic==step.cinematic;
        if(step.area==c.town.read().area) return true;
        if(step.target) if(const auto portal=find(c,*step.target,true))
            return c.town.permitsInteraction(c.session.read(),portal->key);
        return std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &target) {
            return target.interaction==OnlineMapInteraction::Portal && target.destination==step.area &&
                c.town.permitsInteraction(c.session.read(),target.unit);
        });
    case QuestAction::Encounter:
        if(step.target) if(const auto portal=find(c,*step.target,true))
            return portal->mode && *portal->mode!=0 && c.town.permitsInteraction(c.session.read(),portal->key) &&
                (!step.status || world.quests.statuses[questDefinition(step.stateQuest.value_or(goal.questId)).nativeSlot]==step.status);
        return false;
    default:return false;
    }
}
void DebugAi::Impl::waitForWorldResult(Context &c,const QuestStep &step,std::string_view expected) {
    cancelWalk(c);
    if(!questRun.worldWait) {
        if(!c.town.read().area) return;
        WorldWait pending;pending.area=*c.town.read().area;pending.anchor=c.position;
        if(questRun.encounterArea==pending.area && questRun.encounterPosition)
            pending.anchor={float(questRun.encounterPosition->x),float(questRun.encounterPosition->y)};
        if(step.action==QuestAction::Encounter && step.clearCombat && step.witness && step.residualRange) {
            pending.residualRange=step.residualRange;pending.witnessSize=c.town.monsterSizeX(step.witness->identity);
            pending.inspectResidualAt=Clock::now()+std::chrono::seconds(2);
            pending.soundSequence=c.session.read().world.combatSequence;
        }
        pending.seconds=step.resultWaitSeconds?step.resultWaitSeconds:unsigned(eventPolicy.waitSeconds);
        pending.deadline=Clock::now()+std::chrono::seconds(pending.seconds);pending.expected=expected;
        questRun.worldWait=std::move(pending);note("world-wait-started",std::string(expected));
    }
    wait("quest-world-result",std::string(expected));
}
void DebugAi::Impl::observeWorldWait(Context &c) {
    if(!questRun.worldWait || cursor>=goals.size() || questRun.step>=goals[cursor].steps.size()) return;
    const auto &step=goals[cursor].steps[questRun.step];auto &pending=*questRun.worldWait;
    // Facts are checked even while input is suspended and before deadlines.
    if(worldResultReady(c,step)) {note("world-result-observed",pending.expected);questRun.worldWait.reset();return;}
    if(c.town.read().area && c.town.read().area!=pending.area) {
        note("world-wait-left-scene",pending.expected);questRun.worldWait.reset();return;
    }
    bool progress=false;
    if(pending.residualRange && step.stageSound && questRun.encounterWitness) {
        const auto &world=c.session.read().world;
        const auto [key,revision]=*questRun.encounterWitness;
        const auto witness=world.units.find(key);
        if(witness!=world.units.end() && witness->second.assignmentRevision==revision)
            for(const auto &event:world.combatEvents) if(event.sequence>pending.soundSequence && event.kind==OnlineCombatEvent::Kind::Sound &&
                event.packet==0x2c && event.source==key && event.auxiliary==*step.stageSound) {
                pending.inspectResidualAt=Clock::now();
                if(pending.lastProgress!="witness stage sound") {progress=true;pending.lastProgress="witness stage sound";}
            }
        pending.soundSequence=world.combatSequence;
    }
    if(step.action==QuestAction::Encounter && step.clearCombat) for(const auto &[key,unit]:c.session.read().world.units) {
        const auto identity=std::pair{key,unit.assignmentRevision};
        auto found=pending.enemies.find(identity);
        if(found==pending.enemies.end()) {
            if(!encounterEnemy(c,unit) || pending.enemies.size()>=256) continue;
            found=pending.enemies.emplace(identity,WorldWait::Enemy{}).first;
            progress=true;pending.lastProgress="new hostile assigned in encounter";
        }
        auto &enemy=found->second;
        if(!enemy.dead && onlineMonsterCorpse(unit)) {
            enemy.dead=true;progress=true;pending.lastProgress="actual encounter corpse";
        }
        if(unit.lifePercent) {
            const auto health=uint8_t(std::min(127,int(unit.lifeCarriesTriggerFlag?*unit.lifePercent&0x7f:*unit.lifePercent)));
            if(enemy.bestLife && health<*enemy.bestLife) {progress=true;pending.lastProgress="net encounter life decrease";}
            if(!enemy.bestLife || health<*enemy.bestLife) enemy.bestLife=health;
        }
    }
    if(progress) {++pending.progress;pending.deadline=Clock::now()+std::chrono::seconds(pending.seconds);}
    if(!inputBlocked && Clock::now()>=pending.deadline)
        throw std::runtime_error("World event made no relevant progress: "+pending.expected+"; no operation replayed");
}
bool DebugAi::Impl::tacticalPositionAllowed(const Context &c,Vec at) const {
    const auto &rooms=c.town.navigationRooms();
    if(std::none_of(rooms.begin(),rooms.end(),[&](const auto &room) {
        return room.loaded && room.level==c.town.read().area && at.x>=room.x && at.y>=room.y && at.x<room.x+room.width && at.y<room.y+room.height;
    })) return false;
    if(supplyStage==SupplyStage::None && questRun.encounterArea==c.town.read().area && questRun.encounterPosition) {
        const Vec anchor{float(questRun.encounterPosition->x),float(questRun.encounterPosition->y)};
        // When already displaced, permit movement back toward the scene.
        if((at-anchor).length()>std::max(float(encounterMovementRadius()),(c.position-anchor).length())) return false;
    }
    if(supplyStage==SupplyStage::None && questRun.worldWait && questRun.worldWait->area==c.town.read().area) {
        const auto anchor=questRun.worldWait->anchor;
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
    if(!map || !origin) return {};
    const Vec offset{float(origin->x),float(origin->y)},center{float(anchor.x),float(anchor.y)};
    const auto grid=c.town.navigationGrid(c.session.read());const auto hazards=visibleThreats(c);
    struct Candidate {OnlinePoint point;float score;};std::vector<Candidate> candidates;
    for(const int radius:{eventPolicy.standoffMin,(eventPolicy.standoffMin+eventPolicy.standoffMax)/2,eventPolicy.standoffMax})
        for(int direction=0;direction<16;++direction) {
            const float angle=float(direction)*6.28318530718f/16;
            const Vec at{std::floor(center.x+radius*std::cos(angle)),std::floor(center.y+radius*std::sin(angle))};
            if(at.x<0 || at.y<0 || at.x>65535 || at.y>65535 || !tacticalPositionAllowed(c,at) ||
                frontierCoolingDown({uint16_t(at.x),uint16_t(at.y)},2) ||
                !grid.nativeMovementSegment(at-offset+Vec{.5f,.5f},at-offset+Vec{.5f,.5f},playerMovement) ||
                !map->grid.collisionSegment(at-offset+Vec{.5f,.5f},center-offset+Vec{.5f,.5f},4) || threatClearance(hazards,at)<0) continue;
            candidates.push_back({{uint16_t(at.x),uint16_t(at.y)},(at-c.position).length()});
        }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto &a,const auto &b){return a.score<b.score;});
    for(const auto &candidate:candidates) {
        const Vec at{float(candidate.point.x),float(candidate.point.y)};
        const auto path=c.walk.navigationSearch().path(grid,c.position-offset+Vec{.5f,.5f},at-offset+Vec{.5f,.5f},false,playerMovement,true);
        if(!path.empty() && (path.back()-(at-offset+Vec{.5f,.5f})).length()<1 && tacticalPathSafe(c,path,offset,hazards)) return candidate.point;
    }
    return {};
}
} // namespace d2x
