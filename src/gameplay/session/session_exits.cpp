#include "gameplay/session/session.hpp"
#include <algorithm>

namespace d2x {
namespace {
bool atBoundary(const LevelExit &exit, const MapRecipe &recipe, Vec pos) {
    if (!exit.boundary)
        return false;
    const auto &b = *exit.boundary;
    float lateral = b.side % 2 ? pos.y : pos.x;
    float depth = b.side == 1   ? pos.x
                  : b.side == 2 ? pos.y
                  : b.side == 3 ? recipe.width * 5 - pos.x
                                : recipe.height * 5 - pos.y;
    return depth <= .8f && lateral >= b.start * 5 && lateral < b.end * 5;
}
} // namespace
void GameSession::cancelExit() {
    if (pendingExit_)
        simulation_.stopWalking();
    pendingExit_.reset();
    boundaryMoveTarget_.reset();
}
bool GameSession::routeBoundaryMove(Vec target) {
    const auto &source = region().recipe;
    for (const auto &exit : region().exits) {
        if (!exit.boundary || !exit.enabled)
            continue;
        auto dest = std::find_if(regions_.begin(), regions_.end(),
                                 [&](const auto &r) { return r.definition.id == exit.destination; });
        if (dest == regions_.end())
            continue;
        const auto &r = dest->recipe;
        Vec global = target + Vec{source.worldX * 5.f, source.worldY * 5.f};
        Vec local = global - Vec{r.worldX * 5.f, r.worldY * 5.f};
        if (local.x >= 0 && local.y >= 0 && local.x < r.width * 5 && local.y < r.height * 5) {
            beginExit(exit.slot);
            boundaryMoveTarget_ = global;
            return true;
        }
    }
    return false;
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
                pendingExit_ = exit.slot;
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
            ? atBoundary(*exit, region().recipe, p.pos)
            : ((p.pos - exit->accessPoint).length() <= 2 && map().grid.segment(p.pos, exit->accessPoint))) {
        auto destination = std::find_if(regions_.begin(), regions_.end(),
                                        [&](const auto &r) { return r.definition.id == exit->destination; });
        if (destination == regions_.end()) {
            cancelExit();
            return;
        }
        for (const auto &back : destination->exits)
            if (back.destination == region().definition.id) {
                // Explicit stair activation avoids arrival-triggered warp ping-pong.
                auto target = destination->definition.id;
                Vec arrival = back.arrival;
                auto onward = boundaryMoveTarget_;
                if (exit->boundary && back.boundary) {
                    const auto &a = region().recipe;
                    const auto &b = destination->recipe;
                    Vec translated =
                        p.pos + Vec{float((a.worldX - b.worldX) * 5), float((a.worldY - b.worldY) * 5)};
                    int side = back.boundary->side;
                    if (side % 2)
                        translated.x = side == 1 ? .5f : b.width * 5 - .5f;
                    else
                        translated.y = side == 2 ? .5f : b.height * 5 - .5f;
                    if (destination->map.grid.walkable(translated) &&
                        destination->map.grid.segment(back.arrival, translated))
                        arrival = translated;
                }
                cancelExit();
                enter(target, arrival);
                if (onward)
                    simulation_.execute(MoveTo{
                        *onward - Vec{destination->recipe.worldX * 5.f, destination->recipe.worldY * 5.f}});
                return;
            }
        cancelExit();
    } else if (p.route.empty()) {
        simulation_.emit(InteractionFailed{{}, "Cannot reach these stairs."});
        cancelExit();
    }
}
} // namespace d2x
