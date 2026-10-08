#include "melee_decision.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
bool hasMeleeDecision(MonsterAiKind kind) {
    return kind == MonsterAiKind::CorruptRogue || kind == MonsterAiKind::Goatman || kind == MonsterAiKind::CorruptLancer;
}
std::optional<MeleeDecision> decideMeleeMonster(const MonsterAiProfile &rules, MeleeDecisionInput input) {
    if (!hasMeleeDecision(rules.kind) || input.difficulty < 0 || input.difficulty > 2) return {};
    MeleeDecision result;
    result.random = input.random; result.charged = input.charged;
    const auto &p = rules.params;
    auto chance = [&](int percent) { return limitedRandom(result.random, 100) < unsigned(percent); };
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
        if (attack) result.action = MeleeDecisionAction::Attack;
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
    result.action = MeleeDecisionAction::Approach;
    return result;
}
float monsterMovementSpeed(int nativeVelocity, int percentage) {
    // Old session.cpp: fixed-point truncation precedes subtile/second conversion.
    return float((int64_t(nativeVelocity) * 256 * std::max(25, percentage)) / 100) * 25.f / 4096.f;
}
}
