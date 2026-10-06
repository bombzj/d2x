#include "remote_control.hpp"
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace d2x {
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
    const int dx = int(movement.goal.x) - player.x, dy = int(movement.goal.y) - player.y;
    if (std::abs(dx) > 50 || std::abs(dy) > 50)
        return reject("Movement target exceeds the native 50-subtile request range");
    // D2MOO PATH_AllocDynamicPath gives players PATHTYPE_STRAIGHT. Its
    // PATH_Straight_Compute only tries AStar within an 18-subtile radius.
    constexpr int nativeDetourRadius = 18;
    if (movement.unit && dx * dx + dy * dy <= nativeDetourRadius * nativeDetourRadius) {
        if (!session_.move_to_unit(*movement.unit, movement.run))
            return reject(view.error ? view.error->message : "Unit movement is rate limited");
        movement.finalUnit = true;
    } else {
        OnlinePoint next = player;
        if (player != movement.goal) {
            const Vec origin{float(binding.origin->x), float(binding.origin->y)};
            const Vec start{float(player.x) - origin.x + .5f, float(player.y) - origin.y + .5f};
            const Vec goal{float(movement.goal.x) - origin.x + .5f, float(movement.goal.y) - origin.y + .5f};
            // Reuse the common map planner. Only its shortcuts opt into the
            // original integer movement ray; offline path policy is unchanged.
            auto route = map->grid.path(start, goal, movement.unit.has_value(), playerMovement, true);
            while (!route.empty() && int(std::floor(route.front().x)) == int(std::floor(start.x)) &&
                int(std::floor(route.front().y)) == int(std::floor(start.y))) route.pop_front();
            if (route.empty()) return reject("No reachable movement route in active native collision");
            const auto delta = route.front() - start;
            const float length = delta.length();
            for (float distance = std::min(float(nativeDetourRadius), length); distance > 0; distance -= 1.f) {
                const auto point = distance >= length ? route.front() : start + delta.unit() * distance;
                const int x = int(std::floor(point.x)) + binding.origin->x;
                const int y = int(std::floor(point.y)) + binding.origin->y;
                const int sx = x - player.x, sy = y - player.y;
                if ((!sx && !sy) || sx * sx + sy * sy > nativeDetourRadius * nativeDetourRadius ||
                    x < 0 || y < 0 || x > UINT16_MAX || y > UINT16_MAX) continue;
                const Vec cell{float(x) - origin.x + .5f, float(y) - origin.y + .5f};
                if (!map->grid.segment(start, cell, {}, playerMovement) ||
                    !map->grid.nativeMovementSegment(start, cell, playerMovement)) continue;
                next = {uint16_t(x), uint16_t(y)}; break;
            }
            if (next == player) return reject("Native movement ray has no reachable route segment");
        }
        if (!session_.move_to(next, movement.run))
            return reject(view.error ? view.error->message : "Movement request is rate limited");
        movement.segment = next;
    }
    movement.requestRevision = view.world.movementRequest ? view.world.movementRequest->revision : 0;
    if (approach_ && movement.unit == approach_->target)
        approach_->requestRevision = movement.requestRevision;
    movement.submitted = std::chrono::steady_clock::now();
    reason_.clear(); return true;
}
bool RemoteControl::move(OnlinePoint point, bool run) {
    scene_.update(session_.read());
    const auto player = session_.read().world.playerPosition;
    if (!player || std::abs(int(player->x) - point.x) > 50 || std::abs(int(player->y) - point.y) > 50)
        return reject("Movement exceeds the native 50-subtile request range");
    if (!scene_.permits(session_.read(), point)) return reject("Movement destination is outside active player collision");
    if (movement_ && !movement_->unit && movement_->goal == point && movement_->run == run &&
        movement_->game == session_.read().gameGeneration && movement_->area == session_.read().world.areaGeneration)
        return true;
    const auto now = std::chrono::steady_clock::now();
    Movement movement; movement.goal = point; movement.run = run;
    movement.game = session_.read().gameGeneration; movement.area = session_.read().world.areaGeneration;
    movement.deadline = now + std::chrono::seconds(15);
    if (!submitSegment(movement)) return false;
    movement_ = std::move(movement); approach_.reset(); reason_.clear(); return true;
}
bool RemoteControl::moveToUnit(OnlineUnitKey target, bool run) {
    scene_.update(session_.read());
    if (!scene_.permitsInteraction(session_.read(), target)) return reject("Target is not assigned in the current scene");
    const auto player = *session_.read().world.playerPosition;
    const auto point = *session_.read().world.units.at(target).position;
    if (std::abs(int(player.x) - point.x) > 50 || std::abs(int(player.y) - point.y) > 50)
        return reject("Target exceeds the native 50-subtile request range");
    const auto now = std::chrono::steady_clock::now();
    Movement movement; movement.goal = point; movement.unit = target; movement.run = run;
    movement.game = session_.read().gameGeneration; movement.area = session_.read().world.areaGeneration;
    movement.deadline = now + std::chrono::seconds(15);
    if (!submitSegment(movement)) return false;
    if (movement.finalUnit) movement_.reset();
    else movement_ = std::move(movement);
    approach_.reset(); reason_.clear(); return true;
}
bool RemoteControl::interact(OnlineUnitKey target, bool run) {
    scene_.update(session_.read());
    const auto &world = session_.read();
    if (!scene_.permitsInteraction(world, target)) return reject("Target is not assigned in the current scene");
    const auto player = *world.world.playerPosition;
    const auto point = *world.world.units.at(target).position;
    if (std::abs(int(player.x) - point.x) > 50 || std::abs(int(player.y) - point.y) > 50)
        return reject("Target exceeds the native 50-subtile request range");
    if (target.type != 1 && target.type != 0) {
        const auto entry = std::find_if(scene_.read().mapTargets.begin(), scene_.read().mapTargets.end(),
            [&](const auto &value) { return value.unit == target; });
        const bool stash = entry != scene_.read().mapTargets.end() && entry->interaction == OnlineMapInteraction::Stash;
        if (!session_.interact_map_unit(target, stash)) return reject(world.error ? world.error->message : "Interaction is rate limited");
        cancelMovement(); reason_.clear(); return true;
    }
    if ((target.type == 1 && world.world.npcRequested == target.id) || (approach_ && approach_->target == target))
        return reject("Unit interaction is already pending or open");
    if (scene_.interactionReady(world, target)) {
        const bool sent = target.type == 0 ? session_.interact_map_unit(target) : session_.interact_npc(target.id);
        if (!sent) return reject(world.error ? world.error->message : "Unit interaction is rate limited");
        cancelMovement(); reason_.clear(); return true;
    }
    // Native 0x13 ignores distant NPCs. Native 0x02/04 keeps following their server GUID.
    if (!moveToUnit(target, run)) return false;
    approach_ = Approach{target, world.gameGeneration, world.world.areaGeneration,
        world.world.movementRequest ? world.world.movementRequest->revision : 0,
        std::chrono::steady_clock::now() + std::chrono::seconds(15)};
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
            world.areaGeneration != movement_->area || !world.playerPosition ||
            !scene_.read().movementAvailable || onlinePlayerDead(world) ||
            world.npcRequested || world.waypointSource ||
            std::any_of(world.items.begin(), world.items.end(), [&](const auto &entry) {
                return entry.second.mode == 4 && entry.second.ownerType == 0 &&
                    entry.second.owner == view.load.playerUnitId;
            }) ||
            !world.movementRequest || world.movementRequest->revision != movement_->requestRevision) {
            cancelMovement();
        } else if (now >= movement_->deadline) {
            cancelMovement(); reason_ = "Server movement did not arrive before timeout";
        } else {
            auto &movement = *movement_;
            const auto player = *world.playerPosition;
            if (!movement.unit && player == movement.goal) {
                movement_.reset(); reason_.clear();
            } else {
                const bool atSegment = movement.segment && (movement.unit || movement.segment != movement.goal) &&
                    std::abs(int(player.x) - movement.segment->x) <= 1 &&
                    std::abs(int(player.y) - movement.segment->y) <= 1;
                // PlrMsg can defer 0x96 until position/stamina changes qualify.
                // A missing sample after one second is not a stopped path: a
                // walk segment itself may take longer. Reissuing it restarts
                // native path computation and the display prediction.
                if (atSegment && now - movement.submitted >= std::chrono::milliseconds(100)) {
                    // Plan from the native position, never from the sprite's
                    // predicted endpoint. Doors and moving targets may have changed.
                    if (!submitSegment(movement)) {
                        const bool rateLimited = !view.error &&
                            (reason_ == "Movement request is rate limited" || reason_ == "Unit movement is rate limited");
                        if (!rateLimited) cancelMovement();
                    } else if (movement.finalUnit) {
                        // The final native GUID command follows a moving target;
                        // the NPC approach below still waits for actual proximity.
                        movement_.reset();
                    }
                }
            }
        }
    }
    if (!approach_) return;
    if (view.stage != OnlineStage::ProtocolReady || view.gameGeneration != approach_->game ||
        view.world.areaGeneration != approach_->area || view.world.waypointSource ||
        view.world.npcRequested || !view.world.movementRequest ||
        view.world.movementRequest->revision != approach_->requestRevision ||
        !scene_.permitsInteraction(view, approach_->target)) {
        cancelApproach(); return;
    }
    if (now >= approach_->deadline) {
        cancelApproach(); reason_ = "Server unit approach did not arrive before timeout"; return;
    }
    if (scene_.interactionReady(view, approach_->target)) {
        const bool sent = approach_->target.type == 0 ? session_.interact_map_unit(approach_->target)
                                                     : session_.interact_npc(approach_->target.id);
        if (sent) { cancelMovement(); reason_.clear(); }
    }
}
} // namespace d2x
