#include "ai_impl.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
namespace {
std::string_view ownerName(DebugActionOwner owner) {
    static constexpr std::string_view names[]{"goal","navigation","door","waypoint","mobility","potion","supply","combat"};
    return names[unsigned(owner)];
}
}
DebugSubmitResult DebugAi::Impl::submitInput(Context &c,DebugActionOwner owner,std::string action,unsigned inputs,
        const std::function<bool()> &submit,const std::function<std::string()> &rejected,std::chrono::milliseconds budget) {
    const auto now=Clock::now();auto &pending=submissionWaits[owner];
    if(pending.action!=action) pending={std::move(action),{},inputs,0,decisions,now+budget,{}};
    pending.decision=decisions;
    if(!canInput(c,owner,inputs)) {
        pending.reason="Waiting for the current action's input ownership";
    } else if(now>=pending.retryAt) {
        ++pending.attempts;
        if(submit()) {submissionWaits.erase(owner);return DebugSubmitResult::Accepted;}
        pending.reason=rejected();
        if(pending.reason.empty()) pending.reason="Normal client input is temporarily unavailable";
        pending.retryAt=now+std::chrono::milliseconds(200);
    }
    if(now>=pending.deadline) return DebugSubmitResult::Expired;
    wait(pending.action,pending.reason);return DebugSubmitResult::Waiting;
}
bool DebugAi::Impl::submitItem(Context &c,const OnlineItemCommand &command,std::string action,DebugActionOwner owner) {
    const auto key=action+":"+std::to_string(unsigned(command.action))+":"+std::to_string(command.item)+":"+std::to_string(command.npc);
    const auto result=submitInput(c,owner,key,DebugItem|DebugInteraction,
        [&]{return c.inventory.submit(c.session,command);},[&]{return c.inventory.reason();});
    if(result==DebugSubmitResult::Expired) throw std::runtime_error("Normal item input was not accepted: "+action+"; "+submissionWaits.at(owner).reason);
    return result==DebugSubmitResult::Accepted;
}
bool DebugAi::Impl::submitCombat(Context &c,const OnlineCombatCommand &command,DebugActionOwner owner,std::string action,std::optional<Vec> at) {
    const auto key=action+":"+std::to_string(command.skill)+":"+std::to_string(unsigned(command.hand));
    return submitInput(c,owner,key,DebugSkill,[&]{return c.combat.submit(command,at);},
        [&]{return c.combat.reason();})==DebugSubmitResult::Accepted;
}
bool DebugAi::Impl::questWaitRequiresInput(const Context &c) const {
    if(!questRun.pending) return false;
    const auto &pending=*questRun.pending;
    if(pending.action==QuestPending::Action::Object && cursor<goals.size() && questRun.step<goals[cursor].steps.size() &&
        goals[cursor].steps[questRun.step].refreshFlags && !pending.statusRequested) return true;
    if(pending.action==QuestPending::Action::Introduction) return !pending.statusRequested;
    if(pending.action==QuestPending::Action::Object && pending.unit && cursor<goals.size() &&
        questRun.step<goals[cursor].steps.size() && c.session.read().world.npcConversation &&
        c.session.read().world.npcConversation->source==pending.unit->id) {
        const auto &step=goals[cursor].steps[questRun.step];const auto &conversation=*c.session.read().world.npcConversation;
        for(const auto &message:conversation.messages) if(!conversation.acknowledged.contains(message.stringId) &&
            std::find(step.topics.begin(),step.topics.end(),message.stringId)!=step.topics.end()) return true;
    }
    if(pending.action==QuestPending::Action::Object && pending.refreshIssued) return !pending.statusRequested;
    if(pending.action!=QuestPending::Action::Topic && !(pending.action==QuestPending::Action::Imbue && questRun.materialPhase==4)) return false;
    if(!pending.refreshIssued) return true;
    if(cursor<goals.size() && questRun.step<goals[cursor].steps.size() && goals[cursor].steps[questRun.step].optional) return false;
    return pending.unit && (!pending.statusRequested || c.session.read().world.npcRequested!=pending.unit->id);
}
std::vector<DebugActionContract> DebugAi::Impl::actionContracts(const Context &c) const {
    using Owner=DebugActionOwner;std::vector<DebugActionContract> result;
    const auto defense=debugActionBit(Owner::Combat)|debugActionBit(Owner::Potion);
    if(c.walk.active()) result.push_back({Owner::Navigation,"navigation",DebugMotion|DebugInteraction,~0u,
        "actual position / interaction","replan from current collision"});
    if(pendingDoor) result.push_back({Owner::Door,"door",DebugMotion|DebugInteraction,defense,
        "same assignment's non-neutral mode","fail without replay",doorDeadline});
    if(pendingWaypoint || closeWaypoint) result.push_back({Owner::Waypoint,"waypoint",DebugMotion|DebugInteraction|DebugMenu,defense,
        closeWaypoint?"close owned menu when input resumes":"mode / personal 63 unlock","fail without local unlock",waypointDeadline,bool(closeWaypoint)});
    if(mobilityTarget) result.push_back({Owner::Mobility,"mobility",DebugMotion|DebugSkill,0,
        "normal skill displacement","fall back to ordinary walking",mobilityDeadline});
    if(potionRequest) result.push_back({Owner::Potion,"potion",DebugItem,0,
        potionRequest->buying?"trade result and actual owned stock":"actual potion consumption","fail without repeating use / purchase",potionRequest->deadline});
    if(localPad && localPad->waiting) result.push_back({Owner::Goal,"local-pad-transfer",
        DebugMotion|DebugSkill|DebugInteraction|DebugMenu,0,"new own-unit relocation in the same level",
        "fail without replaying the submitted pad",localPad->deadline});
    if(questRun.pending || questTransaction(c)) {
        const bool atomic=questTransaction(c);
        const bool journal=questRun.pending && cursor<goals.size() && questRun.step<goals[cursor].steps.size() &&
            goals[cursor].steps[questRun.step].publishesTombSymbol;
        result.push_back({Owner::Goal,atomic?"quest-transaction":journal?"journal-reading":"quest-result",
            atomic?unsigned(DebugAllInputs):journal?unsigned(DebugMotion|DebugSkill|DebugInteraction|DebugMenu):unsigned(DebugItem|DebugInteraction|DebugMenu),0,
            questWaitRequiresInput(c)?"normal follow-up query input":"native quest / inventory result",
            "observe result; query only at the registered continuation; never replay service",
            questRun.pending?questRun.pending->deadline:Clock::time_point{},questRun.pending && questRun.pending->resultObserved});
    }
    if(travelPhase==TravelPhase::WaypointWait || (travelPhase==TravelPhase::CrossWait && legUnit) || portalIssued || supplyPortalUsed)
        result.push_back({supplyStage==SupplyStage::None?Owner::Goal:Owner::Supply,"area-transfer",
            DebugMotion|DebugSkill|DebugInteraction|DebugMenu,0,"actual destination scene","observe or fail; no blind transfer replay",
            supplyPortalUsed?supplyPortalDeadline:actionDeadline});
    if(shopIssued) result.push_back({Owner::Supply,"merchant",DebugItem|DebugInteraction|DebugMenu,0,
        "same interaction's shelf / transaction","fail without repeating shop / purchase",shopDeadline});
    if(supplyStage!=SupplyStage::None) result.push_back({Owner::Supply,"supply-detour",0,0,
        "supplies and original area","retain the original goal",supplyDeadline});
    return result;
}
bool DebugAi::Impl::canInput(const Context &c,DebugActionOwner owner,unsigned requested) const {
    if(inputBlocked || !active()) return false;
    const auto contracts=actionContracts(c);
    return std::all_of(contracts.begin(),contracts.end(),[&](const auto &entry){return entry.permits(owner,requested);});
}
void DebugAi::Impl::describeActions(const Context &c) {
    actionDiagnostics={{"inputBlocked",inputBlocked},{"executionSuspendedMs",executionClock.suspendedMs()},
        {"contracts",Json::array()},{"submissions",Json::array()},{"allowed",Json::object()}};
    for(const auto &[owner,pending]:submissionWaits) if(pending.decision+1>=decisions)
        actionDiagnostics["submissions"].push_back({{"owner",ownerName(owner)},{"action",pending.action},
            {"requestedInputs",pending.inputs},{"submitted",false},{"attempts",pending.attempts},{"reason",pending.reason},
            {"clock","execution"},{"waitRemainingMs",std::max<int64_t>(0,
                std::chrono::duration_cast<std::chrono::milliseconds>(pending.deadline-Clock::now()).count())}});
    for(const auto &entry:actionContracts(c)) actionDiagnostics["contracts"].push_back({
        {"owner",ownerName(entry.owner)},{"action",entry.action},{"inputs",entry.inputs},{"interruptors",entry.interruptors},
        {"waitingFor",entry.waitingFor},{"recovery",entry.recovery},{"resultObserved",entry.resultObserved},
        {"waitRemainingMs",entry.deadline==Clock::time_point{}?Json(nullptr):Json(std::max<int64_t>(0,
            std::chrono::duration_cast<std::chrono::milliseconds>(entry.deadline-Clock::now()).count()))},
        {"clock",entry.action=="supply-detour" || (entry.action=="merchant" && shelfObserved) ||
            (questWaitRequiresInput(c) && entry.owner==DebugActionOwner::Goal)?"execution":"real"}});
    for(const auto &[owner,inputs]:std::initializer_list<std::pair<DebugActionOwner,unsigned>>{
        {DebugActionOwner::Goal,DebugAllInputs},{DebugActionOwner::Combat,DebugMotion|DebugSkill},
        {DebugActionOwner::Potion,DebugItem},{DebugActionOwner::Mobility,DebugMotion|DebugSkill},
        {DebugActionOwner::Supply,DebugAllInputs},{DebugActionOwner::Waypoint,DebugMotion|DebugInteraction|DebugMenu}})
        actionDiagnostics["allowed"][ownerName(owner)]=canInput(c,owner,inputs);
}
void DebugAi::Impl::observeActions(Context &c) {
    const auto &view=c.session.read();const auto now=Clock::now();
    std::string failure;
    const auto observe=[&](auto action) {
        try {action();} catch(const std::exception &e) {if(failure.empty()) failure=e.what();}
    };
    observe([&]{if(cursor<goals.size()) observeChild(c,questLeaf(goals[cursor]));});
    observe([&]{observeLocalPad(c);});
    // Observe every independent result before considering any new input.
    // A potion or movement wait must not hide an already arrived door result.
    observe([&]{observePotion(c);});
    observe([&]{observeCombat(c);});
    if(pendingDoor) {
        const auto door=view.world.units.find(pendingDoor->first);
        if(door==view.world.units.end() || door->second.assignmentRevision!=pendingDoor->second) {
            note("door-assignment-withdrawn");pendingDoor.reset();
        } else if(door->second.mode && *door->second.mode!=0) {
            operatedDoors.insert(*pendingDoor);note("door-mode-observed",std::to_string(door->first.id));pendingDoor.reset();
        } else if(now>=doorDeadline) failure="Door interaction submitted but the assigned door stayed closed";
        else wait("door-wait","Waiting for the actual door mode; defensive inputs remain eligible");
    }
    if(pendingWaypoint) {
        const auto waypoint=view.world.units.find(pendingWaypoint->unit);
        const bool menu=view.world.waypointSource==pendingWaypoint->unit.id && view.world.waypointHistory &&
            std::any_of(c.town.read().waypoints.begin(),c.town.read().waypoints.end(),[&](const auto &entry) {
                return entry.level==pendingWaypoint->area && entry.unlocked;
            });
        if(waypoint==view.world.units.end() || waypoint->second.assignmentRevision!=pendingWaypoint->assignment) {
            note("waypoint-assignment-withdrawn");pendingWaypoint.reset();
        } else if(menu || (!pendingWaypoint->menu && waypoint->second.mode && *waypoint->second.mode!=0)) {
            activatedWaypointAreas.insert(pendingWaypoint->area);
            if(view.world.waypointSource==pendingWaypoint->unit.id) closeWaypoint=pendingWaypoint->unit.id;
            note("waypoint-activation-observed",std::to_string(waypoint->first.id));pendingWaypoint.reset();
        } else if(now>=waypointDeadline) failure="Waypoint activation submitted without its actual mode / unlock result";
        else wait("waypoint-activation-wait","Waiting for actual waypoint mode / personal unlock");
    }
    if(closeWaypoint && view.world.waypointSource!=closeWaypoint) closeWaypoint.reset();
    observe([&]{observeMobility(c);});
    observe([&]{observeQuestResult(c);});
    if(shopIssued && !potionRequest && !shelfObserved) {
        const bool ready=supplyNpc && view.world.shopSource==supplyNpc->id && !view.world.shopGamble &&
            (!view.world.itemRequest || view.world.itemRequest->state!=OnlineItemRequest::State::Pending);
        if(ready) {shelfObserved=true;shopDeadline=now+std::chrono::seconds(12);note("potion-shelf-observed");}
        else if(now>=shopDeadline) failure="Potion vendor shelf was not observed; no shop replay";
    }
    if(travelPhase==TravelPhase::WaypointWait || (travelPhase==TravelPhase::CrossWait && legUnit)) {
        if(legDestination && c.town.read().area==legDestination) {
            travelPhase=TravelPhase::Route;legDestination.reset();waypointDestination.reset();legUnit.reset();cancelWalk(c);
            note("travel-result-observed");
        } else if(now>=actionDeadline) failure="Submitted travel's destination was not observed";
    }
    if(supplyPortalUsed && supplyOrigin!=c.town.read().area && now>=supplyPortalDeadline)
        failure="Return portal was used but the original supply area was not observed";
    if(!failure.empty()) throw std::runtime_error(failure);
}
} // namespace d2x
