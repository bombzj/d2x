#include "ai_impl.hpp"
#include "content/world/object_mode.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace d2x {
bool DebugAi::Impl::questFactSatisfied(const Context &c,const Goal &goal,size_t node,const QuestFact &fact) const {
    using Kind=QuestFact::Kind;
    switch(fact.kind) {
    case Kind::RequirementObserved:
        return fact.value<goal.requirements.size() && goal.requirements[fact.value].retainObservation &&
            questState.questRun.observations.contains(fact.value);
    case Kind::Status:
        return c.observation.read().world.quests.statuses[questDefinition(fact.quest).nativeSlot]==fact.value;
    case Kind::MonsterObserved:
        return (!fact.area || c.town.read().area==fact.area) && find(c,Selector{1,fact.value,"observed quest monster"},true).has_value();
    case Kind::ObjectOpened:
        if(const auto level=fact.area?fact.area:goal.steps.at(node).area;level && c.town.read().area!=level) return false;
        return std::any_of(c.observation.read().world.units.begin(),c.observation.read().world.units.end(),[&](const auto &entry) {
            const auto &unit=entry.second;
            if(entry.first.type!=2 || unit.classId!=fact.value || !unit.mode) return false;
            if(fact.any) return *unit.mode!=0;
            const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
            const float age=unit.actionReceivedMilliseconds && now>=unit.actionReceivedMilliseconds?
                float(now-unit.actionReceivedMilliseconds)/1000.f:0.f;
            for(size_t row=0;row<definitions->objects.rows().size();++row)
                if(definitions->objects.number(row,"Id")==fact.value)
                    return objectDisplayMode(definitions->objects,row,*unit.mode,age)==2;
            return *unit.mode==2;
        });
    case Kind::Item:return owned(c,fact.item);
    case Kind::GroundItem:
        return groundItemSeen(c,fact.item);
    case Kind::Equipped:
        return std::any_of(c.observation.read().world.items.begin(),c.observation.read().world.items.end(),[&](const auto &entry) {
            const auto &item=entry.second;return item.code==fact.item && item.mode==1 && item.body==4 &&
                onlineItemOwnedBy(item,c.observation.read().load.playerUnitId);
        });
    case Kind::EquipmentRestored:
        return questEquipmentRestored(c);
    case Kind::BackpackItem: {
        const auto item=accessibleItem(c,fact.item);return item && item->mode==0 && item->page==1;
    }
    case Kind::Flags: {
        const auto value=questFlags(c,fact.quest);
        return value && !(*value&fact.clear) && (fact.any?bool(*value&fact.value):(*value&fact.value)==fact.value);
    }
    case Kind::Area:return c.town.read().area==fact.value;
    case Kind::Cinematic:return c.observation.read().world.cinematic==fact.value;
    case Kind::SummitPassage:
        if(!questState.summitTrialKills) return true;
        // Once a death was observed, the altar already produced this trial.
        return fact.value?questState.summitTrialKills->contains(fact.value):!questState.summitTrialKills->empty();
    case Kind::InitiallyRewarded:return goal.initiallyRewarded;
    case Kind::TrueTombArea:
        return questState.tombSymbol && *questState.tombSymbol<7 && c.town.read().area==definitions->area("Act 2 - Tomb Tal "+std::to_string(*questState.tombSymbol+1));
    case Kind::TombClue:return questState.tombSymbol && *questState.tombSymbol<7;
    case Kind::Waypoint:
        return navigation.knownWaypointAreas.contains(fact.value);
    case Kind::PortalObserved:
        return std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry) {
            return entry.interaction==OnlineMapInteraction::Portal && entry.destination==fact.value &&
                c.observation.read().world.units.contains(entry.unit);
        });
    case Kind::JournalRead:return questState.journalReadGame==c.observation.read().gameGeneration;
    case Kind::CairnOrder:return c.observation.read().world.quests.cainStones.has_value();
    case Kind::ActChange: {
        const auto &words=c.observation.read().world.quests.playerFlags;
        return words && fact.value<words->size() && ((*words)[fact.value]&1);
    }
    case Kind::Corpse: {
        const auto &step=goal.steps.at(node);if(!step.target) return false;
        if(const auto level=fact.area?fact.area:step.area;level && c.town.read().area!=level) return false;
        const auto unit=find(c,*step.target,true);return unit && onlineMonsterCorpse(*unit);
    }
    case Kind::BookUsed: {
        const auto value=questFlags(c,QuestId::RadamentsLair);
        return value && (*value&0x2003) && !(*value&0x20) && !owned(c,"ass");
    }
    }
    return false;
}
bool DebugAi::Impl::questRequirementSatisfied(const Context &c,const Goal &goal,size_t index) const {
    const auto &node=goal.requirements.at(index);
    if(node.retainObservation && questState.questRun.observations.contains(index)) return true;
    return std::any_of(node.alternatives.begin(),node.alternatives.end(),[&](const auto &all) {
        return !all.empty() && std::all_of(all.begin(),all.end(),[&](const auto &fact){return questFactSatisfied(c,goal,index,fact);});
    });
}
DebugAi::Impl::RouteDrop DebugAi::Impl::routeDropState(const Context &c,const Goal &goal) const {
    if(!goal.routeDrop || c.town.read().town || supply.supplyStage!=SupplyStage::None || !questState.questRun.initialized) return RouteDrop::None;
    const auto index=*goal.routeDrop;
    const auto &step=goal.steps.at(index);
    const auto &quest=questDefinition(step.stateQuest.value());
    const auto flags=questFlags(c,*step.stateQuest);
    const auto &view=c.observation.read();
    if(view.load.act!=quest.act || !flags || (*flags&0x4000) || view.world.quests.statuses[quest.nativeSlot]==12 ||
        questRequirementSatisfied(c,goal,index)) return RouteDrop::None;
    if(rememberedDrops.contains(step.item) || observedDrop(c,step.item)) return RouteDrop::Observed;
    const auto &notice=view.world.quests.statusChanges[quest.nativeSlot];
    if(step.dropNoticeStatus && view.world.quests.statuses[quest.nativeSlot]==step.dropNoticeStatus &&
        notice.receivedMilliseconds && notice.areaGeneration==view.world.areaGeneration) {
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        const auto grace=std::clamp<uint64_t>(2ULL*view.latencyMilliseconds.value_or(0)+300,600,1600);
        if(uint64_t(now)-notice.receivedMilliseconds<grace) return RouteDrop::Notice;
    }
    return RouteDrop::Seek;
}
void DebugAi::Impl::observeQuestFacts(const Context &c) {
    if(cursor>=goals.size() || goals[cursor].kind!=Kind::Quest || !questState.questRun.initialized) return;
    const auto &goal=goals[cursor];
    if(questState.summitTrialKills) {
        for(const auto &step:goal.steps) {
            if(step.stateQuest!=QuestId::RiteOfPassage || !step.target || area!=step.area) continue;
            if(step.action!=QuestAction::Operate && step.action!=QuestAction::Hunt) continue;
            const auto unit=find(c,*step.target,true);
            if(!unit) continue;
            if(step.action==QuestAction::Operate) {
                // A portal can reset the trial while the player stays in 120.
                // A5Q5 returns the altar to NEUTRAL and removes its guardians.
                if(unit->mode==0) {questState.summitTrialKills->clear();break;}
            } else if(onlineMonsterCorpse(*unit)) questState.summitTrialKills->insert(step.target->identity);
            else questState.summitTrialKills->erase(step.target->identity);
        }
    }
    for(size_t index=0;index<goal.requirements.size();++index)
        if(goal.requirements[index].retainObservation && questRequirementSatisfied(c,goal,index)) questState.questRun.observations.insert(index);
}
bool DebugAi::Impl::selectQuestRequirement(Context &c,const Goal &goal) {
    if(goal.requirements.size()!=goal.steps.size() || goal.requirements.size()>64)
        throw std::runtime_error("Quest dependency graph has an invalid action boundary");
    std::vector<uint8_t> satisfied(goal.requirements.size()),state(goal.requirements.size());
    // Observe transient corpses/portals even while a different node owns an
    // accepted request. Retaining that request must not discard new facts.
    for(size_t i=0;i<goal.requirements.size();++i) {
        const auto &step=goal.steps[i];
        if(step.journalTalkStatus && c.control.quests().statuses[questDefinition(*step.stateQuest).nativeSlot]!=step.journalTalkStatus) {
            // Only the current journal instruction can request this visit.
            questState.questRun.observations.erase(i);
            questState.questRun.journalReceipts.erase(i);
            satisfied[i]=true;
            continue;
        }
        if(step.journalTalkStatus && c.town.read().area==step.area && questState.questRun.observations.contains(i)) {
            const auto npc=find(c,*step.target);
            if(npc && c.observation.read().world.questAlerts.contains(npc->key) &&
                questState.questRun.journalReceipts[i].unproductiveFlags!=questFlags(c,*step.stateQuest))
                questState.questRun.observations.erase(i);
        }
        satisfied[i]=questRequirementSatisfied(c,goal,i);
        if(satisfied[i] && goal.requirements[i].retainObservation) questState.questRun.observations.insert(i);
    }
    // Never replace a submitted operation with a newly attractive branch.
    // Its own handler must first consume the actual result or report unknown.
    const bool waiting=questState.questRun.journalVisit || questState.questRun.pending || questState.questRun.recoveringItem || questTransaction(c) || objectIssued || pickup || portalIssued || supply.potionRequest ||
        navigation.pendingDoor || navigation.pendingWaypoint || navigation.closeWaypoint || navigation.mobilityTarget || (navigation.localPad && navigation.localPad->waiting) ||
        navigation.travelPhase==TravelPhase::WaypointWait || (navigation.travelPhase==TravelPhase::CrossWait && navigation.legUnit);
    if(questState.questRun.selected && waiting) {
        questState.questPlanDiagnostics["decision"]="retain-submitted-action";return true;
    }
    std::vector<size_t> ready;
    std::function<void(size_t)> visit=[&](size_t index) {
        if(index>=state.size()) throw std::runtime_error("Quest dependency refers to a missing condition");
        if(satisfied[index] || state[index]==2) return;
        if(state[index]==1) throw std::runtime_error("Quest dependency graph contains a cycle");
        state[index]=1;bool dependencies=true;
        for(const auto dependency:goal.requirements[index].needs) {
            if(dependency>=state.size()) throw std::runtime_error("Quest dependency is outside the graph");
            if(!satisfied[dependency]) {dependencies=false;visit(dependency);}
        }
        if(dependencies) ready.push_back(index);
        state[index]=2;
    };
    bool complete=true;
    for(const auto root:goal.roots) {
        if(root>=satisfied.size()) throw std::runtime_error("Quest graph has an invalid root");
        complete=complete && satisfied[root];visit(root);
    }
    // An observed drop may interrupt an approach. Submitted operations were
    // retained above, and a timed encounter must keep its scene and deadline.
    const bool finishSequence=questState.questRun.selected &&
        ((questState.questRun.materialPhase && goal.steps.at(questState.questRun.step).action!=QuestAction::SearchDrop) ||
         (goal.steps.at(questState.questRun.step).witness && questState.questRun.sweepReady!=Clock::time_point{}));
    const auto drop=finishSequence || questState.questRun.worldWait || questState.questRun.encounterAnchored?RouteDrop::None:routeDropState(c,goal);
    if(drop==RouteDrop::Notice && !waiting && !c.observation.read().world.npcRequested && !c.observation.read().world.waypointRequested) {
        releaseCombat(c);cancelWalk(c);wait("quest-drop-notice","Quest update received; briefly observing for an assigned drop");
        return false;
    }
    const bool collectDrop=drop==RouteDrop::Observed;
    if(collectDrop) {visit(*goal.routeDrop);complete=false;}
    questState.questPlanDiagnostics={{"decision","evaluate-missing-conditions"},{"nodes",Json::array()},
        {"roots",goal.roots},{"ready",Json::array()},{"selected",nullptr}};
    for(size_t i=0;i<satisfied.size();++i) questState.questPlanDiagnostics["nodes"].push_back({
        {"index",i},{"name",goal.requirements[i].name},{"satisfied",bool(satisfied[i])},
        {"needed",state[i]!=0},{"needs",goal.requirements[i].needs},
        {"observationRetained",questState.questRun.observations.contains(i)}});
    if(complete) {
        releaseCombat(c);cancelWalk(c);questState.questRun.selected=false;questState.questRun.step=goal.steps.size();
        if(questFinished(c,goal)) advance(c,"Native facts satisfy the quest condition graph: "+goal.quest);
        else wait("quest-result","Graph conditions satisfied; waiting for actual final personal flags");
        return false;
    }
    if(ready.empty()) throw std::runtime_error("Unsatisfied quest conditions have no ready native action");
    if(questState.questRun.selected && !satisfied[questState.questRun.step] &&
        (questState.questRun.worldWait || (navigation.activeTargetSearch && std::find(ready.begin(),ready.end(),questState.questRun.step)!=ready.end()))) {
        questState.questPlanDiagnostics["decision"]="retain-observed-event-scene";
        questState.questPlanDiagnostics["selected"]={{"index",questState.questRun.step},{"name",goal.requirements[questState.questRun.step].name}};
        return true;
    }
    const auto operationHere=[&](const QuestStep &step) {
        if(step.action==QuestAction::Portal && step.sourceArea==c.town.read().area) return true;
        if(step.trueTomb) return questState.tombSymbol && *questState.tombSymbol<7 &&
            c.town.read().area==definitions->area("Act 2 - Tomb Tal "+std::to_string(*questState.tombSymbol+1));
        return !step.area || step.area==c.town.read().area;
    };
    const bool retainWalk=questState.questRun.selected && c.walk.active() && std::find(ready.begin(),ready.end(),questState.questRun.step)!=ready.end() &&
        (operationHere(goal.steps[questState.questRun.step]) || std::none_of(ready.begin(),ready.end(),[&](auto i){return operationHere(goal.steps[i]);}));
    float best=std::numeric_limits<float>::max();size_t chosen=ready.front();
    for(const auto index:ready) {
        const auto &step=goal.steps[index];const auto &node=goal.requirements[index];
        auto destination=step.area;
        if(step.trueTomb && questState.tombSymbol && *questState.tombSymbol<7)
            destination=definitions->area("Act 2 - Tomb Tal "+std::to_string(*questState.tombSymbol+1));
        const bool townAction=destination && destination==c.observation.read().load.townArea;
        const bool here=operationHere(step);
        // Independent field requirements are gathered before an otherwise
        // optional return. Once in town, finish ready native topics in the
        // verified Cain priority order before another field departure.
        float score=here?0.f:townAction && !c.town.read().town?10000.f:100.f;
        if(here && !townAction && step.target) {
            const auto assigned=find(c,*step.target,true);
            const auto point=assigned?assigned->position:c.town.knownTargetPosition(*area,step.target->type,step.target->identity);
            // Prefer an observed ready operation over searching the same
            // level for another one. This is a debug travel estimate; it
            // neither reveals a preset location nor proves reachability.
            score+=point?std::min(31.f,(Vec{float(point->x),float(point->y)}-c.position).length()/16.f):32.f;
        }
        if(townAction && c.town.read().town) score+=float(node.townOrder)*.01f;
        if(ready.size()>1 && !retainWalk && !here && !townAction && destination && area) {
            const auto path=route(c,*area,*destination);const float cost=routeCost(c,path);
            score+=std::isfinite(cost)?std::min(cost,5000.f):5000.f;
        }
        if(questState.questRun.selected && questState.questRun.step==index) score-=1.f; // Stable approach until new facts make another branch useful.
        questState.questPlanDiagnostics["ready"].push_back({{"index",index},{"name",node.name},{"score",score},
            {"area",destination?Json(*destination):Json(nullptr)},{"townOperation",townAction}});
        if(score<best) {best=score;chosen=index;}
    }
    if(collectDrop) {chosen=*goal.routeDrop;questState.questPlanDiagnostics["decision"]="collect-observed-route-drop";}
    else if(retainWalk) {chosen=questState.questRun.step;questState.questPlanDiagnostics["decision"]="retain-active-condition-approach";}
    questState.questPlanDiagnostics["selected"]={{"index",chosen},{"name",goal.requirements[chosen].name}};
    if(!questState.questRun.selected || questState.questRun.step!=chosen) {
        releaseCombat(c);cancelWalk(c);
        if(c.observation.read().world.npcRequested) c.control.closeNpc();
        auto observations=std::move(questState.questRun.observations);
        auto receipts=std::move(questState.questRun.journalReceipts);
        resetStep();questState.questRun={};questState.questRun.initialized=true;questState.questRun.selected=true;questState.questRun.step=chosen;
        questState.questRun.observations=std::move(observations);
        questState.questRun.journalReceipts=std::move(receipts);
        note("quest-condition-selected",goal.requirements[chosen].name);
    }
    return true;
}
} // namespace d2x
