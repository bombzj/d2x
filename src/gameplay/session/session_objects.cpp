#include "gameplay/session/session.hpp"
#include "content/object_loot.hpp"
#include "content/item_quality.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace d2x {
namespace {
uint32_t roll(uint64_t &state, uint32_t bound) {
    state = uint64_t(uint32_t(state)) * 0x6ac690c5ULL + (state >> 32);
    return bound ? uint32_t(state) % bound : 0;
}
} // namespace
void GameSession::activateLootObject(EntityId id) {
    // Region objects are world state; a character-only disk restore rebuilds them.
    auto &live = regions_.at(current_).objects;
    auto found = std::find_if(live.begin(), live.end(), [id](const WorldObject &value) { return value.id == id; });
    if (found == live.end() || found->interaction != Interaction::Loot ||
        (found->operateFn != 1 && found->operateFn != 3 && found->operateFn != 4 &&
         found->operateFn != 5 && found->operateFn != 7 && found->operateFn != 14 &&
         found->operateFn != 19 && found->operateFn != 20) ||
        loot_.settled(id))
        return;
    LootPlan plan;
    plan.randomState = loot_.randomState();
    if (found->operateFn == 19 || found->operateFn == 20) {
        plan = planAct1RackLoot(content_, worldContent_, region().definition.id,
                                state().population.difficulty, found->operateFn == 20,
                                plan.randomState);
    } else if (found->operateFn == 7) {
        // Native barrel explosion art is not implemented; do not reuse a demo spell.
        auto damage = [&](float life) {
            const float minimum = std::max(life / 32.f, 1.f / 256.f);
            const float maximum = std::max(life / 8.f, minimum + 1.f / 256.f);
            return (minimum + (maximum - minimum) * float(roll(plan.randomState, 1000)) / 999.f) *
                   float(found->objectDamage) / 100.f;
        };
        auto &player = simulation_.state_.player;
        if ((player.pos - found->pos).length() <= 3.f && roll(plan.randomState, 100) < 65)
            player.hp -= damage(player.hp);
        for (auto &enemy : simulation_.state_.area.enemies)
            if (enemy.hp > 0 && (enemy.pos - found->pos).length() <= 3.f &&
                roll(plan.randomState, 100) < 65)
                simulation_.damageEnemy(enemy, damage(enemy.hp), id);
    } else {
        const auto entry = resolveAct1ObjectTreasure(content_, worldContent_, region().definition.id,
                                                      state().population.difficulty);
        if (!entry.deferred.empty()) {
            plan.deferred = entry.deferred;
        } else {
            const bool drops = found->operateFn == 1 || found->operateFn == 14 ||
                (found->operateFn == 4 && roll(plan.randomState, 100) >= 25) ||
                ((found->operateFn == 3 || found->operateFn == 5) &&
                 roll(plan.randomState, 100) <= 20);
            if (drops) {
                const auto ratios = content_.tables.find("itemratio");
                if (ratios == content_.tables.end())
                    plan.deferred = "MPQ ItemRatio is unavailable";
                else {
                    std::set<size_t> usedUniques;
                    for (auto row : loot_.usedUniques()) usedUniques.insert(size_t(row));
                    plan = planItemLoot(content_, ratios->second, entry.treasureClass, entry.itemLevel,
                                        0, plan.randomState, usedUniques, characterDefinition_.code,
                                        characterStats().combat.magicFind, characterStats().combat.goldFind);
                }
            }
        }
    }
    if (!plan.deferred.empty())
        simulation_.emit(LootDeferred{id, plan.deferred});
    auto drops = loot_.settle({id, {}, region().definition.id, state().population.difficulty},
                              std::move(plan));
    spawnLoot(drops, region().definition.id, found->pos);
    found->operatedAt = state().time;
    found->interaction = Interaction::None;
    simulation_.emit(ObjectInteracted{id, Interaction::Loot, found->name});
}
void GameSession::activateShrine(EntityId id) {
    auto &objects = regions_.at(current_).objects;
    auto found = std::find_if(objects.begin(), objects.end(), [id](const WorldObject &value) {
        return value.id == id;
    });
    if (found == objects.end() || found->interaction != Interaction::Shrine ||
        found->shrineCode <= 0) return;
    applyShrine(found->shrineCode, found->shrineName, found->shrineEffect,
                found->shrineDuration);
    found->operatedAt = state().time;
    found->interaction = Interaction::None;
    simulation_.emit(ObjectInteracted{id, Interaction::Shrine, found->shrineName});
}
void GameSession::applyShrine(int code, std::string name, std::string effect, float duration) {
    auto &player = simulation_.state_.player;
    if (code == 1) {
        player.hp = float(simulation_.characterStats_.maxLife);
        player.mana = float(simulation_.characterStats_.maxMana);
    } else if (code == 2) {
        player.hp = float(simulation_.characterStats_.maxLife);
    } else if (code == 3) {
        player.mana = float(simulation_.characterStats_.maxMana);
    }
    if (duration > 0) {
        const auto record = content_.states.find(shrineStateName(code));
        if (record == content_.states.end()) return; // Unverified special shrine state.
        CombatEffectSpec stateEffect;
        stateEffect.state = record->second.definition;
        stateEffect.source = {CombatEffectSource::Shrine, player.id, code, 0};
        stateEffect.duration = EffectFrame(duration * 25.f + .5f);
        // Attributes remain deferred, but display lifetime must obey the same
        // MPQ death/expiry rules as every other state, including stambarblue.
        for (const auto &previous : shrineStatuses_) player.combatEffects.remove(previous.stateEffect);
        const auto applied = player.combatEffects.apply(std::move(stateEffect), state().frame);
        shrineStatuses_.clear();
        shrineStatuses_.push_back({code, std::move(name), std::move(effect), state().time + duration,
                                   applied.handle});
        refreshCharacter();
    }
}
void GameSession::grantShrine(int code) {
    if (state().player.dead || code <= 0) return;
    const auto &table = content_.tables.at("shrines");
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (table.number(row, "Code") == code) {
            const std::string name(table.value(row, "Shrine name"));
            applyShrine(code, name, std::string(table.value(row, "Effect")),
                        float(table.number(row, "Duration in frames").value_or(0)) / 25.f);
            simulation_.emit(ObjectInteracted{{}, Interaction::Shrine, name});
            return;
        }
}
void GameSession::drinkWell(EntityId id) {
    auto &objects = regions_.at(current_).objects;
    auto found = std::find_if(objects.begin(), objects.end(), [id](const WorldObject &value) {
        return value.id == id;
    });
    if (found == objects.end() || found->interaction != Interaction::Well ||
        found->remainingUses <= 0 || found->parameters[2] <= 0) return;
    auto &player = simulation_.state_.player;
    const auto &stats = simulation_.characterStats_;
    const float fraction = float(found->parameters[1]) / 256.f;
    bool used = false;
    auto restore = [&](float &value, int maximum) {
        if (value >= maximum) return;
        value = std::min(float(maximum), value + float(maximum) * fraction);
        used = true;
    };
    if (found->parameters[3] & 2) restore(player.hp, stats.maxLife);
    if (found->parameters[3] & 1) restore(player.mana, stats.maxMana);
    restore(player.stamina, stats.maxStamina);
    if (player.poisonRemaining > 0 || player.chill > 0 || player.webSlowRemaining > 0) {
        player.poisonRemaining = player.poisonPerSecond = player.chill = player.webSlowRemaining = 0;
        player.webSlowPercent = 0;
        player.webSource = {};
        used = true;
    }
    if (!used) return;
    if (found->operatedAt < 0) found->operatedAt = state().time;
    --found->remainingUses;
    found->animationMode = 2 - found->remainingUses / found->parameters[2];
    if (!found->remainingUses) found->interaction = Interaction::None;
    simulation_.emit(ObjectInteracted{id, Interaction::Well, found->name});
}
void GameSession::updateObjectTimers() {
    const float now = state().time;
    const auto effects = state().player.combatEffects.entries();
    std::erase_if(shrineStatuses_, [&](const ShrineStatus &status) {
        return std::none_of(effects.begin(), effects.end(), [&](const ActiveCombatEffect &effect) {
            return effect.handle == status.stateEffect && effect.activeAt(state().frame);
        });
    });
    for (auto &region : regions_)
        for (auto &object : region.objects) {
            if (object.operateFn == 22 && object.parameters[2] > 0 && object.parameters[0] > 0 &&
                object.operatedAt >= 0 && now >= object.operatedAt +
                    float(object.parameters[0]) / 25.f) {
                const int maximum = 2 * object.parameters[2];
                object.remainingUses = std::min(maximum, object.remainingUses + 1);
                object.animationMode = 2 - object.remainingUses / object.parameters[2];
                object.interaction = Interaction::Well;
                object.operatedAt = object.remainingUses == maximum ? -1 : now;
            }
            if (object.shrineCode > 0 && object.shrineReset > 0 && object.operatedAt >= 0 &&
                now >= object.operatedAt + object.shrineReset) {
                object.operatedAt = -1;
                object.interaction = Interaction::Shrine;
            }
        }
}
} // namespace d2x
