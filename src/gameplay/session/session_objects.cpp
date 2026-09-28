#include "core/random.hpp"
#include "gameplay/session/session.hpp"
#include "content/object_loot.hpp"
#include "content/item_quality.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace d2x {
namespace {
uint32_t roll(uint64_t &state, uint32_t bound) {
    return limitedRandom(state, bound);
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
    if (state().player.dead || state().player.hp <= 0 || found->operatedAt >= 0 ||
        found->modeAt(state().time) != 0) return;
    ItemHandle key;
    const bool unlocking = found->chest && found->chest->locked;
    if (unlocking && characterDefinition_.code != "ass") {
        // ItemMode::DoKeyCheck: only inv page 0; never stash, cube, belt or cursor.
        for (auto itemId : inventory_.contents(playerContainers_.backpack)) {
            const auto *item = inventory_.item(itemId);
            const auto *definition = inventory_.catalog().find(item->definition);
            if (item->quantity && definition && definition->equipment.isType("key")) {
                key = item->handle();
                break;
            }
        }
        if (!key.id) {
            simulation_.emit(InteractionFailed{id, "I need a key.", true});
            return;
        }
    }
    LootPlan plan;
    plan.randomState = loot_.randomState();
    auto objectSeed = regions_.at(current_).objectSeed;
    if (found->chest) {
        std::set<size_t> usedUniques;
        for (auto row : loot_.usedUniques()) usedUniques.insert(size_t(row));
        plan = planChestLoot(content_, resolveObjectTreasure(content_, worldContent_,
            region().definition.id, state().population.difficulty), *found->chest,
            found->objectClass, objectSeed, usedUniques, characterDefinition_.code,
            characterStats().combat.magicFind, characterStats().combat.goldFind);
        // Unsupported data must not consume a key or permanently empty a chest.
        if (!plan.deferred.empty()) {
            simulation_.emit(LootDeferred{id, plan.deferred});
            return;
        }
    } else if (found->operateFn == 19 || found->operateFn == 20) {
        plan = planAct1RackLoot(content_, worldContent_, region().definition.id,
                                state().population.difficulty, found->operateFn == 20,
                                plan.randomState);
    } else if (found->operateFn == 7) {
        // Native barrel explosion art is not implemented; do not reuse a demo spell.
        // ObjEval::OBJEVAL_ApplyTrapObjectDamage: an environment hazard hits
        // every eligible unit independently of faction, with the same mitigation.
        for (auto target : simulation_.combatUnits()) {
            if (!target.alive() || (found->pos - *target.position).length() > 3.f ||
                !map().grid.missileSegment(found->pos, *target.position, {0x04, 1})) continue;
            const int level = target.stats.level;
            const auto &attributes = target.stats.attributes;
            const int parameter = 2 * (int(uint8_t(level + int(roll(plan.randomState, unsigned(level >> 2))))) -
                5 * (attributes.dexterity >> 1) - level);
            const int chance = std::max(parameter - attributes.defense + 125, 65);
            if (int(roll(plan.randomState, 100)) >= chance) continue;
            const int life = int(*target.life * 256.f);
            const int minimum = std::max(life >> 5, 1), maximum = std::max(life >> 3, minimum + 1);
            const float damage = float((minimum + int(roll(plan.randomState, unsigned(maximum - minimum + 256)))) *
                int64_t(found->objectDamage) / 100) / 256.f;
            DamageRequest request{id, target.id, damage};
            request.permission = DamagePermission::Environment;
            simulation_.dealDamage(request);
        }
    } else {
        const auto entry = resolveObjectTreasure(content_, worldContent_, region().definition.id,
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
    if (key.id) {
        auto consumed = inventory_.consume(key, 1, inventoryAccess());
        const bool succeeded = bool(consumed);
        publishInventory(std::move(consumed), key.id);
        if (!succeeded) return;
    }
    if (found->chest) {
        found->chest->locked = false;
        found->chest->lootSeed = plan.randomState;
        regions_.at(current_).objectSeed = objectSeed;
    }
    auto drops = loot_.settle({id, {}, region().definition.id, state().population.difficulty,
                              false, found->chest.has_value()},
                              std::move(plan));
    spawnLoot(drops, region().definition.id, found->pos);
    found->operatedAt = state().time;
    found->interaction = Interaction::None;
    simulation_.emit(ObjectInteracted{id, Interaction::Loot, found->name, false, unlocking});
}
void GameSession::activateShrine(EntityId id) {
    auto &objects = regions_.at(current_).objects;
    auto found = std::find_if(objects.begin(), objects.end(), [id](const WorldObject &value) {
        return value.id == id;
    });
    if (found == objects.end() || found->interaction != Interaction::Shrine ||
        found->shrineCode <= 0) return;
    if (!applyShrine(found->shrineCode, found->id, found->pos)) return;
    found->operatedAt = state().time;
    found->interaction = Interaction::None;
    simulation_.emit(ObjectInteracted{id, Interaction::Shrine, found->shrineName});
}
void GameSession::grantShrine(int code) {
    code = activeShrineCode(code);
    const auto found = content_.shrines.find(code);
    if (state().player.dead || found == content_.shrines.end()) return;
    if (applyShrine(code, {}, state().player.pos))
        simulation_.emit(ObjectInteracted{{}, Interaction::Shrine, found->second.name});
}
void GameSession::drinkWell(EntityId id) {
    auto &objects = regions_.at(current_).objects;
    auto found = std::find_if(objects.begin(), objects.end(), [id](const WorldObject &value) {
        return value.id == id;
    });
    if (found == objects.end() || found->interaction != Interaction::Well ||
        found->remainingUses <= 0 || found->parameters[2] <= 0) return;
    auto &player = simulation_.state_.player;
    const auto &stats = simulation_.state_.player.attributes;
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
