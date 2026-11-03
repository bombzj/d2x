#include "remote_control.hpp"
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace d2x {
namespace {
constexpr auto unitApproachTimeout=std::chrono::seconds(15);
bool nativeProgress(std::optional<OnlinePoint> &previous, OnlinePoint player, OnlinePoint goal) {
    auto distance = [&](OnlinePoint point) {
        const int64_t dx=int64_t(point.x)-goal.x, dy=int64_t(point.y)-goal.y;
        return dx*dx+dy*dy;
    };
    const bool closer=previous && player!=*previous && distance(player)<distance(*previous);
    previous=player;
    return closer;
}
}
bool RemoteControl::reject(std::string reason) { reason_ = std::move(reason); return false; }
bool RemoteControl::submitSegment(Movement &movement, std::optional<OnlinePoint> stopNpc) {
    const auto &view = session_.read();
    const auto &binding = scene_.read();
    const auto *map = scene_.map();
    if (!map || !binding.origin || !view.world.playerPosition || !binding.movementAvailable)
        return reject("Movement has no active native collision binding");
    const auto player = *view.world.playerPosition;
    if (movement.unit) {
        const auto found = view.world.units.find(*movement.unit);
        if (found == view.world.units.end() || !found->second.position)
            return reject("Movement target is no longer assigned");
    }
    const Vec origin = movement.requestOrigin.value_or(Vec{float(player.x),float(player.y)});
    const float dx = float(movement.goal.x) - origin.x, dy = float(movement.goal.y) - origin.y;
    if (std::abs(dx) > 50 || std::abs(dy) > 50)
        return reject("Movement target exceeds the native 50-subtile request range");
    // PlrMsg 01/03 takes the actual pointer destination; 02/04 takes the
    // actual GUID. The 18-cell limit in PATH_Straight_Compute applies to
    // its AStar fallback, not to commands or unobstructed walking distance.
    // Splitting from a delayed position makes dragging send a waypoint
    // behind the moving server player and waits for samples between legs.
    if (movement.unit) {
        if (!session_.move_to_unit(*movement.unit, movement.run, movement.context, stopNpc))
            return reject(session_.read().error ? session_.read().error->message : "Unit movement was not submitted");
        movement.finalUnit = true;
    } else {
        if (!session_.move_to(movement.goal, movement.run, movement.context))
            return reject(session_.read().error ? session_.read().error->message : "Movement request was not submitted");
        movement.segment = movement.goal;
    }
    const auto &sent = session_.read();
    movement.requestRevision = sent.world.movementRequest ? sent.world.movementRequest->revision : 0;
    movement.context = onlineIntentContext(sent);
    if (approach_) {
        approach_->requestRevision = movement.requestRevision;
        approach_->context = movement.context;
    }
    movement.submitted = std::chrono::steady_clock::now();
    movement.deadline = movement.submitted + std::chrono::seconds(onlineMovementProgressTimeoutSeconds);
    movement.progressPosition = player;
    reason_.clear(); return true;
}
bool RemoteControl::move(OnlinePoint point, bool run, std::optional<OnlineIntentContext> context, std::optional<Vec> requestOrigin) {
    if (!context) context = onlineIntentContext(session_.read());
    if (!onlineInteractionMatches(*context, session_.read())) return reject("Movement intent belongs to a previous game, area or interaction");
    scene_.update(session_.read(), requestOrigin);
    const auto player = session_.read().world.playerPosition;
    if (requestOrigin && (!std::isfinite(requestOrigin->x) || !std::isfinite(requestOrigin->y)))
        return reject("Movement projection origin is invalid");
    const Vec origin = requestOrigin.value_or(player ? Vec{float(player->x),float(player->y)} : Vec{});
    if (!player || std::abs(origin.x - point.x) > 50 || std::abs(origin.y - point.y) > 50)
        return reject("Movement exceeds the native 50-subtile request range");
    if (!scene_.permits(session_.read(), point)) return reject("Movement destination is outside active player collision");
    if (movement_ && !movement_->unit && movement_->goal == point && movement_->run == run &&
        movement_->game == session_.read().gameGeneration && movement_->area == session_.read().world.areaGeneration &&
        onlineInteractionMatches(movement_->context, session_.read()))
        return true;
    const auto now = std::chrono::steady_clock::now();
    Movement movement; movement.goal = point; movement.run = run;
    movement.requestOrigin = requestOrigin;
    movement.context = *context;
    movement.game = session_.read().gameGeneration; movement.area = session_.read().world.areaGeneration;
    movement.deadline = now + std::chrono::seconds(onlineMovementProgressTimeoutSeconds);
    if (!submitSegment(movement)) return false;
    movement_ = std::move(movement); approach_.reset(); reason_.clear(); return true;
}
bool RemoteControl::moveToUnit(OnlineUnitKey target, bool run, std::optional<OnlineIntentContext> context,
                               std::optional<Vec> displayOrigin, std::optional<OnlinePoint> stopNpc) {
    if (!context) context = onlineIntentContext(session_.read());
    if (!onlineInteractionMatches(*context, session_.read())) return reject("Unit movement intent belongs to a previous game, area or interaction");
    scene_.update(session_.read(), displayOrigin);
    if (!scene_.permitsInteraction(session_.read(), target)) return reject("Target is not assigned in the current scene");
    const auto player = *session_.read().world.playerPosition;
    const auto sample = *session_.read().world.units.at(target).position;
    const auto point = target.type==1 && npcPathPosition_ ? npcPathPosition_(target).value_or(sample) : sample;
    const Vec origin = displayOrigin.value_or(Vec{float(player.x), float(player.y)});
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) ||
        std::abs(origin.x - point.x) > 50 || std::abs(origin.y - point.y) > 50)
        return reject("Target exceeds the native 50-subtile request range");
    const auto now = std::chrono::steady_clock::now();
    Movement movement; movement.goal = point; movement.unit = target; movement.run = run;
    movement.requestOrigin = origin;
    movement.context = *context;
    movement.game = session_.read().gameGeneration; movement.area = session_.read().world.areaGeneration;
    movement.deadline = now + std::chrono::seconds(onlineMovementProgressTimeoutSeconds);
    if (!submitSegment(movement, stopNpc)) return false;
    if (movement.finalUnit) movement_.reset();
    else movement_ = std::move(movement);
    approach_.reset(); reason_.clear(); return true;
}
bool RemoteControl::interact(OnlineUnitKey target, bool run, std::optional<OnlineIntentContext> context,
                             std::optional<Vec> displayOrigin, bool followUntilServerArrival) {
    if (!context) context = onlineIntentContext(session_.read());
    if (!onlineInteractionMatches(*context, session_.read())) return reject("Interaction intent belongs to a previous game, area or interaction");
    scene_.update(session_.read(), displayOrigin);
    const auto &world = session_.read();
    if (!scene_.permitsInteraction(world, target)) return reject("Target is not assigned in the current scene");
    const auto player = *world.world.playerPosition;
    const auto sample = *world.world.units.at(target).position;
    const auto point = target.type==1 && npcPathPosition_ ? npcPathPosition_(target).value_or(sample) : sample;
    if (displayOrigin && (!std::isfinite(displayOrigin->x) || !std::isfinite(displayOrigin->y) ||
        displayOrigin->x < 0 || displayOrigin->y < 0 || displayOrigin->x > UINT16_MAX || displayOrigin->y > UINT16_MAX))
        return reject("Interaction projection origin is invalid");
    const Vec origin = displayOrigin.value_or(Vec{float(player.x), float(player.y)});
    if (std::abs(origin.x - point.x) > 50 || std::abs(origin.y - point.y) > 50)
        return reject("Target exceeds the native 50-subtile request range");
    if ((target.type == 1 && world.world.npcRequested == target.id) || (approach_ && approach_->target == target))
        return reject("Unit interaction is already pending or open");
    // The current local path position authorizes only the original request.
    // PlrMsg validates eligibility before the server opens a conversation.
    const auto &targets = scene_.read().mapTargets;
    std::optional<OnlinePoint> stopNpc;
    if (std::any_of(targets.begin(), targets.end(), [&](const auto &entry) { return entry.unit == target && entry.stopNpcOnClick; }))
        stopNpc = point;
    if (!followUntilServerArrival && scene_.interactionReady(world, target, displayOrigin, false, point)) {
        if (!submitInteraction(target, *context, stopNpc)) return false;
        if (stopNpc && stopNpcPath_) stopNpcPath_(target);
        cancelMovement(); reason_.clear(); return true;
    }
    if (!moveToUnit(target, run, context, displayOrigin, stopNpc)) return false;
    if (stopNpc && stopNpcPath_) stopNpcPath_(target);
    approach_ = Approach{target, world.gameGeneration, world.world.areaGeneration,
        world.world.movementRequest ? world.world.movementRequest->revision : 0,
        std::chrono::steady_clock::now() + unitApproachTimeout, onlineIntentContext(session_.read()), player,
        followUntilServerArrival};
    return true;
}
bool RemoteControl::submitInteraction(OnlineUnitKey target, const OnlineIntentContext &context, std::optional<OnlinePoint> stopNpc) {
    const auto &before=session_.read();
    const auto player=before.world.playerPosition.value_or(OnlinePoint{});
    const auto origin=scene_.inputOrigin(before).value_or(Vec{float(player.x),float(player.y)});
    InteractionSubmission submitted;submitted.target=target;submitted.context=onlineIntentContext(before);
    submitted.combatSequence=before.world.combatSequence;
    submitted.position={uint16_t(std::clamp(std::lround(origin.x),0l,long(UINT16_MAX))),
        uint16_t(std::clamp(std::lround(origin.y),0l,long(UINT16_MAX)))};
    if(const auto actor=before.world.units.find({0,before.load.playerUnitId.value_or(0)});actor!=before.world.units.end())
        submitted.discontinuity=actor->second.positionDiscontinuity;
    const auto remember=[&] {
        submitted.revision=session_.read().world.revision;interactionSubmission_=submitted;
    };
    if (target.type == 1) {
        if (!session_.interact_npc(target.id, context, stopNpc)) return reject(session_.read().error ? session_.read().error->message : "Interaction is already pending or open");
        remember();
        const auto now=std::chrono::steady_clock::now();
        opening_=Opening{target,onlineIntentContext(session_.read()),0,0,
            now+std::chrono::milliseconds(100),now+std::chrono::seconds(15)};
        return true;
    }
    const auto entry = std::find_if(scene_.read().mapTargets.begin(), scene_.read().mapTargets.end(), [&](const auto &value) { return value.unit == target; });
    if (entry == scene_.read().mapTargets.end()) return reject("Interaction target is no longer assigned");
    const auto intent = entry->interaction == OnlineMapInteraction::Stash ? OnlineObjectIntent::Stash
        : entry->interaction == OnlineMapInteraction::Waypoint ? OnlineObjectIntent::Waypoint : OnlineObjectIntent::Operate;
    if (!session_.interact_map_unit(target, intent, context)) return reject(session_.read().error ? session_.read().error->message : "Interaction is already pending or open");
    remember();
    if(target.type==5 || intent==OnlineObjectIntent::Waypoint) {
        const auto &view=session_.read();const auto now=std::chrono::steady_clock::now();
        opening_=Opening{target,onlineIntentContext(view),view.world.movementRequest?view.world.movementRequest->revision:0,0,
            now+std::chrono::milliseconds(100),now+std::chrono::seconds(target.type==5?1:15)};
    } else opening_.reset();
    return true;
}
void RemoteControl::observeOpening() {
    if(!opening_) return;
    const auto &view=session_.read();const auto &world=view.world;const auto now=std::chrono::steady_clock::now();
    const auto target=opening_->target;
    // Only the opening click is repeatable. A conversation (including its
    // pending assignment), area change, new intent or retired unit ends it.
    // Never replay a menu selection, transaction, object operation or portal.
    if(view.stage!=OnlineStage::ProtocolReady || !onlineInteractionMatches(opening_->context,view) ||
        onlinePlayerDead(world) || !world.units.contains(target) || now>=opening_->deadline ||
        (target.type==1 && (world.npcRequested!=target.id || world.npcConversation || world.npcPendingConversation)) ||
        (target.type==2 && (world.waypointRequested!=target.id || world.waypointSource)) ||
        (target.type!=1 && (!world.movementRequest || world.movementRequest->revision!=opening_->movementRevision))) {
        opening_.reset();return;
    }
    if(now<opening_->next) return;
    // NPC13 outside the host's 7..8 approach band is ignored. After the
    // short burst, keep the original click intent and complete it once the
    // host's position actually enters the menu range. This is a single
    // receipt-driven continuation, not a timed loop or a longer NPC deadline.
    const bool confirmedArrival=target.type!=5 && opening_->retries>=3;
    if(confirmedArrival && !scene_.interactionReady(view,target)) return;
    const bool sent=target.type==1?session_.interact_npc(target.id,opening_->context):
        session_.interact_map_unit(target,target.type==2?OnlineObjectIntent::Waypoint:OnlineObjectIntent::Operate,opening_->context);
    opening_->next=now+std::chrono::milliseconds(100);
    if(!sent) return;
    const auto &after=session_.read();opening_->context=onlineIntentContext(after);
    opening_->movementRevision=after.world.movementRequest?after.world.movementRequest->revision:0;
    ++opening_->retries;
    if(confirmedArrival || (target.type==5 && opening_->retries>=3)) opening_.reset();
}
bool RemoteControl::townPortal() {
    scene_.update(session_.read());
    const auto &binding = scene_.read();
    const auto &world = session_.read().world;
    if (!binding.nativeMapReady || !binding.movementAvailable || binding.town)
        return reject("Town portals require a live player in a loaded outdoor or dungeon area");
    const bool knownQuantities=std::any_of(binding.townPortalSkills.begin(),binding.townPortalSkills.end(),
        [&](uint16_t skill){return world.itemSkillQuantities.contains(skill);});
    for (uint16_t skill : binding.townPortalSkills) {
        const auto quantity = world.itemSkillQuantities.find(skill);
        const auto level = world.playerSkills.find(skill);
        // Reported consumable quantities take precedence over old skill-list
        // availability. A retained scroll skill must not hide a usable tome.
        if ((knownQuantities || session_.read().protocolProfile == "pd2-cn-normal-20261020") &&
            (quantity == world.itemSkillQuantities.end() || !quantity->second)) continue;
        if (quantity != world.itemSkillQuantities.end() ? quantity->second != 0
            : level != world.playerSkills.end() && level->second != 0) {
            if (!session_.create_town_portal(skill)) return reject(session_.read().error
                ? session_.read().error->message : "Portal creation was not submitted");
            cancelMovement(); reason_.clear(); return true;
        }
    }
    return reject("No available town-portal scroll or tome skill has been reported by the server");
}
void RemoteControl::tick(std::optional<Vec> displayOrigin, std::optional<Vec> pathPosition, bool actionBusy) {
    if(!actionBusy) observeOpening();
    const auto &view = session_.read();
    const auto now = std::chrono::steady_clock::now();
    if (view.remoteLobbyOnly && pathPosition && view.stage == OnlineStage::ProtocolReady &&
        std::isfinite(pathPosition->x) && std::isfinite(pathPosition->y) &&
        pathPosition->x >= 0 && pathPosition->y >= 0 &&
        pathPosition->x <= UINT16_MAX && pathPosition->y <= UINT16_MAX) {
        // Native DynamicPath+2/+6 are integer high words, not rounded targets.
        session_.report_position_pd2({uint16_t(pathPosition->x), uint16_t(pathPosition->y)}, onlineIntentContext(view));
    }
    if(displayOrigin && view.stage==OnlineStage::ProtocolReady && view.load.playerUnitId && !onlinePlayerDead(view.world)) {
        const auto found=view.world.units.find({0,*view.load.playerUnitId});
        if(found!=view.world.units.end() && found->second.position && found->second.pathVerificationRevision &&
            found->second.positionRevision==found->second.pathVerificationRevision &&
            (verificationGame_!=view.gameGeneration || verificationArea_!=view.world.areaGeneration || verificationRevision_!=found->second.pathVerificationRevision)) {
            const auto &unit=found->second;
            verificationGame_=view.gameGeneration;verificationArea_=view.world.areaGeneration;verificationRevision_=unit.pathVerificationRevision;
            if(std::isfinite(displayOrigin->x) && std::isfinite(displayOrigin->y) &&
                displayOrigin->x>=0 && displayOrigin->y>=0 && displayOrigin->x<=UINT16_MAX && displayOrigin->y<=UINT16_MAX) {
                const OnlinePoint local{uint16_t(std::lround(displayOrigin->x)),uint16_t(std::lround(displayOrigin->y))};
                const auto previous=view.world.movementRequest;
                const bool run=previous?previous->run:unit.nativeMode?unit.mode==3:unit.mode==23 || unit.mode==24;
                const bool idle=!previous && !view.world.combatRequest && !scene_.read().town &&
                    (unit.nativeMode?unit.mode==1:unit.mode==7);
                // Original 1.13c D2Client RVA 2A1B0: own-unit verification
                // tolerates per-axis drift, then asks the server with 5F. It
                // never overwrites the local path with resource coordinates.
                const auto tolerance=uint64_t(idle?3:run?7:5)+(uint64_t(view.latencyMilliseconds.value_or(0))+50)/128;
                const int dx=int(local.x)-unit.position->x,dy=int(local.y)-unit.position->y;
                bool report=uint64_t(std::abs(dx))>tolerance || uint64_t(std::abs(dy))>tolerance;
                const int64_t squared=int64_t(dx)*dx+int64_t(dy)*dy;
                if(report && squared<100 && unit.verifiedDestination) {
                    const int x=2*int(unit.position->x)-unit.verifiedDestination->x;
                    const int y=2*int(unit.position->y)-unit.verifiedDestination->y;
                    const int64_t backX=int(local.x)-x,backY=int(local.y)-y;
                    if(x>0 && backX*backX+backY*backY<squared) report=false;
                }
                if(report) session_.update_position(local,unit.pathVerificationRevision,onlineIntentContext(view));
            }
        }
    }
    if (movement_) {
        const auto &world = view.world;
        if (view.stage != OnlineStage::ProtocolReady || view.gameGeneration != movement_->game ||
            !onlineInteractionMatches(movement_->context, view) ||
            world.areaGeneration != movement_->area || !world.playerPosition ||
            !scene_.read().movementAvailable || onlinePlayerDead(world) ||
            world.npcRequested || world.waypointSource ||
            onlineHasCursorItem(world.items, view.load.playerUnitId) ||
            !world.movementRequest || world.movementRequest->revision != movement_->requestRevision) {
            cancelMovement();
        } else {
            auto &movement = *movement_;
            const auto player = *world.playerPosition;
            const auto actor = view.load.playerUnitId ? world.units.find({0, *view.load.playerUnitId}) : world.units.end();
            // Native Straight/AStar can finish at a neighbouring reachable
            // cell. Accept only a newer server target already reached, not
            // merely the sprite or a player sample near the requested goal.
            const bool reachedNativeGoal = actor != world.units.end() &&
                actor->second.pathVerificationRevision > movement.requestRevision &&
                actor->second.verifiedDestination == player &&
                std::abs(int(player.x) - movement.goal.x) <= 1 &&
                std::abs(int(player.y) - movement.goal.y) <= 1;
            if (nativeProgress(movement.progressPosition,player,movement.segment.value_or(movement.goal)))
                movement.deadline=now+std::chrono::seconds(onlineMovementProgressTimeoutSeconds);
            // The bound is on lack of native progress, not total journey time.
            if (now >= movement.deadline) {
                cancelMovement(); reason_ = "Server movement made no progress before timeout";
            } else if (!movement.unit && (player == movement.goal || reachedNativeGoal)) {
                movement_.reset(); reason_.clear();
            }
        }
    }
    if (!approach_) return;
    if (view.stage != OnlineStage::ProtocolReady || view.gameGeneration != approach_->game ||
        !onlineInteractionMatches(approach_->context, view) ||
        view.world.areaGeneration != approach_->area || view.world.waypointSource ||
        view.world.npcRequested || !view.world.movementRequest ||
        view.world.movementRequest->revision != approach_->requestRevision ||
        !scene_.permitsInteraction(view, approach_->target)) {
        cancelApproach(); return;
    }
    if (view.world.playerPosition) {
        const auto &target=view.world.units.at(approach_->target);
        if (target.position && nativeProgress(approach_->progressPosition,*view.world.playerPosition,*target.position))
            approach_->deadline=now+unitApproachTimeout;
    }
    if (now >= approach_->deadline) {
        cancelApproach(); reason_ = "Server unit approach did not arrive before timeout"; return;
    }
    if(actionBusy) return; // Keep observing progress; defer the next automatic click/follow until recovery.
    // A unit click can race the end of the preceding coordinate path. A
    // newer verification may still show that old endpoint, outside interaction
    // range, while local prediction has already continued toward the unit.
    // Continue the retained click with ordinary 02/04 once per such receipt;
    // repeating 13 alone cannot approach an NPC outside its 7..8 distance band.
    const auto actor=view.load.playerUnitId?view.world.units.find({0,*view.load.playerUnitId}):view.world.units.end();
    const auto &target=view.world.units.at(approach_->target);
    if(actor!=view.world.units.end() && actor->second.position && target.position &&
        actor->second.positionRevision==actor->second.pathVerificationRevision &&
        actor->second.pathVerificationRevision>approach_->requestRevision && actor->second.verifiedDestination &&
        std::abs(int(actor->second.position->x)-actor->second.verifiedDestination->x)<=1 &&
        std::abs(int(actor->second.position->y)-actor->second.verifiedDestination->y)<=1 &&
        !scene_.interactionReady(view,approach_->target) &&
        // PlrMsg::sub_6FC83C60 uses per-axis distance, not unit interaction distance.
        std::abs(int(actor->second.position->x)-target.position->x)<=50 &&
        std::abs(int(actor->second.position->y)-target.position->y)<=50) {
        if(session_.move_to_unit(approach_->target,view.world.movementRequest->run,approach_->context)) {
            const auto &sent=session_.read();
            approach_->requestRevision=sent.world.movementRequest->revision;
            approach_->context=onlineIntentContext(sent);
            reason_.clear();
        }
        // Keep the existing no-progress deadline; a resend is not progress.
        return;
    }
    // 02/04 only follows the unit. A stationary host need not send another
    // 96 on arrival. Prefer fresh server progress briefly, then let a local
    // arrival submit the normal 13. Only its result completes the interaction.
    const auto grace=std::chrono::milliseconds(std::clamp<uint64_t>(
        2*view.latencyMilliseconds.value_or(0)+200,600,2000));
    const bool settledFollow=approach_->awaitServerRange && now>=approach_->deadline-unitApproachTimeout+grace;
    if(approach_->awaitServerRange && !settledFollow && !scene_.interactionReady(view,approach_->target)) return;
    // The retained LoD NPC click uses UNITS_IsInMeleeRange, independently
    // of the first-click distance 2 and the server's menu distance 6.
    const auto targetPath = npcPathPosition_ ? npcPathPosition_(approach_->target) : std::nullopt;
    const auto followRange = view.protocolProfile == "lod113c" && npcFollowRange_
        ? npcFollowRange_(approach_->target) : std::nullopt;
    const bool observedMenuRange = !followRange && approach_->target.type == 1 &&
        (settledFollow || scene_.interactionReady(view, approach_->target)) &&
        scene_.interactionReady(view, approach_->target, displayOrigin, false, targetPath);
    if (observedMenuRange || scene_.interactionReady(view, approach_->target, displayOrigin, true, targetPath, followRange)) {
        const bool sent = submitInteraction(approach_->target, approach_->context);
        if (sent) { cancelMovement(); reason_.clear(); }
    }
}
} // namespace d2x
