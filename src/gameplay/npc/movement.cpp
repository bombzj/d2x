#include "gameplay/session/session.hpp"
#include <algorithm>

namespace d2x {
namespace {
uint32_t roll(uint64_t &state, uint32_t bound) {
    state = uint64_t(uint32_t(state)) * 0x6ac690c5ULL + (state >> 32);
    return uint32_t(state) % bound;
}
} // namespace

void GameSession::advanceNpcPaths(float dt) {
    if (dt <= 0)
        return;
    auto &active = regions_[current_];
    const auto &grid = active.map.grid;
    for (auto &npc : active.objects) {
        if (npc.npcPath.empty() || npc.npcVelocity <= 0 || npc.id == pendingInteraction_ ||
            npc.id == engagedNpc_)
            continue;
        if (!npc.npcRandom)
            npc.npcRandom = (uint64_t(state().mapSeed) << 32) | npc.id.value;
        npc.npcWait = std::max(0.f, npc.npcWait - dt);
        if (npc.npcWait > 0)
            continue;
        if (npc.npcRoute.empty()) {
            // The original Npc AI considers map paths with a 66% roll at each think.
            if (roll(npc.npcRandom, 100) >= 66) {
                npc.npcWait = 8.f / 25.f;
                continue;
            }
            npc.npcTarget = int(roll(npc.npcRandom, uint32_t(npc.npcPath.size())));
            auto target = npc.npcPath[size_t(npc.npcTarget)].position;
            if ((target - npc.pos).length() < .01f) {
                npc.npcWait = 8.f / 25.f;
                continue;
            }
            npc.npcRoute = grid.path(npc.pos, target);
            // A map AI action lasts at most 12 thoughts in the original NPC AI.
            // Keep the route inside that budget instead of crossing the whole map.
            if (npc.npcRoute.size() > 12)
                npc.npcRoute.resize(12);
            npc.npcRoute.erase(std::find_if(npc.npcRoute.begin(), npc.npcRoute.end(),
                [&](Vec point) { return (point - npc.npcHome).length() > 8.f; }),
                npc.npcRoute.end());
            if (npc.npcRoute.empty()) {
                npc.npcWait = 8.f / 25.f;
                continue;
            }
        }
        while (!npc.npcRoute.empty() && (npc.npcRoute.front() - npc.pos).length() < .01f)
            npc.npcRoute.pop_front();
        if (npc.npcRoute.empty()) {
            npc.npcWait = 120.f / 25.f;
            continue;
        }
        Vec delta = npc.npcRoute.front() - npc.pos;
        float distance = delta.length();
        npc.npcLook = delta.unit();
        Vec next = npc.pos + npc.npcLook * std::min(distance, npc.npcVelocity * dt);
        if ((next - npc.npcHome).length() > 8.f || !grid.walkable(next) ||
            !grid.segment(npc.pos, next)) {
            npc.npcRoute.clear();
            npc.npcWait = 120.f / 25.f;
            continue;
        }
        npc.pos = next;
        npc.accessPoint = grid.nearest(next);
        if ((npc.npcRoute.front() - npc.pos).length() < .01f) {
            npc.npcRoute.pop_front();
            if (npc.npcRoute.empty())
                npc.npcWait = 120.f / 25.f;
        }
    }
}
} // namespace d2x
