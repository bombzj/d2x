#include "melee_decision.hpp"
#include "skirmish_decision.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
bool hasMeleeDecision(MonsterAiKind kind) {
    return hasSkirmishDecision(kind) || kind == MonsterAiKind::Skeleton || kind == MonsterAiKind::Zombie || kind == MonsterAiKind::Brute || kind == MonsterAiKind::Fallen || kind == MonsterAiKind::Wraith || kind == MonsterAiKind::CorruptRogue || kind == MonsterAiKind::Goatman || kind == MonsterAiKind::CorruptLancer;
}
std::optional<MonsterDecision> decideMeleeMonster(const MonsterAiProfile &rules, MonsterDecisionInput input) {
    if (!hasMeleeDecision(rules.kind) || input.difficulty < 0 || input.difficulty > 2) return {};
    if (hasSkirmishDecision(rules.kind)) return decideSkirmishMonster(rules,input);
    MonsterDecision result;
    result.random = input.random; result.charged = input.charged;
    result.alerted = input.alerted;
    const auto &p = rules.params;
    auto chance = [&](int percent) { return limitedRandom(result.random, 100) < unsigned(percent); };
    if (rules.kind == MonsterAiKind::Wraith) {
        if (chance(p[input.contact ? 2 : 0])) {
            result.action = input.contact ? MonsterDecisionAction::Attack : MonsterDecisionAction::Approach;
            if (!input.contact) result.approachRadius = 12;
        } else result.waitFrames = p[1];
        return result;
    }
    if (rules.kind == MonsterAiKind::Fallen) {
        if (!input.commanded && input.leader && input.distance < 15 && chance(p[0])) {
            result.action = MonsterDecisionAction::Shout; return result;
        }
        if (input.contact) {
            if ((input.alerted && !input.commanded) || chance(p[2])) {
                result.action = MonsterDecisionAction::Attack; result.attackMode = chance(p[3]) ? 4 : 5; result.alerted = false;
            } else if (!input.commanded && chance(30)) result.action = MonsterDecisionAction::Shout;
            else result.waitFrames = input.commanded ? 5 : 10;
        } else if (input.commanded || input.retaliating || input.distance <= p[1]) result.action = MonsterDecisionAction::Approach;
        else if (chance(30)) { result.action = MonsterDecisionAction::Wander; result.stopDistance = 3; }
        else result.waitFrames = 10;
        return result;
    }
    if (rules.kind == MonsterAiKind::Brute) {
        if (input.contact) {
            if (chance(p[2])) { result.action = MonsterDecisionAction::Attack; result.attackMode = chance(p[3]) ? 4 : 5; }
            else if (chance(p[2])) { result.action = MonsterDecisionAction::Circle; result.stopDistance = 4; }
            else result.waitFrames = 15;
        } else {
            result.action = MonsterDecisionAction::Approach;
            result.velocityPercent = 75 + 100 - std::clamp(input.lifePercent, 40, 100);
        }
        return result;
    }
    if (rules.kind == MonsterAiKind::Zombie) {
        if (input.contact) {
            result.action = MonsterDecisionAction::Attack;
            result.attackMode = chance(p[3]) ? 4 : 5;
        } else if (input.retaliating || input.burialGrounds || (input.distance < p[1] && chance(p[0]))) {
            result.action = MonsterDecisionAction::Approach;
            result.velocityPercent = 175; result.running = true;
        } else { result.action = MonsterDecisionAction::Wander; result.stopDistance = 3; }
        return result;
    }
    if (rules.kind == MonsterAiKind::Skeleton) {
        if (chance(p[input.contact ? 2 : 0])) {
            result.action = input.contact ? MonsterDecisionAction::Attack : MonsterDecisionAction::Approach;
            result.stopDistance = rules.meleeRange;
            if (input.contact) result.attackMode = chance(p[3]) ? 4 : 5;
        } else result.waitFrames = p[1];
        return result;
    }
    if (input.contact) {
        bool attack;
        if (rules.kind == MonsterAiKind::CorruptLancer) {
            attack = result.charged || chance(p[1]);
            if (attack) result.charged = false;
            else result.waitFrames = p[2];
        } else {
            attack = chance(p[2]);
            if (!attack) result.waitFrames = p[1];
        }
        if (attack) result.action = MonsterDecisionAction::Attack;
        return result;
    }
    if (rules.kind == MonsterAiKind::CorruptRogue) {
        result.running = true;
        if (input.distance <= 20 - 3 * input.difficulty) {
            if (!chance(p[0])) { result.waitFrames = p[1]; return result; }
            result.running = chance(p[4]);
        }
        result.velocityPercent = 75 + (result.running ? p[3] : 0);
        result.stopDistance = result.running ? 2 : 0;
    } else if (rules.kind == MonsterAiKind::Goatman) {
        if (!chance(p[0])) { result.waitFrames = p[1]; return result; }
    } else {
        if (input.distance > p[4]) { result.charged = true; result.running = true; }
        else {
            if (!chance(p[0])) { result.waitFrames = p[2]; return result; }
            result.running = chance(p[3]);
        }
        result.velocityPercent = result.running ? 175 : 75;
        result.stopDistance = result.running ? std::max(0, rules.meleeRange - 1) : 2;
    }
    result.action = MonsterDecisionAction::Approach;
    return result;
}
float monsterMovementSpeed(int nativeVelocity, int percentage) {
    // Old session.cpp: fixed-point truncation precedes subtile/second conversion.
    return float((int64_t(nativeVelocity) * 256 * std::max(25, percentage)) / 100) * 25.f / 4096.f;
}
}
