#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
void GameSessionImpl::updatePrisonerObjects() {
    std::vector<Vec> portalPositions;
    if (int(region().definition.id) == 111 && quest(QuestId::RescueOnMountArreat).stage < 3) {
        const auto *prisoner = monsterContent_.find("act5pow");
        if (prisoner && !prisoner->hostile() && prisoner->walkVelocity && *prisoner->walkVelocity > 0) {
            for (auto &npc : world_.at(size_t(current_)).objects) {
                if (npc.npcClass != "act5pow" || npc.questHidden) continue;
                if (!npc.questEscape) {
                    for (const auto &door : state().area.enemies)
                        if (door.identity.monster == "prisondoor" && door.hp <= 0 && (door.pos - npc.pos).length() < 15) {
                            npc.questEscape = map().grid.nearest(door.pos + Vec{2, 0}, npc.npcMovement);
                            portalPositions.push_back(*npc.questEscape);
                            npc.npcRoute = map().grid.path(npc.pos, *npc.questEscape, false, npc.npcMovement); break;
                        }
                }
                if (!npc.questEscape || (npc.questTimer && *npc.questTimer == state().frame)) continue;
                npc.questTimer = state().frame;
                if (!npc.npcRoute.empty()) {
                    const auto delta = npc.npcRoute.front() - npc.pos;
                    const float distance = delta.length(), step = float(*prisoner->walkVelocity) / 16.f;
                    npc.npcLook = delta;
                    npc.pos = distance <= step ? npc.npcRoute.front() : npc.pos + delta * (step / distance);
                    npc.accessPoint = npc.pos;
                    if (distance <= step) npc.npcRoute.pop_front();
                }
                if ((npc.pos - *npc.questEscape).length() > 1) continue;
                npc.questHidden = true;
                auto &record = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty))[questIndex(QuestId::RescueOnMountArreat)];
                record.flags = std::min(15u, record.flags + 1);
                if (record.flags == 15) { record.stage = 3; simulation_->emit(QuestAdvanced{QuestId::RescueOnMountArreat, 3}); }
            }
        }
    }
    if (int(region().definition.id) != 111) return;
    auto &objects = world_.at(size_t(current_)).objects;
    for (auto point : portalPositions) {
        if (std::any_of(objects.begin(), objects.end(), [&](const auto &o) {
            return o.contentKey == "quest.prisoner.portal" && (o.pos - point).length() < 1;
        })) continue;
        WorldObject portal;
        portal.id = ids_.allocate(); portal.act = 4; portal.objectClass = 189;
        portal.pos = portal.accessPoint = point; portal.contentKey = "quest.prisoner.portal";
        portal.appearance = {"objects", {}, "on", "hth", {}};
        configureWorldObject(portal, questObjectRows_);
        portal.interaction = Interaction::None; portal.setAnimationMode(1, state().time);
        portal.questTimer = state().frame + 25;
        objects.push_back(std::move(portal));
    }
    // ACT5Q2_UpdatePortalMode: 25-frame updates, six opened updates after
    // the group escapes, then SPECIAL1 and SPECIAL2. This is not a player exit.
    for (auto &portal : objects) {
        if (portal.contentKey != "quest.prisoner.portal" || !portal.questTimer || state().frame < *portal.questTimer) continue;
        if (portal.animationMode == 1) portal.setAnimationMode(2, state().time);
        else if (portal.animationMode == 2) {
            const auto escaped = std::count_if(objects.begin(), objects.end(), [&](const auto &npc) {
                return npc.npcClass == "act5pow" && npc.questEscape && (*npc.questEscape - portal.pos).length() < 1 && npc.questHidden;
            });
            if (escaped == 5 && ++portal.questHits > 5) portal.setAnimationMode(3, state().time);
        } else if (portal.animationMode == 3) portal.setAnimationMode(4, state().time);
        portal.questTimer = state().frame + 25;
    }
}
} // namespace d2x
