#include "gameplay/session/session.hpp"
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
std::vector<std::pair<int, Vec>> GameSession::sceneRegions() const {
    std::vector<std::pair<int, Vec>> result{{current_, {}}};
    const auto &origin = region().recipe;
    for (int index = 0; index < int(regions_.size()); ++index) {
        const auto &candidate = regions_[index];
        if (index == current_ ||
            std::none_of(origin.boundaries.begin(), origin.boundaries.end(), [&](const auto &boundary) {
                return boundary.destination == int(candidate.definition.id);
            })) continue;
        result.push_back({index, {float((candidate.recipe.worldX - origin.worldX) * 5),
                                  float((candidate.recipe.worldY - origin.worldY) * 5)}});
    }
    return result;
}
bool GameSession::roomVisible(int index, Vec position) const {
    if (index == current_) return active(position);
    const auto &candidate = regions_.at(index);
    const auto &origin = region().recipe;
    if (std::none_of(origin.boundaries.begin(), origin.boundaries.end(), [&](const auto &boundary) {
            return boundary.destination == int(candidate.definition.id);
        })) return false;
    const auto *observer = map().activation.room(state().player.pos);
    const auto *target = candidate.map.activation.room(position);
    if (!observer || !target) return false;
    // DRLG propagates room visibility across continuous level boundaries too.
    // Compare the actual rooms in one coordinate space; do not clamp the observer
    // to the nearest room of a different level (which would reveal distant rooms).
    const int dx = (candidate.recipe.worldX - origin.worldX) * 5;
    const int dy = (candidate.recipe.worldY - origin.worldY) * 5;
    return observer->x <= target->x + dx + target->width &&
           target->x + dx <= observer->x + observer->width &&
           observer->y <= target->y + dy + target->height &&
           target->y + dy <= observer->y + observer->height;
}
void GameSession::cancelExit() {
    if (pendingExit_)
        simulation_.stopWalking();
    pendingExit_.reset();
    boundaryMoveTarget_.reset();
    boundaryPassage_.reset();
}
bool GameSession::beginBoundaryExit(const LevelExit &exit, std::optional<Vec> target) {
    if (!exit.enabled || !exit.boundary || state().player.dead) return false;
    const auto destination = std::find_if(regions_.begin(), regions_.end(),
        [&](const Region &candidate) { return candidate.definition.id == exit.destination; });
    if (destination == regions_.end()) return false;
    const bool approachBlockedTarget = target && !destination->map.grid.walkable(*target);
    const Vec start = state().player.pos;
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
        const auto approach = map().grid.path(start, passage.departure);
        if (approach.empty()) continue;
        float length = routeLength(start, approach);
        if (!approachBlockedTarget && length >= best) continue;
        float gap = 0;
        if (target) {
            const auto onward = destination->map.grid.path(passage.arrival, *target, approachBlockedTarget);
            if (onward.empty() && (!approachBlockedTarget || !destination->map.grid.walkable(passage.arrival))) continue;
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
    simulation_.execute(MoveTo{selected->departure});
    return true;
}
bool GameSession::routeBoundaryMove(Vec target) {
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || state().player.dead) return false;
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
        auto dest = std::find_if(regions_.begin(), regions_.end(),
                                 [&](const auto &r) { return r.definition.id == exit.destination; });
        if (dest == regions_.end())
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
        simulation_.stopWalking();
        simulation_.emit(InteractionFailed{{}, "No walkable route to the adjoining ground."});
    }
    return adjoiningTarget;
}
void GameSession::beginExit(int slot) {
    if (pendingExit_ == slot || state().player.dead)
        return;
    cancelExit();
    cancelPickup();
    cancelInteraction();
    closeStorage();
    auto exit = std::find_if(region().exits.begin(), region().exits.end(),
                             [slot](const auto &e) { return e.slot == slot; });
    if (exit == region().exits.end())
        return;
    if (!exit->enabled) {
        simulation_.emit(InteractionFailed{{}, "This destination is not implemented yet."});
        return;
    }
    if (exit->boundary) {
        if (!beginBoundaryExit(*exit, std::nullopt))
            simulation_.emit(InteractionFailed{{}, "No reachable passage to this area."});
        return;
    }
    pendingExit_ = slot;
    simulation_.execute(MoveTo{exit->accessPoint});
}
void GameSession::updateExit() {
    if (!pendingExit_ && !state().player.dead) {
        const auto &p = state().player;
        for (const auto &exit : region().exits) {
            if (!exit.enabled || !exit.boundary)
                continue;
            constexpr int outwardX[]{0, -1, 0, 1}, outwardY[]{1, 0, -1, 0};
            int side = exit.boundary->side;
            bool outward = p.moving && p.look.x * outwardX[side] + p.look.y * outwardY[side] > 0;
            if (outward && atBoundary(exit, region().recipe, p.pos)) {
                const auto passage = std::find_if(exit.passages.begin(), exit.passages.end(),
                    [&](const auto &candidate) { return atPassage(exit, region().recipe, p.pos, candidate); });
                if (passage == exit.passages.end()) continue;
                pendingExit_ = exit.slot;
                boundaryPassage_ = *passage;
                break;
            }
        }
    }
    if (!pendingExit_)
        return;
    if (state().player.dead) {
        cancelExit();
        return;
    }
    auto exit = std::find_if(region().exits.begin(), region().exits.end(),
                             [&](const auto &e) { return e.slot == *pendingExit_; });
    if (exit == region().exits.end() || !exit->enabled) {
        cancelExit();
        return;
    }
    const auto &p = state().player;
    if (p.castTime > 0 || p.leapTime > 0 || p.spinTime > 0 || p.meleeTime > 0)
        return;
    if (exit->boundary
            ? boundaryPassage_ && atPassage(*exit, region().recipe, p.pos, *boundaryPassage_)
            : ((p.pos - exit->accessPoint).length() <= 2 && map().grid.segment(p.pos, exit->accessPoint))) {
        auto destination = std::find_if(regions_.begin(), regions_.end(),
                                        [&](const auto &r) { return r.definition.id == exit->destination; });
        if (destination == regions_.end()) {
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
                        translated.y += p.pos.y - boundaryPassage_->departure.y;
                    else
                        translated.x += p.pos.x - boundaryPassage_->departure.x;
                    if (!atBoundary(back, b, translated) || !destination->map.grid.walkable(translated)) {
                        cancelExit();
                        simulation_.emit(InteractionFailed{{}, "The adjoining ground is blocked."});
                        return;
                    }
                    arrival = translated;
                }
                cancelExit();
                enter(target, arrival, coordinateOffset);
                if (onward)
                    simulation_.execute(MoveTo{
                        *onward - Vec{destination->recipe.worldX * 5.f, destination->recipe.worldY * 5.f}});
                return;
            }
        cancelExit();
    } else if (p.route.empty()) {
        simulation_.emit(InteractionFailed{{}, exit->boundary ? "Cannot reach this boundary passage." :
                                                                "Cannot reach these stairs."});
        cancelExit();
    }
}
} // namespace d2x
