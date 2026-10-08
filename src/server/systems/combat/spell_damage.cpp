#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "server/systems/effects/system.hpp"
#include <algorithm>
namespace d2x::server::combat {
DomainResult<SpellPlan> System::prepareSpells(std::vector<SpellImpact> impacts) const {
    SpellPlan plan;
    for (auto &impact : impacts) {
        if (!impact.projectile || !impact.source || impact.next || impact.damage < 0 || impact.damage > INT32_MAX ||
            impact.type == DamageType::Poison || impact.targets.size() > 65536 || std::any_of(impact.coldDivisor.begin(), impact.coldDivisor.end(), [](int n) { return n <= 0; }) ||
            std::any_of(impact.freezeDivisor.begin(), impact.freezeDivisor.end(), [](int n) { return n <= 0; }))
            return {DomainStatus::InvalidRequest, {}};
        if(!impact.targetDamage.empty() && (impact.targetDamage.size()!=impact.targets.size() ||
            std::any_of(impact.targetDamage.begin(),impact.targetDamage.end(),[](int64_t n){return n<0 || n>INT32_MAX;}))) return {DomainStatus::InvalidRequest,{}};
        if (impact.targets.empty()) continue;
        const auto same = [&](const auto &previous) { return previous.projectile == impact.projectile && previous.occurrence == impact.occurrence; };
        if (std::any_of(state_.spells.begin(), state_.spells.end(), same) || std::any_of(plan.spells.begin(), plan.spells.end(), same))
            return {DomainStatus::Stale, {}};
        if (impact.targets.size() > 65536 - state_.spellTargets - plan.targets || state_.spells.size() + plan.spells.size() >= 256)
            return {DomainStatus::Capacity, {}};
        plan.targets += impact.targets.size(); plan.spells.push_back(std::move(impact));
    }
    return {DomainStatus::Applied, std::move(plan)};
}
void System::commitSpells(SpellPlan &&plan) noexcept {
    state_.spellTargets += plan.targets; state_.spells.splice(state_.spells.end(), plan.spells);
}
DomainResult<> System::enqueue(SpellImpact impact) {
    std::vector<SpellImpact> values; values.push_back(std::move(impact));
    auto plan = prepareSpells(std::move(values)); if (!plan) return {plan.status, {}};
    commitSpells(std::move(*plan.value)); return {DomainStatus::Applied, std::monostate{}};
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
        const auto *monsterSource=ports_.monsters.find(impact.source);
        if ((!owner || !owner->entered || owner->area!=impact.area) && (!monsterSource || monsterSource->owner || monsterSource->area!=impact.area)) impact.next=impact.targets.size();
        if(!area || area->definition.town) impact.next=impact.targets.size();
        while (impact.next < impact.targets.size()) {
            if(!owner && monsterSource) {
                const PlayerState *player=nullptr;
                for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.actor==impact.targets[impact.next]) {player=&p;break;}}
                if(!player || !player->entered || player->area!=impact.area || player->persistent.player.hp<=0) {++impact.next;continue;}
                if(!ports_.effects.reactionCapacity()) {blocked=true;break;}
                const ActorContext actor{player->player,player->actor,player->area,area->generation,0,tick.tick};
                const auto result=ports_.effects.receive(actor,impact.targetDamage.empty()?impact.damage:impact.targetDamage[impact.next],impact.type);
                if(result.status==DomainStatus::Capacity) {blocked=true;break;}
                if(result) ports_.effects.react(actor,impact.source,CombatEffectEvent::HitByMissile,impact.returnFire);
                ++impact.next;continue;
            }
            const auto *target = ports_.monsters.find(impact.targets[impact.next]);
            if (!target || target->owner || target->life <= 0 || target->area != impact.area ||
                (impact.nextDelay && target->nextHitTick > tick.tick)) { ++impact.next; continue; }
            const int raw = target->rule.resistances[size_t(impact.type)];
            const int resistance = impact.type == DamageType::Cold && raw < 100 ? std::max(-100, raw - impact.coldPierce) : raw;
            int64_t amount = impact.targetDamage.empty()?impact.damage:impact.targetDamage[impact.next];
            if (impact.staticPercent) {
                const auto floor = std::max<int64_t>(256, target->maximumLife * impact.staticFloors.at(size_t(target->rule.difficulty)) / 100);
                amount = std::max(impact.minimumStaticDamage, target->life * impact.staticPercent / 100);
                amount = std::min(std::max<int64_t>(0, target->life - floor), amount * std::clamp(100 - raw, 0, 100) / 100);
            } else amount = int64_t(mitigateMonsterDamage(float(amount) / 256.f, resistance) * 256.f);
            uint64_t cold = 0;
            if (impact.coldFrames && raw < 100) {
                const int divisor = (impact.freeze ? impact.freezeDivisor : impact.coldDivisor).at(size_t(target->rule.difficulty));
                cold = impact.coldFrames * uint64_t(std::clamp(100 - raw, 0, 200)) / (100u * unsigned(divisor));
            }
            const auto result = ports_.monsters.damage(target->id, impact.source, amount, tick.tick, cold, impact.freeze,impact.hitClass);
            if (result.status == DomainStatus::Capacity) { blocked = true; break; }
            if (result && impact.nextDelay) ports_.monsters.hitDelay(target->id, tick.tick + impact.nextDelay);
            if(result && impact.knockback && target->life>0) {ports_.monsters.knockback(target->id,owner->position,tick.tick);cancel(target->id);}
            ++impact.next;
        }
        if (impact.next == impact.targets.size()) { state_.spellTargets -= impact.targets.size(); it = state_.spells.erase(it); }
        else ++it;
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
