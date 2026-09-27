#include "gameplay/session/session.hpp"
#include <algorithm>

namespace d2x {
bool GameSession::assignKashyaHireling() {
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
void GameSession::advanceHireling(float dt) {
    auto &hireling = simulation_.state_.player.hireling;
    auto &player = simulation_.state_.player;
    if (!hireling.active() || player.dead || dt <= 0) return;
    const auto stats = hirelingStats();
    hireling.hp = std::min(float(stats.base.life), hireling.hp +
        (stats.base.life * 25.f / 2000.f + stats.combat.replenishLife * 25.f / 256.f) * dt);
    hireling.attackTimer = std::max(0.f, hireling.attackTimer - dt);
    const HirelingDefinition *definition = nullptr;
    for (const auto &entry : content_.hirelings)
        if (entry.sourceRow == hireling.sourceRow) { definition = &entry; break; }
    const MonsterRecord *actor = nullptr;
    for (const auto &[id, entry] : monsterContent_.monsters())
        if (entry.index == hireling.classId) { actor = &entry; break; }
    const auto *timing = monsterContent_.hirelingAttackTiming(hireling.classId);
    if (definition && actor && actor->attack1Projectile && timing) {
        const auto &projectile = *actor->attack1Projectile;
        const Enemy *target = nullptr;
        float closest = projectile.velocity * projectile.lifetime;
        for (const auto &enemy : state().area.enemies) {
            if (enemy.hp <= 0 || !simulation_.active(enemy.pos) ||
                !simulation_.missilePathClear(projectile.id, hireling.pos, enemy.pos)) continue;
            float distance = (enemy.pos - hireling.pos).length();
            if (distance < closest) { closest = distance; target = &enemy; }
        }
        if (target) {
            hireling.route.clear();
            hireling.moving = false;
            hireling.look = (target->pos - hireling.pos).unit();
            if (hireling.attackTimer <= 0) {
                player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                                      (player.combatRandom >> 32);
                auto spread = unsigned(stats.weapon.maximum - stats.weapon.minimum + 1);
                float damage = float(stats.weapon.minimum +
                                     int(uint32_t(player.combatRandom) % spread)) / 256.f;
                simulation_.state_.area.missiles.push_back({ids_.allocate(), player.id,
                    hireling.pos, hireling.look * projectile.velocity,
                    projectile.lifetime, SkillBehavior::None, true, projectile.id, damage});
                auto &missile = simulation_.state_.area.missiles.back();
                missile.attackElements = simulation_.rollAttackElements(stats.weapon.item, &stats.combat);
                missile.attackElements.playerKillEffects = false;
                missile.attackElements.ranged = true;
                missile.attackElements.attackerLevel = hireling.level;
                missile.attackElements.lifeLeech = missile.attackElements.manaLeech = 0;
                missile.attackerLevel = hireling.level;
                missile.attackRating = stats.base.attackRating;
                hireling.attackTimer = timing->duration;
            }
            return;
        }
    }
    if ((hireling.pos - player.pos).length() > 12.f) {
        auto path = map().grid.path(hireling.pos, player.pos);
        if (path.empty()) {
            hireling.moving = false;
            return;
        }
        hireling.route = std::move(path);
    } else if (hireling.route.empty() && (hireling.pos - player.pos).length() > 2.5f)
        hireling.route = map().grid.path(hireling.pos, player.pos);
    if (hireling.route.empty()) {
        hireling.moving = false;
        return;
    }
    auto target = hireling.route.front();
    auto delta = target - hireling.pos;
    float distance = delta.length();
    if (distance < .05f) {
        hireling.route.pop_front();
        hireling.moving = !hireling.route.empty();
        return;
    }
    auto step = std::min(distance, 4.f * dt);
    hireling.look = delta * (1.f / distance);
    auto next = hireling.pos + hireling.look * step;
    if (map().grid.segment(hireling.pos, next)) {
        hireling.pos = next;
        hireling.moving = true;
    } else {
        hireling.route.clear();
        hireling.moving = false;
    }
}
} // namespace d2x
