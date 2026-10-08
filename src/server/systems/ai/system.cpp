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
        std::optional<UnitTarget> target;Vec targetPosition;int targetSize=2;
        float distance = 25.f; // Original ordinary-monster target search radius.
        for (const auto &[playerId, player] : ports_.players.all()) {
            (void)playerId;
            if (!player.entered || player.area != monster.area || player.persistent.player.hp <= 0) continue;
            const float candidate = (player.position - monster.position).length();
            if (candidate < distance) { distance = candidate; target=UnitTarget{player.actor,0,0};targetPosition=player.position;targetSize=2; }
        }
        for(const auto &[petId,pet]:ports_.monsters.read().actors) if(pet.amazonPet && pet.life>0 && pet.area==monster.area) {
            const float candidate=(pet.position-monster.position).length();
            if(candidate<distance) {distance=candidate;target=UnitTarget{petId,0,1};targetPosition=pet.position;targetSize=pet.rule.size;}
        }
        if (controller.target != (target ? std::optional(target->id) : std::nullopt)) {
            controller.pursuing = false; controller.charged = false; ports_.monsters.stop(id);
        }
        controller.target = target ? std::optional(target->id) : std::nullopt;
        if (!target) { controller.pursuing = false; ports_.monsters.stop(id); continue; }
        const auto &area = ports_.areas.at(monster.area);
        if (area.definition.town) continue;
        const auto &rules = monster.rule.ai;
        if (hasMeleeDecision(rules.kind)) {
            if (meleeFamily(id,*target,targetPosition,targetSize,tick,controller) == StepStatus::Blocked) blocked = true;
            continue;
        }
        auto chance = [&](int value) { return int(limitedRandom(controller.random, 100)) < value; };
        const bool contact = meleeDistance(monster.position, monster.rule.size, targetPosition, targetSize) <= monster.rule.meleeRange &&
            area.definition.collision.segment(monster.position, targetPosition);
        bool act = false;
        if (contact) {
            controller.pursuing = false; ports_.monsters.stop(id);
            switch (rules.kind) {
            case MonsterAiKind::Zombie: act = true; break;
            case MonsterAiKind::Fallen: case MonsterAiKind::Skeleton: case MonsterAiKind::Brute: act = chance(rules.params[2]); break;
            default: break;
            }
            if (act) ports_.skills.requestCast({id, 0, *target, tick.tick});
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
                ports_.monsters.requestMove({id, {monster.area, area.generation, targetPosition}, target->id, monster.rule.meleeRange, 75, false});
            } else if (rules.kind == MonsterAiKind::Skeleton) controller.nextDecision = tick.tick + uint64_t(rules.params[1]);
        }
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
