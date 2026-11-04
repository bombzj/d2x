#include "ai_impl.hpp"
#include "contracts/online_npc_speech.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
bool DebugAi::Impl::decisionDue(const Context &c,Clock::time_point now) {
    const auto &view=c.session.read();const auto &world=view.world;
    using Mode=DebugDecisionCadence::Mode;
    const bool missiles=c.supplies && std::any_of(c.supplies->missiles.begin(),c.supplies->missiles.end(),[](const auto &m){return m.hostile && m.age>=0 && m.remaining>0;});
    const bool closeThreat=!actionRecoveryBusy && !c.town.read().town && std::any_of(world.units.begin(),world.units.end(),[&](const auto &entry) {
        const auto &unit=entry.second;
        return entry.first.type==1 && unit.position && !onlineMonsterCorpse(unit) &&
            (Vec{float(unit.position->x),float(unit.position->y)}-c.position).length()<=policy.radius && c.combat.hostileSource(unit);
    });
    const bool lostLife=decisionLife && world.life && *world.life<*decisionLife;
    const bool urgent=lostLife || onlinePlayerDead(world) || game!=view.gameGeneration || areaGeneration!=world.areaGeneration;
    auto mode=inputBlocked?Mode::Suspended:actionRecoveryBusy || missiles || hazardDodge?Mode::Reaction:
        combatTarget || closeThreat?Mode::Combat:c.moving || c.walk.active()?Mode::Navigation:
        questRun.pending || questRun.worldWait || potionRequest || mobilityTarget || pendingDoor || pendingWaypoint?Mode::Waiting:Mode::Idle;
    std::array<uint64_t,24> signals{view.gameGeneration,world.areaGeneration,uint64_t(view.stage),uint64_t(inputBlocked),
        uint64_t(world.life.value_or(0))|(uint64_t(world.mana.value_or(0))<<16),world.quests.revision,world.itemRevision,
        world.combatRequest?world.combatRequest->revision:0,world.interactionGeneration,world.cinematicRevision,
        world.units.size(),world.rooms.size(),0,0,
        uint64_t(world.waypointSource.value_or(0))|(uint64_t(world.npcConversation?world.npcConversation->source:0)<<32),
        uint64_t(c.moving)|(uint64_t(c.walk.active())<<1)|(uint64_t(actionRecoveryBusy)<<2)};
    signals[16]=world.npcConversation?world.npcConversation->revision:0;
    signals[17]=world.itemRequest?(world.itemRequest->sequence<<4)|uint64_t(world.itemRequest->state):0;
    signals[18]=world.storage.revision;signals[19]=world.tradeResult?world.tradeResult->revision:0;
    signals[20]=uint64_t(world.leftSkill?world.leftSkill->skill:UINT16_MAX)|(uint64_t(world.rightSkill?world.rightSkill->skill:UINT16_MAX)<<16);
    if(pendingDoor) if(const auto door=world.units.find(pendingDoor->first);door!=world.units.end()) signals[21]=door->second.mode.value_or(0)+1;
    signals[22]=world.staffRevision;signals[23]=uint64_t(bool(view.error))|(uint64_t(closeThreat)<<1)|(uint64_t(missiles)<<2);
    if(const auto player=world.units.find({0,view.load.playerUnitId.value_or(0)});player!=world.units.end()) signals[12]=player->second.positionDiscontinuity;
    if(combatTarget) if(const auto enemy=world.units.find(*combatTarget);enemy!=world.units.end())
        signals[13]=uint64_t(enemy->second.mode.value_or(0))|(uint64_t(enemy->second.lifePercent.value_or(0))<<8)|(enemy->second.hitRevision<<16);
    auto deadline=supplyStage==SupplyStage::None?goalDeadline:supplyDeadline;
    const auto include=[&](Clock::time_point at) {if(at!=Clock::time_point{} && at>now && (deadline==Clock::time_point{} || at<deadline)) deadline=at;};
    if(questRun.pending && !questRun.pending->resultObserved) include(questRun.pending->deadline);
    if(questRun.worldWait) include(questRun.worldWait->deadline);
    for(const auto &[kind,pending]:passageWaits) {(void)kind;include(pending.retryAt);include(pending.deadline);}
    if(pickup && !pickup->observed) include(pickup->deadline);
    if(potionRequest) include(potionRequest->deadline);
    if(mobilityTarget) include(mobilityDeadline);
    if(pendingDoor) include(doorDeadline);
    if(pendingWaypoint) include(waypointDeadline);
    if(!inputBlocked) {include(combatRun.nextCast);include(nextPotion);}
    if(!cadence.due(now,mode,signals,urgent,deadline)) return false;
    decisionLife=world.life;return true;
}
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
    if(now>=pending.deadline) return DebugSubmitResult::Expired;
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
    const unsigned inputs=owner==DebugActionOwner::Potion && command.action==OnlineItemAction::Use?
        unsigned(DebugItem):unsigned(DebugItem|DebugInteraction);
    const auto result=submitInput(c,owner,key,inputs,
        [&]{return c.inventory.submit(c.session,command);},[&]{return c.inventory.reason();});
    if(result==DebugSubmitResult::Expired) throw std::runtime_error("Normal item input was not accepted: "+action+"; "+submissionWaits.at(owner).reason);
    return result==DebugSubmitResult::Accepted;
}
bool DebugAi::Impl::prepareNpcService(Context &c, OnlineUnitKey npc, DebugActionOwner owner) {
    const auto &conversation = c.session.read().world.npcConversation;
    if (!conversation || conversation->type != npc.type || conversation->source != npc.id) {
        wait("npc-service-open","Waiting for the current NPC service conversation");
        walk(c,{{"goal","unit"},{"unitType",npc.type},{"unitId",npc.id},{"interact",true}});
        return false;
    }
    const auto automatic = nextAutomaticNpcSpeech(*conversation);
    if (!automatic) return true;
    const auto result = submitInput(c, owner, "npc-service-speech", DebugInteraction | DebugMenu,
        [&]{return c.control.acknowledgeNpc(*automatic);},
        [&]{return c.session.read().error ? c.session.read().error->message : std::string{};});
    if (result == DebugSubmitResult::Expired) throw std::runtime_error("Unable to finish the NPC's automatic speech");
    if (result == DebugSubmitResult::Accepted) {
        cancelWalk(c);
        wait("npc-service-speech","Automatic speech acknowledged; observing the resulting conversation");
    }
    return false;
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
        goals[cursor].steps[questRun.step].refreshFlags && !pending.statusRequested &&
        (goals[cursor].steps[questRun.step].topics.empty() || pending.refreshIssued)) return true;
    if(pending.action==QuestPending::Action::Introduction) return !pending.statusRequested;
    if(pending.action==QuestPending::Action::Object && pending.unit && cursor<goals.size() &&
        questRun.step<goals[cursor].steps.size() && c.session.read().world.npcConversation &&
        c.session.read().world.npcConversation->source==pending.unit->id) {
        const auto &step=goals[cursor].steps[questRun.step];const auto &conversation=*c.session.read().world.npcConversation;
        for(const auto &message:conversation.messages) if(!conversation.acknowledged.contains(message.stringId) &&
            std::find(step.topics.begin(),step.topics.end(),message.stringId)!=step.topics.end()) return true;
    }
    if(pending.action==QuestPending::Action::Object && pending.refreshIssued)
        return !pending.statusRequested && cursor<goals.size() && questRun.step<goals[cursor].steps.size() &&
            goals[cursor].steps[questRun.step].publishesTombSymbol;
    if(pending.action!=QuestPending::Action::Topic && !((pending.action==QuestPending::Action::Imbue ||
        pending.action==QuestPending::Action::Service) && questRun.materialPhase==4)) return false;
    if(!pending.refreshIssued) return true;
    if(cursor<goals.size() && questRun.step<goals[cursor].steps.size() && goals[cursor].steps[questRun.step].optional) return false;
    return !pending.statusRequested;
}
void DebugAi::Impl::execute(Context &c,const Goal &goal) {
    if(continuation==Continuation::WorldResult) continuation=Continuation::None;
    executeGoal(c,goal);
    if(continuation==Continuation::WorldResult && canInput(c,DebugActionOwner::Combat,DebugMotion|DebugSkill))
        combat(c,goal,true);
}
// Re-enter the selected step through its normal postcondition. A reaction
// can replace a walk, but it cannot complete the parent or repeat its input.
bool DebugAi::Impl::resumeAction(Context &c,const Goal &goal,const Goal &leaf) {
    if(supplyStage!=SupplyStage::None) return false;
    const bool search=goal.kind==Kind::Quest && questRun.step<goal.steps.size() &&
        goal.steps[questRun.step].action==QuestAction::SearchDrop;
    if((leaf.kind==Kind::Acquire || search) && (pickup || rememberedDrops.contains(leaf.item)))
        continuation=Continuation::KnownDrop;
    else if(continuation==Continuation::KnownDrop) continuation=Continuation::None;
    if(continuation==Continuation::None) return false;
    using Owner=DebugActionOwner;
    if(!canInput(c,Owner::Goal,DebugAllInputs)) return true;
    const bool collecting=continuation==Continuation::KnownDrop;
    if(!pickup && (collecting || questRun.worldWait) &&
        canInput(c,Owner::Supply,DebugAllInputs) && autoSupply(c,leaf)) return true;
    if(collecting && !pickup && rememberedDrops.contains(leaf.item) &&
        canInput(c,Owner::Combat,DebugMotion|DebugSkill) && combat(c,leaf,true)) return true;
    if(collecting) releaseCombat(c);
    execute(c,goal);
    if(active() && !pickup && canInput(c,Owner::Mobility,DebugMotion|DebugSkill)) mobility(c);
    return true;
}
void DebugAi::Impl::observePickup(const Context &c) {
    if(!pickup) return;
    if(pickup->observed) {
        if(!owned(c,pickup->code)) {note("pickup-ownership-lost",pickup->code);pickup.reset();}
        return;
    }
    if(owned(c,pickup->code)) {
        pickup->observed=true;note("pickup-ownership-observed",pickup->code);return;
    }
    const auto &request=c.session.read().world.itemRequest;
    if(request && (request->sequence!=pickup->sequence || request->command.item!=pickup->item ||
        request->state==OnlineItemRequest::State::Rejected || request->state==OnlineItemRequest::State::Interrupted ||
        request->state==OnlineItemRequest::State::TimedOut))
        throw std::runtime_error("Pickup request ended without observed ownership: "+pickup->code);
    if(Clock::now()>=pickup->deadline)
        throw std::runtime_error("No ownership observed for submitted pickup: "+pickup->code);
}
std::vector<DebugActionContract> DebugAi::Impl::actionContracts(const Context &c) const {
    using Owner=DebugActionOwner;std::vector<DebugActionContract> result;
    const auto defense=debugActionBit(Owner::Combat)|debugActionBit(Owner::Potion);
    if(c.walk.active()) {
        const bool recovering=c.walk.recoveringMovement();
        result.push_back({Owner::Navigation,recovering?"walking-recovery":"navigation",
            recovering?unsigned(DebugMotion|DebugSkill|DebugInteraction|DebugMenu):unsigned(DebugMotion|DebugInteraction),
            recovering?debugActionBit(Owner::Potion):~0u,
            recovering?"rejoin observed feet through ordinary walking":"actual position / interaction",
            "replan from current collision"});
    }
    if(pendingDoor) result.push_back({Owner::Door,"door",DebugMotion|DebugInteraction,defense|debugActionBit(Owner::Goal),
        "same assignment's non-neutral mode","normal interaction retry every second after initial wait",doorDeadline});
    if(pendingWaypoint || closeWaypoint) result.push_back({Owner::Waypoint,"waypoint",DebugMotion|DebugInteraction|DebugMenu,defense,
        closeWaypoint?"close owned menu when input resumes":"mode / personal 63 unlock","defer optional activation without local unlock",waypointDeadline,bool(closeWaypoint)});
    if(mobilityTarget) result.push_back({Owner::Mobility,"mobility",DebugMotion|DebugSkill,0,
        "normal skill displacement","fall back to ordinary walking",mobilityDeadline});
    if(pickup) result.push_back({Owner::Goal,"pickup-result",DebugItem|DebugInteraction,0,
        "personal item ownership","observe without replaying pickup",pickup->deadline,pickup->observed});
    if(potionRequest) result.push_back({Owner::Potion,"potion",DebugItem,0,
        potionRequest->buying?"trade result and actual owned stock":"actual potion consumption","fail without repeating use / purchase",potionRequest->deadline});
    if(localPad && localPad->waiting) result.push_back({Owner::Goal,"local-pad-transfer",
        DebugMotion|DebugSkill|DebugInteraction|DebugMenu,0,"new own-unit relocation in the same level",
        "fail without replaying the submitted pad",localPad->deadline});
    if(questRun.pending || questTransaction(c)) {
        const bool atomic=questTransaction(c);
        const bool journal=questRun.pending && cursor<goals.size() && questRun.step<goals[cursor].steps.size() &&
            goals[cursor].steps[questRun.step].publishesTombSymbol;
        const bool portal=questRun.pending && questRun.pending->action==QuestPending::Action::Portal;
        result.push_back({Owner::Goal,atomic?"quest-transaction":journal?"journal-reading":portal?"quest-portal-transfer":"quest-result",
            atomic?unsigned(DebugAllInputs):(journal || portal)?unsigned(DebugMotion|DebugSkill|DebugInteraction|DebugMenu):unsigned(DebugItem|DebugInteraction|DebugMenu),0,
            questWaitRequiresInput(c)?"normal follow-up query input":"native quest / inventory result",
            portal?"bounded normal portal retry in the confirmed source scene":"observe result; query only at the registered continuation; never replay service",
            questRun.pending?questRun.pending->deadline:Clock::time_point{},questRun.pending && questRun.pending->resultObserved});
    }
    if(questRun.worldWait) result.push_back({Owner::Goal,"world-result",0,0,
        "relevant scene fact / encounter progress","retain scene; no interaction replay",questRun.worldWait->deadline});
    if(travelPhase==TravelPhase::WaypointWait || (travelPhase==TravelPhase::CrossWait && legUnit) ||
        portalUsed || (portalIssued && c.session.read().world.townPortalPending) || supplyPortalUsed)
        result.push_back({supplyStage==SupplyStage::None?Owner::Goal:Owner::Supply,"area-transfer",
            DebugMotion|DebugSkill|DebugInteraction|DebugMenu,0,"actual destination scene","shared initial wait / one-second source-scene retry",
            supplyPortalUsed?supplyPortalDeadline:actionDeadline});
    if(shopIssued) result.push_back({Owner::Supply,"merchant",DebugItem|DebugInteraction|DebugMenu,0,
        "same interaction's shelf / transaction","reopen unconfirmed shelf once; never repeat a pending purchase",shopDeadline});
    if(healingIssued) result.push_back({Owner::Supply,"town-healing",DebugMotion|DebugInteraction|DebugMenu,0,
        "actual life restored by healer","fail without replaying conversation",shopDeadline,healingObserved});
    if(supplyStage!=SupplyStage::None) result.push_back({Owner::Supply,"supply-detour",0,0,
        "supplies and original area","retain the original goal",supplyDeadline});
    return result;
}
bool DebugAi::Impl::canInput(const Context &c,DebugActionOwner owner,unsigned requested) const {
    if(inputBlocked || !active()) return false;
    if(actionRecoveryBusy && owner!=DebugActionOwner::Potion) return false;
    const auto contracts=actionContracts(c);
    return std::all_of(contracts.begin(),contracts.end(),[&](const auto &entry){
        if(passageDefending && owner==DebugActionOwner::Combat &&
            (entry.action=="door" || entry.action=="quest-portal-transfer" ||
             (entry.action=="area-transfer" && travelPhase!=TravelPhase::WaypointWait))) return true;
        return entry.permits(owner,requested);
    });
}
void DebugAi::Impl::describeActions(const Context &c) {
    actionDiagnostics={{"inputBlocked",inputBlocked},{"actionRecoveryBusy",actionRecoveryBusy},{"executionSuspendedMs",executionClock.suspendedMs()},
        {"continuation",continuation==Continuation::KnownDrop?"known-drop":continuation==Continuation::WorldResult?"world-result":"none"},
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
        {"clock",entry.action=="supply-detour" || entry.action=="world-result" || entry.action=="quest-portal-transfer" || entry.action=="door" || (entry.action=="area-transfer" && travelPhase!=TravelPhase::WaypointWait) || (entry.action=="merchant" && shelfObserved) ||
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
    observe([&]{observePickup(c);});
    observe([&]{observePotion(c);});
    observe([&]{observeCombat(c);});
    observeWaypointMemory(c);
    observePopulation(c);
    if(healingIssued && !healingObserved) {
        if(view.world.life && c.supplies && c.supplies->maxLife && *view.world.life>=*c.supplies->maxLife) {
            healingObserved=true;note("town-healing-result-observed");
        } else if(now>=shopDeadline) failure="Town healer interaction did not restore observed life; no blind retry";
    }
    if(pendingDoor) {
        const auto door=view.world.units.find(pendingDoor->first);
        if(door==view.world.units.end() || door->second.assignmentRevision!=pendingDoor->second) {
            note("door-assignment-withdrawn");pendingDoor.reset();passageWaits.erase(Passage::Door);
        } else if(door->second.mode && *door->second.mode!=0) {
            operatedDoors.insert(*pendingDoor);note("door-mode-observed",std::to_string(door->first.id));pendingDoor.reset();passageWaits.erase(Passage::Door);
            if(supplyStage==SupplyStage::None) continuation=Continuation::WorldResult;
        } else if(now>=doorDeadline) failure="Door interaction submitted but the assigned door stayed closed";
        else wait("door-wait","Waiting for the actual door mode; defensive inputs remain eligible");
    }
    if(pendingWaypoint) {
        const auto waypoint=view.world.units.find(pendingWaypoint->unit);
        const bool menu=view.world.waypointSource==pendingWaypoint->unit.id && view.world.waypointHistory &&
            std::any_of(c.town.read().waypoints.begin(),c.town.read().waypoints.end(),[&](const auto &entry) {
                return entry.level==pendingWaypoint->area && entry.unlocked;
            });
        const bool activated=!pendingWaypoint->menu && view.world.waypointActivation &&
            view.world.waypointActivation->source==pendingWaypoint->unit.id &&
            view.world.waypointActivation->generation==view.world.interactionGeneration;
        if(menu || activated) {
            knownWaypointAreas.insert(pendingWaypoint->area);
            if(view.world.waypointSource==pendingWaypoint->unit.id) closeWaypoint=pendingWaypoint->unit.id;
            note("waypoint-activation-observed","area="+std::to_string(pendingWaypoint->area)+" unit="+std::to_string(pendingWaypoint->unit.id));pendingWaypoint.reset();
        } else if(waypoint==view.world.units.end() || waypoint->second.assignmentRevision!=pendingWaypoint->assignment) {
            note("waypoint-assignment-withdrawn");pendingWaypoint.reset();
        } else if(now>=waypointDeadline) {
            // This is an opportunistic activation, not a requested journey.
            // A server-side approach may be interrupted by defensive input.
            // Keep the missing result visible; never unlock locally or abort
            // the unrelated quest. Retry after the cooldown.
            deferredWaypoint=WaypointDeferral{pendingWaypoint->unit,pendingWaypoint->assignment,
                now+std::chrono::seconds(30),c.position};
            if(view.world.waypointSource==pendingWaypoint->unit.id || view.world.waypointRequested==pendingWaypoint->unit.id)
                closeWaypoint=pendingWaypoint->unit.id;
            note("waypoint-activation-deferred","No actual mode / unlock result; require a new approach before another click");
            pendingWaypoint.reset();
        }
        else wait("waypoint-activation-wait","Waiting for actual waypoint mode / personal unlock");
    }
    if(closeWaypoint && view.world.waypointSource!=closeWaypoint && view.world.waypointRequested!=closeWaypoint) closeWaypoint.reset();
    observe([&]{observeMobility(c);});
    observe([&]{observeQuestFacts(c);});
    observe([&]{observeQuestResult(c);});
    observe([&]{observeWorldWait(c);});
    if(shopIssued && !potionRequest && !shelfObserved) {
        const bool ready=supplyNpc && view.world.shopSource==supplyNpc->id && !view.world.shopGamble &&
            (!view.world.itemRequest || view.world.itemRequest->state!=OnlineItemRequest::State::Pending);
        if(ready) {shelfObserved=true;shopDeadline=now+std::chrono::seconds(12);note("potion-shelf-observed");}
        // Restock owns bounded reopening once ordinary interaction input is available.
    }
    if(travelPhase==TravelPhase::WaypointWait || (travelPhase==TravelPhase::CrossWait && legUnit)) {
        if(legDestination && c.town.read().area==legDestination) {
            travelPhase=TravelPhase::Route;legDestination.reset();waypointDestination.reset();legUnit.reset();cancelWalk(c);
            note("travel-result-observed");
        } else if(travelPhase==TravelPhase::CrossWait && legUnit && recoverTravel(c)) {
            travelPhase=TravelPhase::WarpExit;
        } else if(now>=actionDeadline) {
            if(travelPhase==TravelPhase::CrossWait && legUnit && recoverTravel(c)) travelPhase=TravelPhase::WarpExit;
            else failure="Submitted travel's destination was not observed";
        }
    }
    if(supplyPortalUsed && supplyOrigin!=c.town.read().area) {
        if(recoverTravel(c)) supplyPortalUsed=false;
        else if(now>=supplyPortalDeadline) failure="Return portal was used but the original supply area was not observed";
    }
    if(!failure.empty()) throw std::runtime_error(failure);
}
} // namespace d2x
