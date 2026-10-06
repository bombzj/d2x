#include "remote_control.hpp"
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace d2x {
namespace {
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
bool RemoteControl::submitSegment(Movement &movement) {
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
        movement.goal = *found->second.position;
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
        if (!session_.move_to_unit(*movement.unit, movement.run, movement.context))
            return reject(view.error ? view.error->message : "Unit movement is rate limited");
        movement.finalUnit = true;
    } else {
        if (!session_.move_to(movement.goal, movement.run, movement.context))
            return reject(view.error ? view.error->message : "Movement request is rate limited");
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
    scene_.update(session_.read());
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
bool RemoteControl::moveToUnit(OnlineUnitKey target, bool run, std::optional<OnlineIntentContext> context) {
    if (!context) context = onlineIntentContext(session_.read());
    if (!onlineInteractionMatches(*context, session_.read())) return reject("Unit movement intent belongs to a previous game, area or interaction");
    scene_.update(session_.read());
    if (!scene_.permitsInteraction(session_.read(), target)) return reject("Target is not assigned in the current scene");
    const auto player = *session_.read().world.playerPosition;
    const auto point = *session_.read().world.units.at(target).position;
    if (std::abs(int(player.x) - point.x) > 50 || std::abs(int(player.y) - point.y) > 50)
        return reject("Target exceeds the native 50-subtile request range");
    const auto now = std::chrono::steady_clock::now();
    Movement movement; movement.goal = point; movement.unit = target; movement.run = run;
    movement.context = *context;
    movement.game = session_.read().gameGeneration; movement.area = session_.read().world.areaGeneration;
    movement.deadline = now + std::chrono::seconds(onlineMovementProgressTimeoutSeconds);
    if (!submitSegment(movement)) return false;
    if (movement.finalUnit) movement_.reset();
    else movement_ = std::move(movement);
    approach_.reset(); reason_.clear(); return true;
}
bool RemoteControl::interact(OnlineUnitKey target, bool run, std::optional<OnlineIntentContext> context) {
    if (!context) context = onlineIntentContext(session_.read());
    if (!onlineInteractionMatches(*context, session_.read())) return reject("Interaction intent belongs to a previous game, area or interaction");
    scene_.update(session_.read());
    const auto &world = session_.read();
    if (!scene_.permitsInteraction(world, target)) return reject("Target is not assigned in the current scene");
    const auto player = *world.world.playerPosition;
    const auto point = *world.world.units.at(target).position;
    if (std::abs(int(player.x) - point.x) > 50 || std::abs(int(player.y) - point.y) > 50)
        return reject("Target exceeds the native 50-subtile request range");
    if ((target.type == 1 && world.world.npcRequested == target.id) || (approach_ && approach_->target == target))
        return reject("Unit interaction is already pending or open");
    if (scene_.interactionReady(world, target)) {
        if (!submitInteraction(target, *context)) return false;
        cancelMovement(); reason_.clear(); return true;
    }
    if (target.type == 2) {
        const auto point = scene_.interactionApproachPoint(world, target);
        if (!point) return reject("No reachable interaction position in active native collision");
        if (!move(*point, run, context)) return false;
    } else if (!moveToUnit(target, run, context)) return false;
    approach_ = Approach{target, world.gameGeneration, world.world.areaGeneration,
        world.world.movementRequest ? world.world.movementRequest->revision : 0,
        std::chrono::steady_clock::now() + std::chrono::seconds(15), onlineIntentContext(session_.read()), player};
    return true;
}
bool RemoteControl::submitInteraction(OnlineUnitKey target, const OnlineIntentContext &context) {
    if (target.type == 1) {
        if (!session_.interact_npc(target.id, context)) return reject(session_.read().error ? session_.read().error->message : "Interaction is rate limited");
        return true;
    }
    const auto entry = std::find_if(scene_.read().mapTargets.begin(), scene_.read().mapTargets.end(), [&](const auto &value) { return value.unit == target; });
    if (entry == scene_.read().mapTargets.end()) return reject("Interaction target is no longer assigned");
    const auto intent = entry->interaction == OnlineMapInteraction::Stash ? OnlineObjectIntent::Stash
        : entry->interaction == OnlineMapInteraction::Waypoint ? OnlineObjectIntent::Waypoint : OnlineObjectIntent::Operate;
    if (!session_.interact_map_unit(target, intent, context)) return reject(session_.read().error ? session_.read().error->message : "Interaction is rate limited");
    return true;
}
bool RemoteControl::townPortal() {
    scene_.update(session_.read());
    const auto &binding = scene_.read();
    const auto &world = session_.read().world;
    if (!binding.nativeMapReady || !binding.movementAvailable || binding.town)
        return reject("Town portals require a live player in a loaded outdoor or dungeon area");
    for (uint16_t skill : binding.townPortalSkills) {
        const auto quantity = world.itemSkillQuantities.find(skill);
        const auto level = world.playerSkills.find(skill);
        if (quantity != world.itemSkillQuantities.end() ? quantity->second != 0
            : level != world.playerSkills.end() && level->second != 0) {
            if (!session_.create_town_portal(skill)) return reject(session_.read().error
                ? session_.read().error->message : "Portal creation is rate limited");
            cancelMovement(); reason_.clear(); return true;
        }
    }
    return reject("No available town-portal scroll or tome skill has been reported by the server");
}
void RemoteControl::tick() {
    const auto &view = session_.read();
    const auto now = std::chrono::steady_clock::now();
    if (movement_) {
        const auto &world = view.world;
        if (view.stage != OnlineStage::ProtocolReady || view.gameGeneration != movement_->game ||
            !onlineInteractionMatches(movement_->context, view) ||
            world.areaGeneration != movement_->area || !world.playerPosition ||
            !scene_.read().movementAvailable || onlinePlayerDead(world) ||
            world.npcRequested || world.waypointSource ||
            std::any_of(world.items.begin(), world.items.end(), [&](const auto &entry) {
                return entry.second.mode == 4 && entry.second.ownerType == 0 &&
                    entry.second.owner == view.load.playerUnitId;
            }) ||
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
            approach_->deadline=now+std::chrono::seconds(15);
    }
    if (now >= approach_->deadline) {
        cancelApproach(); reason_ = "Server unit approach did not arrive before timeout"; return;
    }
    if (scene_.interactionReady(view, approach_->target)) {
        const bool sent = submitInteraction(approach_->target, approach_->context);
        if (sent) { cancelMovement(); reason_.clear(); }
    }
}
} // namespace d2x
