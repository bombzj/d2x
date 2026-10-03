#include "gameplay/units/restoration.hpp"
#include "gameplay/units/impairments.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include <algorithm>

namespace d2x {
bool GameSessionImpl::assignKashyaHireling() {
    if (state().player.hireling.sourceRow >= 0) return true;
    for (const auto &npc : region().objects) {
        if (npc.npcClass != "kashya" || !ensureHirelingOffers(npc.id)) continue;
        auto &offers = hirelingOffers_.at(npc.id);
        assignHireling(offers.front());
        offers.erase(offers.begin());
        return true;
    }
    return false;
}
void GameSessionImpl::advanceHireling(float dt) {
    auto &merc = simulation_->state_.player.hireling;
    if (merc.sourceRow < 0 || dt <= 0) return;
    if (!merc.active()) { merc.deathAge += dt; return; }
    merc.combatEffects.expire(state().frame);
    const auto *definition = hirelingDefinition();
    const MonsterRecord *actor = nullptr;
    for (const auto &[id, entry] : monsterContent_.monsters())
        if (entry.index == merc.classId) { actor = &entry; break; }
    if (!actor || !definition || !actor->walkVelocity) return;
    merc.collisionSize = actor->collisionSize;
    if (const auto *motion = monsterContent_.hirelingMotion(merc.classId, "gh"); actor->getHitMode && motion)
        merc.baseHitDuration = motion->duration;
    if (const auto *motion = monsterContent_.hirelingMotion(merc.classId, "dt")) merc.deathDuration = motion->duration;
    const auto stats = hirelingStats();
    merc.hp = std::min(merc.hp, float(stats.base.life));
    merc.hitTime = std::max(0.f, merc.hitTime - dt);
    advanceImpairments({&merc.chill, nullptr, nullptr, nullptr,
                       {&merc.webSlowRemaining, &merc.webSlowPercent}},
                      dt, WebSlowExpiry::RetainMetadata);
    const auto base = deriveHirelingStats(*definition, merc.level);
    restoreRegeneratingLife(merc.healing, merc.hp, stats.base.life, base.life,
                           stats.combat.replenishLife, dt);
    if (merc.hitTime > 0) { merc.moving = false; return; }
    const auto *timing = monsterContent_.hirelingAttackTiming(merc.classId);
    if (merc.attack) {
        advanceHirelingAttack(*actor, stats);
        return;
    }
    controlHireling(*actor, stats, timing, dt);
}
} // namespace d2x
