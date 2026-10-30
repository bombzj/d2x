#include "ai_impl.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace d2x {
bool DebugAi::Impl::questFactSatisfied(const Context &c,const Goal &goal,size_t node,const QuestFact &fact) const {
    using Kind=QuestFact::Kind;
    switch(fact.kind) {
    case Kind::ObjectOpened:
        if(goal.steps.at(node).area && c.town.read().area!=goal.steps.at(node).area) return false;
        return std::any_of(c.session.read().world.units.begin(),c.session.read().world.units.end(),[&](const auto &entry) {
            return entry.first.type==2 && entry.second.classId==fact.value && entry.second.mode==2;
        });
    case Kind::Item:return owned(c,fact.item);
    case Kind::Equipped:
        return std::any_of(c.session.read().world.items.begin(),c.session.read().world.items.end(),[&](const auto &entry) {
            const auto &item=entry.second;return item.code==fact.item && item.mode==1 && item.body==4 &&
                onlineItemOwnedBy(item,c.session.read().load.playerUnitId);
        });
    case Kind::EquipmentRestored: {
        if(!questWeaponRestore) return true;
        const auto item=c.session.read().world.items.find(*questWeaponRestore);
        return item!=c.session.read().world.items.end() && item->second.mode==1 && item->second.body==4 &&
            onlineItemOwnedBy(item->second,c.session.read().load.playerUnitId);
    }
    case Kind::BackpackItem: {
        const auto item=accessibleItem(c,fact.item);return item && item->mode==0 && item->page==1;
    }
    case Kind::Flags: {
        const auto value=questFlags(c,fact.quest);
        return value && !(*value&fact.clear) && (fact.any?bool(*value&fact.value):(*value&fact.value)==fact.value);
    }
    case Kind::Area:return c.town.read().area==fact.value;
    case Kind::TrueTombArea:
        return tombSymbol && *tombSymbol<7 && c.town.read().area==definitions->area("Act 2 - Tomb Tal "+std::to_string(*tombSymbol+1));
    case Kind::TombClue:return tombSymbol && *tombSymbol<7;
    case Kind::Waypoint:
        return c.session.read().world.waypointHistory && std::any_of(c.town.read().waypoints.begin(),c.town.read().waypoints.end(),
            [&](const auto &entry){return entry.level==fact.value && entry.unlocked;});
    case Kind::PortalObserved:
        return std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry) {
            return entry.interaction==OnlineMapInteraction::Portal && entry.destination==fact.value &&
                c.session.read().world.units.contains(entry.unit);
        });
    case Kind::JournalRead:return journalReadGame==c.session.read().gameGeneration;
    case Kind::ActChange: {
        const auto &words=c.session.read().world.quests.playerFlags;
        return words && fact.value<words->size() && ((*words)[fact.value]&1);
    }
    case Kind::Corpse: {
        const auto &step=goal.steps.at(node);if(!step.target) return false;
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
    if(node.retainObservation && questRun.observations.contains(index)) return true;
    return std::any_of(node.alternatives.begin(),node.alternatives.end(),[&](const auto &all) {
        return !all.empty() && std::all_of(all.begin(),all.end(),[&](const auto &fact){return questFactSatisfied(c,goal,index,fact);});
    });
}
bool DebugAi::Impl::selectQuestRequirement(Context &c,const Goal &goal) {
    if(goal.requirements.size()!=goal.steps.size() || goal.requirements.size()>64)
        throw std::runtime_error("Quest dependency graph has an invalid action boundary");
    // Never replace a submitted operation with a newly attractive branch.
    // Its own handler must first consume the actual result or report unknown.
    const bool waiting=questRun.pending || questTransaction(c) || objectIssued || pickupIssued || portalIssued || potionRequest ||
        pendingDoor || pendingWaypoint || closeWaypoint || mobilityTarget || (localPad && localPad->waiting) ||
        travelPhase==TravelPhase::WaypointWait || (travelPhase==TravelPhase::CrossWait && legUnit);
    if(questRun.selected && waiting) {
        questPlanDiagnostics["decision"]="retain-submitted-action";return true;
    }
    std::vector<uint8_t> satisfied(goal.requirements.size()),state(goal.requirements.size());
    for(size_t i=0;i<goal.requirements.size();++i) {
        satisfied[i]=questRequirementSatisfied(c,goal,i);
        if(satisfied[i] && goal.requirements[i].retainObservation) questRun.observations.insert(i);
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
    questPlanDiagnostics={{"decision","evaluate-missing-conditions"},{"nodes",Json::array()},
        {"roots",goal.roots},{"ready",Json::array()},{"selected",nullptr}};
    for(size_t i=0;i<satisfied.size();++i) questPlanDiagnostics["nodes"].push_back({
        {"index",i},{"name",goal.requirements[i].name},{"satisfied",bool(satisfied[i])},
        {"needed",state[i]!=0},{"needs",goal.requirements[i].needs},
        {"observationRetained",questRun.observations.contains(i)}});
    if(complete) {
        releaseCombat(c);cancelWalk(c);questRun.selected=false;questRun.step=goal.steps.size();
        if(questFinished(c,goal)) advance(c,"Native facts satisfy the quest condition graph: "+goal.quest);
        else wait("quest-result","Graph conditions satisfied; waiting for actual final personal flags");
        return false;
    }
    if(ready.empty()) throw std::runtime_error("Unsatisfied quest conditions have no ready native action");
    const auto operationHere=[&](const QuestStep &step) {
        if(step.action==QuestAction::Portal && step.sourceArea==c.town.read().area) return true;
        if(step.trueTomb) return tombSymbol && *tombSymbol<7 &&
            c.town.read().area==definitions->area("Act 2 - Tomb Tal "+std::to_string(*tombSymbol+1));
        return !step.area || step.area==c.town.read().area;
    };
    const bool retainWalk=questRun.selected && c.walk.active() && std::find(ready.begin(),ready.end(),questRun.step)!=ready.end() &&
        (operationHere(goal.steps[questRun.step]) || std::none_of(ready.begin(),ready.end(),[&](auto i){return operationHere(goal.steps[i]);}));
    float best=std::numeric_limits<float>::max();size_t chosen=ready.front();
    for(const auto index:ready) {
        const auto &step=goal.steps[index];const auto &node=goal.requirements[index];
        auto destination=step.area;
        if(step.trueTomb && tombSymbol && *tombSymbol<7)
            destination=definitions->area("Act 2 - Tomb Tal "+std::to_string(*tombSymbol+1));
        const bool townAction=destination && destination==c.session.read().load.townArea;
        const bool here=operationHere(step);
        // Independent field requirements are gathered before an otherwise
        // optional return. Once in town, finish ready native topics in the
        // verified Cain priority order before another field departure.
        float score=here?0.f:townAction && !c.town.read().town?10000.f:100.f;
        if(townAction && c.town.read().town) score+=float(node.townOrder)*.01f;
        if(!here && !townAction && destination && area) {
            const auto path=route(c,*area,*destination);const float cost=routeCost(c,path);
            score+=std::isfinite(cost)?std::min(cost,5000.f):5000.f;
        }
        if(questRun.selected && questRun.step==index) score-=1.f; // Stable approach until new facts make another branch useful.
        questPlanDiagnostics["ready"].push_back({{"index",index},{"name",node.name},{"score",score},
            {"area",destination?Json(*destination):Json(nullptr)},{"townOperation",townAction}});
        if(score<best) {best=score;chosen=index;}
    }
    if(retainWalk) {chosen=questRun.step;questPlanDiagnostics["decision"]="retain-active-condition-approach";}
    questPlanDiagnostics["selected"]={{"index",chosen},{"name",goal.requirements[chosen].name}};
    if(!questRun.selected || questRun.step!=chosen) {
        releaseCombat(c);cancelWalk(c);
        if(c.session.read().world.npcRequested) c.session.close_npc();
        auto observations=std::move(questRun.observations);
        resetStep();questRun={};questRun.initialized=true;questRun.selected=true;questRun.step=chosen;
        questRun.observations=std::move(observations);
        note("quest-condition-selected",goal.requirements[chosen].name);
    }
    return true;
}
} // namespace d2x
