#include "gameplay/session/session.hpp"
#include <algorithm>
#include <charconv>
#include <iomanip>
#include <sstream>
#include <vector>

namespace d2x {
bool GameSession::assignKashyaHireling() {
    auto &player = simulation_.state_.player;
    if (player.hireling.sourceRow >= 0) return true;
    const auto *seller = monsterContent_.find("kashya");
    if (!seller) return false;
    std::vector<const HirelingDefinition *> eligible;
    int bestLevel = -1;
    for (const auto &entry : content_.hirelings) {
        if (entry.act != 1 || entry.difficulty != state().population.difficulty + 1 ||
            entry.seller != seller->index) continue;
        if (entry.level <= player.level && entry.level > bestLevel) {
            bestLevel = entry.level;
            eligible.clear();
        }
        if (entry.level == bestLevel) eligible.push_back(&entry);
    }
    if (eligible.empty()) {
        for (const auto &entry : content_.hirelings)
            if (entry.act == 1 && entry.difficulty == state().population.difficulty + 1 &&
                entry.seller == seller->index &&
                (bestLevel < 0 || entry.level < bestLevel)) {
                bestLevel = entry.level;
                eligible = {&entry};
            }
    }
    if (eligible.empty()) return false;
    const auto *entry = eligible[size_t(player.combatRandom % eligible.size())];
    auto key = entry->nameFirst;
    if (key.size() == entry->nameLast.size() && key.size() > 2 &&
        key.substr(0, key.size() - 2) == entry->nameLast.substr(0, key.size() - 2)) {
        int first = 0, last = 0;
        auto from = std::from_chars(key.data() + key.size() - 2, key.data() + key.size(), first);
        auto to = std::from_chars(entry->nameLast.data() + key.size() - 2,
                                  entry->nameLast.data() + key.size(), last);
        if (from.ec == std::errc{} && to.ec == std::errc{} && last >= first) {
            std::ostringstream name;
            name << key.substr(0, key.size() - 2) << std::setw(2) << std::setfill('0')
                 << (first + int((player.combatRandom >> 16) % unsigned(last - first + 1)));
            key = name.str();
        }
    }
    player.hireling = {entry->sourceRow, entry->classId, key, entry->level,
                       float(entry->life), player.pos, {1, 0}, {}, false, 0};
    return true;
}

void GameSession::advanceHireling(float dt) {
    auto &hireling = simulation_.state_.player.hireling;
    auto &player = simulation_.state_.player;
    if (!hireling.active() || player.dead || dt <= 0) return;
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
                !map().grid.segment(hireling.pos, enemy.pos)) continue;
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
                auto spread = unsigned(definition->damageMax - definition->damageMin + 1);
                float damage = float(definition->damageMin +
                                     int(uint32_t(player.combatRandom) % spread));
                simulation_.state_.area.missiles.push_back({ids_.allocate(), player.id,
                    hireling.pos, hireling.look * projectile.velocity,
                    projectile.lifetime, Skill::Fireball, true, projectile.id, damage});
                auto &missile = simulation_.state_.area.missiles.back();
                missile.attackElements.playerKillEffects = false;
                missile.attackerLevel = hireling.level;
                missile.attackRating = definition->attackRating;
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
