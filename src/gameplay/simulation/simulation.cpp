#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <type_traits>

namespace d2x {
Simulation::Simulation(EntityIds &ids) : ids_(ids) {
    state_.player.id = ids_.allocate();
    heal();
}
void Simulation::clearActions() {
    state_.player.pendingCast.reset();
    stopChannel(state_.player);
    auto &p = state_.player;
    p.route.clear();
    p.attackTarget = {};
    p.throwAttack = p.leftHandAttack = false;
    p.castTime = p.meleeTime = p.hitTime = 0;
    p.weaponAttack.reset();
    p.attackPosition.reset();
    p.moving = false;
    p.runningNow = false;
    state_.message.clear();
}
AreaState Simulation::leaveArea() {
    return std::move(state_.area);
}
void Simulation::enterArea(const Grid &grid, const RoomLayout &rooms, Vec spawn, bool safeZone, AreaState area,
                           std::span<const MonsterSpawn> monsters, std::optional<Vec> coordinateOffset) {
    grid_ = &grid;
    rooms_ = &rooms;
    safeZone_ = safeZone;
    state_.area = std::move(area);
    clearActions();
    state_.player.pos = state_.player.previous = grid.walkable(spawn, playerMovement) ? spawn : grid.nearest(spawn, playerMovement);
    if (!state_.area.initialized) {
        state_.area.pendingSpawns.assign(monsters.begin(), monsters.end());
        state_.area.initialized = true;
    }
    activateMonsters();
    relocateCompanions(state_.player.id, state_.player.pos);
    emit(RegionEntered{state_.area.region, coordinateOffset});
}
void Simulation::restartArea(Vec spawn, std::span<const MonsterSpawn> monsters) {
    auto id = state_.area.region;
    heal();
    AreaState area;
    area.region = id;
    enterArea(*grid_, *rooms_, spawn, safeZone_, std::move(area), monsters);
}
void Simulation::heal() {
    auto &p = state_.player;
    p.hp = state_.player.attributes.maxLife;
    p.mana = state_.player.attributes.maxMana;
    p.stamina = state_.player.attributes.maxStamina;
    p.healing.clear();
    p.manaRestoration.clear();
    // NPC healing cures ailments; it does not dispel beneficial item states.
    // D2MOO SUNITNPC_HealPlayer removes poison/freeze/curable states only.
    p.chill = 0;
    p.poisonRemaining = p.poisonPerSecond = 0;
    p.webSlowRemaining = 0;
    p.webSlowPercent = 0;
    p.webSource = {};
    if (p.hireling.active() && hirelingAttributes_) {
        auto &merc = p.hireling;
        merc.hp = float(hirelingAttributes_().maxLife);
        merc.chill = merc.poisonRemaining = merc.poisonPerSecond = 0;
        merc.webSlowRemaining = 0; merc.healing.clear();
    }
    p.dead = false;
    p.deathTime = 0;
}
void Simulation::spawnEnemies(std::span<const MonsterSpawn> spawns) {
    auto &area = state_.area;
    const size_t first = area.enemies.size();
    area.enemies.reserve(area.enemies.size() + spawns.size());
    for (const auto &spawn : spawns) {
        Enemy enemy;
        enemy.id = ids_.allocate();
        enemy.combatRandom = childRandom(unitRandom_);
        enemy.identity = spawn.identity;
        enemy.kind = spawn.kind;
        enemy.pos = spawn.position;
        enemy.maxHp = monsterDefinition(enemy.kind).maxLife;
        auto baseIdentity = enemy.identity;
        if (baseIdentity.rank == MonsterRank::Champion || baseIdentity.rank == MonsterRank::Unique)
            baseIdentity.rank = MonsterRank::Normal;
        if (monsterNormalCombat_)
            if (auto combat = monsterNormalCombat_(baseIdentity, area.region)) {
                rollRandom(enemy.combatRandom);
                enemy.maxHp = float(combat->minLife +
                    uint32_t(enemy.combatRandom) % unsigned(combat->maxLife - combat->minLife + 1));
            }
        enemy.hp = enemy.maxHp;
        if (enemy.kind == MonsterKind::FoulCrowNest && monsterAi_)
            if (auto ai = monsterAi_(enemy); ai && ai->kind == MonsterAiKind::FoulCrowNest)
                enemy.aiWait = float(ai->params[0]) / 25.f;
        area.enemies.push_back(std::move(enemy));
    }
    if (initializeNaturalElite_) {
        for (size_t index = first; index < area.enemies.size(); ++index)
            initializeNaturalElite_(area.enemies[index], nullptr);
        for (size_t index = first; index < area.enemies.size(); ++index) {
            auto &minion = area.enemies[index];
            if (minion.identity.ownerSpawnKey.empty()) continue;
            const auto owner = std::find_if(area.enemies.begin(), area.enemies.end(), [&](const Enemy &candidate) {
                return candidate.identity.spawnKey == minion.identity.ownerSpawnKey;
            });
            if (owner != area.enemies.end()) initializeNaturalElite_(minion, &*owner);
        }
    }
}
Enemy *Simulation::findEnemy(EntityId id) {
    auto &enemies = state_.area.enemies;
    auto found = std::find_if(enemies.begin(), enemies.end(), [id](const Enemy &e) { return e.id == id; });
    if (found != enemies.end()) return &*found;
    for (auto &companion : state_.companions) if (companion.id == id) return &companion;
    return nullptr;
}
void Simulation::execute(const GameCommand &command) {
    if (!grid_)
        return;
    std::visit(
        [this](const auto &intent) {
            using T = std::decay_t<decltype(intent)>;
            if constexpr (std::is_same_v<T, MoveTo>)
                moveTo(intent.position);
            else if constexpr (std::is_same_v<T, Attack>)
                requestAttack(intent);
            else if constexpr (std::is_same_v<T, DebugKill>) {
                if (!state_.player.dead)
                    if (auto enemy = findEnemy(intent.target);
                        enemy && enemy->hp > 0 && (intent.ignoreActivation || active(enemy->pos)))
                        damageEnemy(*enemy, enemy->hp, state_.player.id, 0, intent.ignoreActivation);
            }
            else if constexpr (std::is_same_v<T, ToggleRun>)
                state_.player.running = !state_.player.running;
            else if constexpr (std::is_same_v<T, StopMoving>)
                stopWalking();
            else if constexpr (std::is_same_v<T, StopChannel>)
                stopChannel(state_.player);
        },
        command);
}
void Simulation::combatEffectsChanged(std::span<const RemovedCombatEffect> removed) {
    if (combatEffectsChanged_) combatEffectsChanged_();
    for (const auto &entry : removed)
        if (entry.effect.spec.restoreStaminaOnRemoval)
            state_.player.stamina = float(state_.player.attributes.maxStamina);
}
void Simulation::tick(float dt, Vec keyboard, bool forceRun) {
    if (!grid_ || dt <= 0)
        return;
    auto &p = state_.player;
    forceRun_ = forceRun;
    p.previous = p.pos;
    ++state_.frame;
    state_.time += dt;
    if (auto removed = p.combatEffects.expire(state_.frame); !removed.empty())
        combatEffectsChanged(removed);
    advanceSkillCasting(p, dt, keyboard.length() > .1f);
    advanceWeaponAttack();
    p.castTime = std::max(0.f, p.castTime - dt);
    p.hitTime = std::max(0.f, p.hitTime - dt);
    p.chill = std::max(0.f, p.chill - dt);
    p.webSlowRemaining = std::max(0.f, p.webSlowRemaining - dt);
    if (p.webSlowRemaining == 0) {
        p.webSlowPercent = 0;
        p.webSource = {};
    }
    p.moving = false;
    if (p.dead)
        p.deathTime += dt;
    for (auto unit : combatUnits()) {
        if (!active(*unit.position)) continue;
        if (unit.monster) {
            unit.monster->hitFlash = std::max(0.f, unit.monster->hitFlash - dt);
            if (!unit.alive()) unit.monster->deathAge += dt;
        }
        if (!unit.alive()) continue;
        auto periodic = [&](auto &record) {
            if (record.poisonRemaining > 0) {
                const float elapsed = std::min(dt, record.poisonRemaining);
                record.poisonRemaining -= elapsed;
                float amount = record.poisonPerSecond * elapsed;
                // Player poison cannot deliver the killing blow; monster poison can.
                if (unit.player || safeZone_) amount = std::min(amount, std::max(0.f, *unit.life - 1.f));
                dealDamage({record.poisonSource, unit.id, amount, MonsterDamageType::Poison, 0, true, false, false, DamagePermission::ExistingEffect});
                if (record.poisonRemaining <= 0) record.poisonPerSecond = 0;
            }
            if (record.openWoundsRemaining > 0) {
                const float elapsed = std::min(dt, record.openWoundsRemaining);
                record.openWoundsRemaining -= elapsed;
                dealDamage({record.openWoundsSource, unit.id, record.openWoundsPerSecond * elapsed,
                            MonsterDamageType::Physical, 0, true, false, false, DamagePermission::ExistingEffect});
                if (record.openWoundsRemaining <= 0) record.openWoundsPerSecond = 0;
            }
        };
        if (unit.player) periodic(*unit.player);
        else if (unit.hireling) periodic(*unit.hireling);
        else periodic(*unit.monster);
    }
    if (!p.dead) {
        updatePotions(dt);
        updatePlayer(dt, keyboard);
        activateMonsters();
    }
    updateMonsterEnchantments();
    updateMonsters(dt);
    updateCompanions(dt);
    updateMissiles(dt);
    if (!p.dead && p.hp <= 0) {
        p.dead = true;
        combatEffectsChanged(p.combatEffects.onDeath(EffectUnitKind::Player));
        stopChannel(p);
        p.pendingCast.reset();
        p.weaponAttack.reset();
        p.attackPosition.reset();
        p.meleeTime = 0;
        p.castTime = 0;
        p.healing.clear();
        p.manaRestoration.clear();
        p.chill = 0;
        p.poisonRemaining = p.poisonPerSecond = 0;
        p.route.clear();
        p.attackTarget = {};
        p.throwAttack = false;
        p.leftHandAttack = false;
        state_.message = "You have died. Press Ctrl+R to return.";
        for (const auto &pet : state_.companions)
            if (pet.hp > 0 && pet.allegiance.owner == p.id) enforceSummonLimit(p.id, pet.summonSkill, 0);
        emit(PlayerDied{p.id});
    }
    if (!p.dead && state_.area.pendingSpawns.empty() && !state_.area.enemies.empty() &&
        state_.area.kills == int(state_.area.enemies.size()))
        state_.message = "Area cleared. Ctrl+F2: travel onward. Ctrl+R: repopulate the area.";
    for (auto &e : state_.area.effects)
        e.age += dt;
    std::erase_if(state_.area.effects, [](const Effect &e) { return e.age >= e.duration; });
}
} // namespace d2x
