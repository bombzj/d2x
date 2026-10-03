#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include <algorithm>
#include <limits>

namespace d2x {
namespace {
bool atBoundary(const LevelExit &exit, const MapRecipe &recipe, Vec pos) {
    if (!exit.boundary)
        return false;
    const auto &b = *exit.boundary;
    float lateral = b.side % 2 ? pos.y : pos.x;
    float plane = b.coordinate(recipe.width, recipe.height) * 5.f;
    float depth = b.side == 1   ? pos.x - plane
                  : b.side == 2 ? pos.y - plane
                  : b.side == 3 ? plane - pos.x
                                : plane - pos.y;
    return std::abs(depth) <= .8f && lateral >= b.start * 5 && lateral < b.end * 5;
}
bool atPassage(const LevelExit &exit, const MapRecipe &recipe, Vec pos,
               const LevelExit::BoundaryPassage &passage) {
    if (!atBoundary(exit, recipe, pos)) return false;
    return exit.boundary->side % 2 ? int(std::floor(pos.y)) == int(std::floor(passage.departure.y)) :
        int(std::floor(pos.x)) == int(std::floor(passage.departure.x));
}
float routeLength(Vec from, const std::deque<Vec> &route) {
    float length = 0;
    for (const auto point : route) {
        length += (point - from).length();
        from = point;
    }
    return length;
}
} // namespace
std::vector<std::pair<int, Vec>> GameSessionImpl::sceneRegions() const {
    return world_.sceneRegions(state().area.region);
}
bool GameSessionImpl::roomVisible(int index, Vec position) const {
    if (index == current_) return active(position);
    return world_.visibleRoom(state().area.region, state().player.movement.pos,
        world_.at(size_t(index)).definition.id, position);
}
void GameSessionImpl::cancelExit() {
    if (pendingExit_)
        simulation_->stopWalking();
    pendingExit_.reset();
    boundaryMoveTarget_.reset();
    boundaryPassage_.reset();
}
bool GameSessionImpl::beginBoundaryExit(const LevelExit &exit, std::optional<Vec> target) {
    if (!exit.enabled || !exit.boundary || state().player.actions.dead || !questExitAllowed(exit.destination)) return false;
    const auto destination = std::find_if(world_.regions().begin(), world_.regions().end(),
        [&](const Region &candidate) { return candidate.definition.id == exit.destination; });
    if (destination == world_.regions().end()) return false;
    const bool approachBlockedTarget = target && !destination->map.grid.walkable(*target, playerMovement);
    const Vec start = state().player.movement.pos;
    auto estimate = [&](const LevelExit::BoundaryPassage &passage) {
        return (passage.departure - start).length() +
            (target ? (*target - passage.arrival).length() : 0.f);
    };
    auto passages = exit.passages;
    std::stable_sort(passages.begin(), passages.end(),
        [&](const auto &left, const auto &right) { return estimate(left) < estimate(right); });
    std::optional<LevelExit::BoundaryPassage> selected;
    float best = std::numeric_limits<float>::infinity();
    float bestGap = std::numeric_limits<float>::infinity();
    for (const auto &passage : passages) {
        if (!approachBlockedTarget && estimate(passage) >= best) break;
        const auto approach = map().grid.path(start, passage.departure, false, playerMovement);
        if (approach.empty()) continue;
        float length = routeLength(start, approach);
        if (!approachBlockedTarget && length >= best) continue;
        float gap = 0;
        if (target) {
            const auto onward = destination->map.grid.path(passage.arrival, *target, approachBlockedTarget, playerMovement);
            if (onward.empty() && (!approachBlockedTarget || !destination->map.grid.walkable(passage.arrival, playerMovement))) continue;
            length += routeLength(passage.arrival, onward);
            gap = ((onward.empty() ? passage.arrival : onward.back()) - *target).length();
        }
        if (gap < bestGap || (gap == bestGap && length < best)) {
            bestGap = gap;
            best = length;
            selected = passage;
        }
    }
    if (!selected) return false;
    cancelExit();
    cancelPickup();
    cancelInteraction();
    closeStorage();
    pendingExit_ = exit.slot;
    boundaryPassage_ = selected;
    if (target)
        boundaryMoveTarget_ = *target + Vec{destination->recipe.worldX * 5.f,
                                           destination->recipe.worldY * 5.f};
    simulation_->execute(MoveTo{selected->departure});
    return true;
}
bool GameSessionImpl::routeBoundaryMove(Vec target) {
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || state().player.actions.dead) return false;
    const auto &source = region().recipe;
    bool adjoiningTarget = false;
    for (const auto &exit : region().exits) {
        if (!exit.boundary || !exit.enabled)
            continue;
        const auto &boundary = *exit.boundary;
        const float plane = boundary.coordinate(source.width, source.height) * 5.f;
        const float depth = boundary.side == 1 ? target.x - plane :
            boundary.side == 2 ? target.y - plane :
            boundary.side == 3 ? plane - target.x : plane - target.y;
        if (depth >= 0) continue;
        auto dest = std::find_if(world_.regions().begin(), world_.regions().end(),
                                 [&](const auto &r) { return r.definition.id == exit.destination; });
        if (dest == world_.regions().end())
            continue;
        const auto &r = dest->recipe;
        Vec global = target + Vec{source.worldX * 5.f, source.worldY * 5.f};
        Vec local = global - Vec{r.worldX * 5.f, r.worldY * 5.f};
        if (local.x >= 0 && local.y >= 0 && local.x < r.width * 5 && local.y < r.height * 5) {
            adjoiningTarget = true;
            if (beginBoundaryExit(exit, local)) return true;
        }
    }
    if (adjoiningTarget) {
        cancelExit();
        simulation_->stopWalking();
        simulation_->emit(InteractionFailed{{}, "No walkable route to the adjoining ground."});
    }
    return adjoiningTarget;
}
bool GameSessionImpl::questExitAllowed(RegionId destination) const {
    if (int(region().definition.id) == 40 && int(destination) == 50)
        return quest(QuestId::ArcaneSanctuary).stage > 0;
    if (int(destination) == 73)
        return quest(QuestId::HoradricStaff).stage >= 6 && !tombOpeningFrame_ &&
            int(region().definition.id) == actTwoTombs(state().mapSeed)[0];
    return true;
}
void GameSessionImpl::beginExit(int slot) {
    if (pendingExit_ == slot || state().player.actions.dead)
        return;
    cancelExit();
    cancelPickup();
    cancelInteraction();
    closeStorage();
    auto exit = std::find_if(region().exits.begin(), region().exits.end(),
                             [slot](const auto &e) { return e.slot == slot; });
    if (exit == region().exits.end())
        return;
    if (!questExitAllowed(exit->destination)) {
        simulation_->emit(InteractionFailed{{}, "This passage is still sealed by its quest."});
        return;
    }
    if (!exit->enabled) {
        simulation_->emit(InteractionFailed{{}, "This destination is not implemented yet."});
        return;
    }
    if (exit->stairObject) {
        const auto *stair = object(exit->stairObject);
        if (!stair) return;
        if (stair->modeAt(state().time) != 2) {
            interact(stair->id);
            return;
        }
    }
    if (exit->boundary) {
        if (!beginBoundaryExit(*exit, std::nullopt))
            simulation_->emit(InteractionFailed{{}, "No reachable passage to this area."});
        return;
    }
    pendingExit_ = slot;
    simulation_->execute(MoveTo{exit->accessPoint});
}
void GameSessionImpl::updateExit() {
    if (!pendingExit_ && !state().player.actions.dead) {
        const auto &p = state().player;
        for (const auto &exit : region().exits) {
            if (!exit.enabled || !exit.boundary || !questExitAllowed(exit.destination))
                continue;
            constexpr int outwardX[]{0, -1, 0, 1}, outwardY[]{1, 0, -1, 0};
            int side = exit.boundary->side;
            bool outward = p.movement.moving && p.movement.look.x * outwardX[side] + p.movement.look.y * outwardY[side] > 0;
            if (outward && atBoundary(exit, region().recipe, p.movement.pos)) {
                const auto passage = std::find_if(exit.passages.begin(), exit.passages.end(),
                    [&](const auto &candidate) { return atPassage(exit, region().recipe, p.movement.pos, candidate); });
                if (passage == exit.passages.end()) continue;
                pendingExit_ = exit.slot;
                boundaryPassage_ = *passage;
                break;
            }
        }
    }
    if (!pendingExit_)
        return;
    if (state().player.actions.dead) {
        cancelExit();
        return;
    }
    auto exit = std::find_if(region().exits.begin(), region().exits.end(),
                             [&](const auto &e) { return e.slot == *pendingExit_; });
    if (exit == region().exits.end() || !exit->enabled || !questExitAllowed(exit->destination)) {
        cancelExit();
        return;
    }
    const auto &p = state().player;
    if (p.actions.castTime > 0 || p.actions.meleeTime > 0)
        return;
    if (exit->boundary
            ? boundaryPassage_ && atPassage(*exit, region().recipe, p.movement.pos, *boundaryPassage_)
            : ((p.movement.pos - exit->accessPoint).length() <= 2 && map().grid.segment(p.movement.pos, exit->accessPoint))) {
        const auto destinationId = exit->destination;
        const auto exitSlot = exit->slot;
        ensureRegion(destinationId, true);
        exit = std::find_if(region().exits.begin(), region().exits.end(),
            [&](const auto &candidate) { return candidate.slot == exitSlot; });
        if (exit == region().exits.end()) { cancelExit(); return; }
        auto destination = std::find_if(world_.regions().begin(), world_.regions().end(),
                                        [&](const auto &r) { return r.definition.id == exit->destination; });
        if (destination == world_.regions().end()) {
            cancelExit();
            return;
        }
        for (const auto &back : destination->exits)
            if (back.destination == region().definition.id &&
                bool(back.boundary) == bool(exit->boundary) &&
                (!exit->boundary || (back.boundary->side == (exit->boundary->side + 2) % 4 &&
                    boundaryPassage_ && atBoundary(back, destination->recipe, boundaryPassage_->arrival)))) {
                // Explicit stair activation avoids arrival-triggered warp ping-pong.
                auto target = destination->definition.id;
                Vec arrival = back.arrival;
                auto onward = boundaryMoveTarget_;
                std::optional<Vec> coordinateOffset;
                if (exit->boundary && back.boundary) {
                    const auto &a = region().recipe;
                    const auto &b = destination->recipe;
                    coordinateOffset = Vec{float((a.worldX - b.worldX) * 5), float((a.worldY - b.worldY) * 5)};
                    Vec translated = boundaryPassage_->arrival;
                    if (exit->boundary->side % 2)
                        translated.y += p.movement.pos.y - boundaryPassage_->departure.y;
                    else
                        translated.x += p.movement.pos.x - boundaryPassage_->departure.x;
                    if (!atBoundary(back, b, translated) || !destination->map.grid.walkable(translated, playerMovement)) {
                        cancelExit();
                        simulation_->emit(InteractionFailed{{}, "The adjoining ground is blocked."});
                        return;
                    }
                    arrival = translated;
                }
                cancelExit();
                enter(target, arrival, coordinateOffset);
                if (onward)
                    simulation_->execute(MoveTo{
                        *onward - Vec{destination->recipe.worldX * 5.f, destination->recipe.worldY * 5.f}});
                return;
            }
        cancelExit();
    } else if (p.movement.route.empty()) {
        simulation_->emit(InteractionFailed{{}, exit->boundary ? "Cannot reach this boundary passage." :
                                                                "Cannot reach these stairs."});
        cancelExit();
    }
}
} // namespace d2x
