#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x::server::ai {
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked = false;
    std::erase_if(state_.controllers, [&](const auto &entry) {
        const auto *actor = ports_.monsters.find(entry.first); return !actor || actor->life <= 0;
    });
    for (const auto &[id, monster] : ports_.monsters.read().actors) {
        if (monster.life <= 0 || monster.busyUntil > tick.tick || monster.frozenUntil > tick.tick || monster.owner) continue;
        auto [entry, fresh] = state_.controllers.try_emplace(id);
        auto &controller = entry->second;
        if (fresh) controller.random = childRandom(ports_.random);
        if (controller.nextDecision > tick.tick) continue;
        controller.actor = id; controller.nextDecision = tick.tick + uint64_t(monster.rule.decisionTicks);
        const PlayerState *target = nullptr;
        float distance = 25.f; // Original ordinary-monster target search radius.
        for (const auto &[playerId, player] : ports_.players.all()) {
            (void)playerId;
            if (!player.entered || player.area != monster.area || player.persistent.player.hp <= 0) continue;
            const float candidate = (player.position - monster.position).length();
            if (candidate < distance) { distance = candidate; target = &player; }
        }
        if (controller.target != (target ? std::optional(target->actor) : std::nullopt)) {
            controller.pursuing = false; controller.charged = false; ports_.monsters.stop(id);
        }
        controller.target = target ? std::optional(target->actor) : std::nullopt;
        if (!target) { controller.pursuing = false; ports_.monsters.stop(id); continue; }
        const auto &area = ports_.areas.at(monster.area);
        if (area.definition.town) continue;
        const auto &rules = monster.rule.ai;
        if (hasMeleeDecision(rules.kind)) {
            if (meleeFamily(id, target->player, tick, controller) == StepStatus::Blocked) blocked = true;
            continue;
        }
        auto chance = [&](int value) { return int(limitedRandom(controller.random, 100)) < value; };
        const bool contact = meleeDistance(monster.position, monster.rule.size, target->position, 2) <= monster.rule.meleeRange &&
            area.definition.collision.segment(monster.position, target->position);
        bool act = false;
        if (contact) {
            controller.pursuing = false; ports_.monsters.stop(id);
            switch (rules.kind) {
            case MonsterAiKind::Zombie: act = true; break;
            case MonsterAiKind::Fallen: case MonsterAiKind::Skeleton: case MonsterAiKind::Brute: act = chance(rules.params[2]); break;
            default: break;
            }
            if (act) ports_.skills.requestCast({id, 0, UnitTarget{target->actor, 0}, tick.tick});
            else if (rules.kind == MonsterAiKind::Skeleton) controller.nextDecision = tick.tick + uint64_t(rules.params[1]);
        } else {
            switch (rules.kind) {
            case MonsterAiKind::Skeleton: act = controller.pursuing || chance(rules.params[0]); break;
            case MonsterAiKind::Fallen: act = distance <= float(rules.params[1]); break;
            case MonsterAiKind::Zombie: act = distance < float(rules.params[1]) && chance(rules.params[0]); break;
            case MonsterAiKind::Brute: act = true; break;
            default: break;
            }
            if (act) {
                controller.pursuing = true;
                ports_.monsters.requestMove({id, {monster.area, area.generation, target->position}, target->actor, monster.rule.meleeRange, 75, false});
            } else if (rules.kind == MonsterAiKind::Skeleton) controller.nextDecision = tick.tick + uint64_t(rules.params[1]);
        }
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
