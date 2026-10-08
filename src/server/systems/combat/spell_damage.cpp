#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include <algorithm>
namespace d2x::server::combat {
DomainResult<> System::enqueue(SpellImpact impact) {
    if (!impact.projectile || !impact.source || impact.next || impact.damage < 0 || impact.damage > INT32_MAX ||
        impact.type != DamageType::Fire || impact.targets.size() > 65536) return {DomainStatus::InvalidRequest, {}};
    if (impact.targets.empty()) return {DomainStatus::Applied, std::monostate{}};
    if (state_.spells.size() >= 256 || impact.targets.size() > 65536 - state_.spellTargets) return {DomainStatus::Capacity, {}};
    if (std::any_of(state_.spells.begin(), state_.spells.end(), [&](const auto &previous) { return previous.projectile == impact.projectile; }))
        return {DomainStatus::Stale, {}};
    // Missile targets are unique, and the bounded cursor survives output pressure.
    const auto count = impact.targets.size();
    state_.spells.push_back(std::move(impact)); state_.spellTargets += count;
    return {DomainStatus::Applied, std::monostate{}};
}
StepStatus System::resolveSpells(TickContext tick) {
    bool blocked = false;
    for (auto it = state_.spells.begin(); it != state_.spells.end();) {
        auto &impact = *it;
        const PlayerState *owner = nullptr;
        for (const auto &[id, player] : ports_.players.all()) {
            (void)id; if (player.actor == impact.source) { owner = &player; break; }
        }
        const auto *area = ports_.areas.find(impact.area);
        // A released projectile can outlive its owner's life, but not a removed
        // connection or this slice's area binding. It never becomes another player's spell.
        if (!owner || !owner->entered || owner->area != impact.area || !area || area->definition.town) impact.next = impact.targets.size();
        while (impact.next < impact.targets.size()) {
            const auto *target = ports_.monsters.find(impact.targets[impact.next]);
            if (!target || target->life <= 0 || target->area != impact.area) { ++impact.next; continue; }
            const auto amount = int64_t(mitigateMonsterDamage(float(impact.damage) / 256.f,
                target->rule.resistances[size_t(impact.type)]) * 256.f);
            const auto result = ports_.monsters.damage(target->id, impact.source, amount, tick.tick);
            if (result.status == DomainStatus::Capacity) { blocked = true; break; }
            ++impact.next;
        }
        if (impact.next == impact.targets.size()) { state_.spellTargets -= impact.targets.size(); it = state_.spells.erase(it); }
        else ++it;
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
