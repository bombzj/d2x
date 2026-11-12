#include "ai_impl.hpp"
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::prepareDecision(Context &c) {
    const auto &view=c.observation.read();const auto &supplies=*c.supplies;
    prepareCombatSkill(c);
    supply.supplyVitals={{"life",view.world.life?Json(*view.world.life):Json(nullptr)},
        {"mana",view.world.mana?Json(*view.world.mana):Json(nullptr)},
        {"displayMaxLife",supplies.maxLife?Json(*supplies.maxLife):Json(nullptr)},
        {"displayMaxMana",supplies.maxMana?Json(*supplies.maxMana):Json(nullptr)}};
    if(supplyPolicy.enabled) {
        const auto stock=portalStock(c);
        supply.supplyVitals["portalBooks"]=stock.books;supply.supplyVitals["portalPages"]=stock.pages;
        supply.supplyVitals["portalCapacity"]=stock.capacity;supply.supplyVitals["portalStockKnown"]=stock.known;
        supply.supplyVitals["healthPotions"]=potionCount(c,PotionKind::Life);
        supply.supplyVitals["manaPotions"]=potionCount(c,PotionKind::Mana);
        const auto ammo=quiverStock(c);
        if(ammo.body) supply.supplyVitals["ammunition"]={{"type",ammo.type},{"stacks",ammo.stacks},{"rounds",ammo.rounds},
            {"equippedQuantity",ammo.equippedQuantity?Json(*ammo.equippedQuantity):Json(nullptr)}};
    }
}
bool DebugAi::Impl::recoverSessionInput(Context &c,Clock::time_point now) {
    const auto &observation=c.observation;auto &town=c.town;auto &walk=c.walk;auto &control=c.control;auto &combat=c.combat;
    const auto &view=observation.read();
    if(!view.error || view.error->kind!=OnlineErrorKind::Timeout) navigation.npcOpeningRecovery=false;
    const auto &item=view.world.itemRequest;
    const bool shelfTimeout=supply.shopIssued && !supply.potionRequest && !supply.shelfObserved && supply.supplyNpc &&
        view.error && view.error->kind==OnlineErrorKind::Timeout &&
        view.error->message=="No related server item update arrived; inspect current inventory before retrying" &&
        item && item->state==OnlineItemRequest::State::TimedOut && item->command.action==OnlineItemAction::TradeOpen &&
        item->command.npc==supply.supplyNpc->id && supply.shopGeneration==view.world.interactionGeneration;
    if(view.error && view.error->kind!=OnlineErrorKind::Input && !shelfTimeout && navigation.handledWaypointTimeout!=view.error->sequence) {
        const auto purpose=bool(navigation.walkIntent)?navigation.walkIntent.purpose.value_or(std::string{}):std::string{};
        const bool optionalWaypoint=purpose=="initial-waypoint-memory" || purpose=="activate-waypoint";
        const bool waypointOpening=view.error->kind==OnlineErrorKind::Timeout &&
            view.error->message=="No original waypoint menu confirmation arrived" &&
            ((navigation.travelPhase==TravelPhase::WaypointOpen && navigation.travelRoute.size()>1) || optionalWaypoint ||
             (!bool(navigation.walkIntent) && navigation.deferredWaypoint && now<navigation.deferredWaypoint->retryAt));
        if(waypointOpening) {
            navigation.handledWaypointTimeout=view.error->sequence;
            if(navigation.travelPhase==TravelPhase::WaypointOpen || optionalWaypoint) {
                const auto child=walk.observe();const auto &source=child.unit;
                if(!source) {finish("failed",view.error->message,c);return true;}
                const OnlineUnitKey key=*source;
                const auto unit=view.world.units.find(key);
                navigation.deferredWaypoint=WaypointDeferral{key,unit==view.world.units.end()?0:unit->second.assignmentRevision,
                    now+std::chrono::seconds(30),c.position};
                note("waypoint-query-deferred","Opening had no menu result; continuing the retained ordinary route");
                if(view.world.waypointRequested || view.world.waypointSource) control.useWaypoint(0);
                cancelWalk(c);
                if(!optionalWaypoint) {navigation.travelPhase=TravelPhase::Route;navigation.waypointDestination.reset();navigation.legDestination.reset();}
            }
            // The first subsequent ordinary input clears the session's
            // timeout. Until then retain the diagnostic, not a fake menu.
        } else {
            const bool npcOpening=view.error->kind==OnlineErrorKind::Timeout &&
                view.error->message=="NPC did not return a conversation before timeout" &&
                bool(navigation.walkIntent) && navigation.walkIntent.goal==DebugWalkGoal::Unit &&
                (navigation.walkIntent.unit?navigation.walkIntent.unit->type:0)==1 && navigation.walkIntent.interact.value_or(false) &&
                !questState.questRun.pending && !supply.potionRequest && !supply.shopIssued;
            if(npcOpening && (navigation.npcOpenRetries<2 || navigation.npcOpeningRecovery)) {
                if(inputBlocked) {wait("npc-recovery-input","The opening timed out; retry input waits until observation closes");return true;}
                if(navigation.npcOpeningRecovery && walk.active()) return true;
                if(navigation.npcOpenRetries>=2) {finish("failed",view.error->message,c);return true;}
                ++navigation.npcOpenRetries;note("npc-open-retry","Opening only; no quest topic or item operation is replayed");
                auto intent=navigation.walkIntent;cancelWalk(c);navigation.walkIntent=intent;
                intent.forceApproach=true;intent.resyncApproach=navigation.npcOpenRetries==2;walk.start(intent,observation,town,control);
                navigation.npcOpeningRecovery=true;return true;
            }
            const bool returning=cursor<goals.size() && (questLeaf(goals[cursor]).kind==Kind::ReturnTown ||
                goals[cursor].kind==Kind::Restock || supply.supplyStage==SupplyStage::ToTown ||
                (navigation.explorationReturn && !navigation.explorationReturn->returning));
            const bool portalTimeout=returning &&
                portalIssued && !portalUsed && view.error->kind==OnlineErrorKind::Timeout &&
                view.error->message=="No new server-owned town portal was assigned";
            if(!portalTimeout) {finish("failed",view.error->message,c);return true;}
            // The native request's result is unknown: never cast again or
            // assume a scroll was consumed. Use a separate ordinary walk home.
            note("town-portal-unconfirmed",view.error->message);
            stop(walk,control,combat);navigation.walkIntent={};portalIssued=false;navigation.passageWaits.erase(Passage::CreatePortal);
        }
    }
    return false;
}
void DebugAi::Impl::dispatchActions(Context &c,const Goal &goal,Clock::time_point now) {
    auto &town=c.town;auto &walk=c.walk;auto &control=c.control;const auto &view=c.observation.read();
    if(inputBlocked) return;
    const bool travelSatisfied=goal.kind==Kind::Travel && goal.area &&
        (goal.requiredWaypoint?navigation.knownWaypointAreas.contains(*goal.area):goal.area==area);
    if(actionRecoveryBusy) {
        if(!travelSatisfied && supply.supplyStage==SupplyStage::None && now>=goalDeadline) {
            finish("failed","Current high-level goal exceeded its execution budget during action recovery",c);return;
        }
        wait("action-recovery","Observing the normal action animation; potion use and result observation remain active");
        describeActions(c);return;
    }
    if(pickup && !pickup->observed) {
        wait("pickup-wait","Waiting for the submitted pickup to confirm personal ownership");return;
    }
    if(supply.quiverTransfer) {
        if(canInput(c,DebugActionOwner::Supply,DebugAllInputs)) reloadQuiver(c);
        describeActions(c);return;
    }
    if(canInput(c,DebugActionOwner::Supply,DebugAllInputs) && reloadQuiver(c)) {describeActions(c);return;}
    if(navigation.routeExecution && navigation.routeExecution->phase!=RouteExecution::Phase::Resume) {
        if(now>=goalDeadline) {finish("failed","Current high-level goal exceeded its execution budget",c);return;}
        if(canInput(c,DebugActionOwner::Potion,DebugItem)) drink(c);
        if(resumeRouteExecution(c,goal)) {describeActions(c);return;}
    }
    if(walk.recoveringMovement()) {
        if(!travelSatisfied && supply.supplyStage==SupplyStage::None && now>=goalDeadline) {
            finish("failed","Current high-level goal exceeded its execution budget during walking recovery",c);return;
        }
        if(canInput(c,DebugActionOwner::Potion,DebugItem)) drink(c);
        const auto child=walk.observe();const auto &anchor=child.recoveryAnchor;
        wait("walking-recovery","n="+std::to_string(child.movementRecoveries)+
            " via="+std::to_string(anchor->x)+","+std::to_string(anchor->y));
        describeActions(c);return;
    }
    if(beginWalkingPassage(c)) {describeActions(c);return;}
    if(navigation.explorationReturn) {
        if(canInput(c,DebugActionOwner::Potion,DebugItem) && drink(c)) {describeActions(c);return;}
        if(canInput(c,DebugActionOwner::Goal,DebugAllInputs)) resumeExplorationReturn(c);
        if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
        describeActions(c);return;
    }
    const auto preparedGoal=cursor;
    const bool prepared=prepareQuest(c,goal);
    if(!active() || cursor!=preparedGoal) return;
    if(supply.supplyStage==SupplyStage::None && now>=goalDeadline && !travelSatisfied) {
        finish("failed","Current high-level goal exceeded its execution budget",c);return;
    }
    using Owner=DebugActionOwner;
    if(navigation.closeWaypoint && canInput(c,Owner::Waypoint,DebugMenu)) {
        if(view.world.waypointSource==navigation.closeWaypoint && navigation.observedWaypointMenu==navigation.closeWaypoint &&
            now<navigation.waypointMenuOpenedAt+std::chrono::milliseconds(eventPolicy.waypointCloseDelayMs)) {
            if(canInput(c,Owner::Potion,DebugItem) && drink(c)) {describeActions(c);return;}
            wait("waypoint-close-delay","Waiting for the configured waypoint menu display time");
            describeActions(c);return;
        }
        if(view.world.waypointSource==navigation.closeWaypoint || view.world.waypointRequested==navigation.closeWaypoint) control.useWaypoint(0);
        navigation.closeWaypoint.reset();describeActions(c);return;
    }
    if(canInput(c,Owner::Potion,DebugItem) && drink(c)) {describeActions(c);return;}
    if(servicePassage(c,goal,prepared)) {describeActions(c);return;}
    if(canInput(c,Owner::Combat,DebugMotion|DebugSkill) && avoidHazards(c)) {describeActions(c);return;}
    if(canInput(c,Owner::Combat,DebugMotion|DebugSkill) && maintainArmor(c)) {describeActions(c);return;}
    if(navigation.pendingDoor) {if(canInput(c,Owner::Combat,DebugMotion|DebugSkill)) combat(c,goal,true);describeActions(c);return;}
    if(!prepared) {
        Goal defense;defense.kind=Kind::Travel;
        if(phase!="quest-drop-notice" && canInput(c,Owner::Combat,DebugMotion|DebugSkill)) combat(c,defense);
        describeActions(c);return;
    }
    if(goal.kind==Kind::Quest && questState.questRun.pending && questWaitRequiresInput(c) &&
        canInput(c,Owner::Goal,DebugAllInputs)) {
        // Finish an arrived reading / query continuation before starting
        // another optional attack. Waiting for a reply grants no priority.
        execute(c,goal);describeActions(c);return;
    }
    const auto leaf=questLeaf(goal);
    if(resumeAction(c,goal,leaf)) {describeActions(c);return;}
    if(canInput(c,Owner::Supply,DebugAllInputs) && autoSupply(c,leaf)) {describeActions(c);return;}
    if(!navigation.pendingWaypoint && !navigation.closeWaypoint && !questState.questRun.worldWait && !questState.questRun.encounterAnchored &&
        !portalIssued &&
        (!town.read().town || (leaf.kind!=Kind::ReturnTown && leaf.kind!=Kind::Restock)) &&
        canInput(c,Owner::Waypoint,DebugMotion|DebugInteraction|DebugMenu) && activateWaypoint(c)) {describeActions(c);return;}
    if(canInput(c,Owner::Combat,DebugMotion|DebugSkill) && ((leaf.kind==Kind::ReturnTown || leaf.kind==Kind::Restock)?
        supplyCombat(c,leaf):combat(c,leaf))) {describeActions(c);return;}
    unsigned inputs=DebugAllInputs;
    if(leaf.kind==Kind::Travel || leaf.kind==Kind::Find) inputs=DebugMotion|DebugInteraction|DebugMenu;
    else if(leaf.kind==Kind::Hunt) inputs=DebugMotion|DebugSkill;
    const auto owner=goal.kind==Kind::Restock?Owner::Supply:Owner::Goal;
    if(canInput(c,owner,inputs)) {execute(c,goal);}
    if(canInput(c,Owner::Mobility,DebugMotion|DebugSkill)) mobility(c);
    describeActions(c);
}
void DebugAi::tickPlan(const OnlineObservation &observation,const RemoteTown &town,DebugAutoWalk &walk,IOnlineControl &control,
        IOnlineCombat &combat,IOnlineInventory &inventory,const SuppliesView &supplies,bool blocked,std::optional<Vec> position,bool moving) {
    auto &s=*impl_;
    const auto now=Impl::Clock::now();const auto &view=observation.read();
    const bool recovering=supplies.actionInputBusy.value_or(supplies.castInputBusy.value_or(false));
    // Resume on the first frontend frame that observes animation recovery,
    // without paying another 40/80/160-ms decision interval. Potion safety
    // also wakes immediately on a real life drop during recovery.
    if((s.actionRecoveryBusy && !recovering) ||
        (recovering && s.decisionLife && view.world.life && *view.world.life<*s.decisionLife)) s.cadence.wake();
    s.actionRecoveryBusy=recovering;
    s.inputBlocked=blocked || !s.active() || !position || !town.read().nativeMapReady || !town.read().area || view.stage!=OnlineStage::ProtocolReady;
    const auto suspended=s.executionClock.sample(now,s.inputBlocked);
    const Vec observed=position.value_or(view.world.playerPosition?Vec{float(view.world.playerPosition->x),float(view.world.playerPosition->y)}:Vec{});
    Impl::Context c{observation,town,walk,control,combat,inventory,observed,moving,&supplies};
    s.suspendExecution(c,suspended);
    s.observeNpcDialogue(view,now);
    if(!s.decisionDue(c,now)) return;
    ++s.decisions;
    const auto beforeGoal=s.cursor,beforeStep=s.questState.questRun.step;
    const auto beforeMoves=s.moveGoals,beforeCasts=s.castRequests,beforeInteractions=s.interactions;
    const auto beforeWalkMoves=walk.observe().moveRequests;
    try {
        if(view.gameGeneration!=s.game) {
            s.finish("cancelled","Game changed",c);return;
        }
        if(view.stage!=OnlineStage::ProtocolReady) {
            if(view.error && view.error->kind!=OnlineErrorKind::Input) s.finish("failed",view.error->message,c);
            else s.wait("loading","Waiting for the normal protocol/map transition");
            return;
        }
        if(onlinePlayerDead(view.world)) {s.finish("failed","Player died; AI never fabricates resurrection",c);return;}
        if(view.world.playerTrade.active()) {s.finish("cancelled","Player trade took control",c);return;}
        s.observeArea(c,now);
        if(s.cursor>=s.goals.size()) {s.finish("completed","All goal postconditions observed",c);return;}
        s.prepareDecision(c);
        s.navigation.mobilityHold=false;
        const auto &goal=s.goals[s.cursor];
        s.observeDrops(c,goal);
        s.observeActions(c);s.describeActions(c);
        if(s.actionRecoveryBusy && s.canInput(c,DebugActionOwner::Potion,DebugItem) && s.drink(c)) {
            s.describeActions(c);return;
        }
        if(s.recoverSessionInput(c,now)) return;
        // Quest and supply actions can both acknowledge speech or leave the
        // conversation, including by submitting their next movement request.
        if(now<s.npcDialogueReadyAt()) {
            if(s.canInput(c,DebugActionOwner::Potion,DebugItem) && s.drink(c)) {s.describeActions(c);return;}
            s.wait("npc-dialogue-close-delay","Waiting for the configured NPC dialogue display time");return;
        }
        if(s.finishAutomaticNpcSpeech(c,goal)) return;
        s.dispatchActions(c,goal,now);
    } catch(const DebugSearchPending &) {
        s.cadence.continueNextFrame();s.wait("navigation-worker","Waiting for background navigation");
        s.describeActions(c);
    } catch(const std::exception &e) {s.finish("failed",e.what(),c);}
    // Always try the original continuation first. A passive wait or a yielded
    // navigation job can use the idle input slot, without delaying a ready leg.
    if(s.active() && s.cursor==beforeGoal && s.questState.questRun.step==beforeStep && s.cursor<s.goals.size() &&
        s.moveGoals==beforeMoves && s.castRequests==beforeCasts && s.interactions==beforeInteractions &&
        walk.observe().moveRequests==beforeWalkMoves) {
        try {s.defendWhileWaiting(c,s.goals[s.cursor]);s.describeActions(c);}
        catch(const std::exception &e) {s.finish("failed",e.what(),c);}
    }
}
} // namespace d2x
